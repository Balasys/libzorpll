/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifdef _MSC_VER
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <sys/poll.h>
#endif

#include <zorpll/stream.h>
#include <zorpll/log.h>
#include <zorpll/zorplib.h>
#include <zorpll/error.h>

#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include <openssl/err.h>

#include <zorpll/ssl.h>
#include <zorpll/streamssl.h>

#include <openssl/err.h>
#define ERR_buflen 4096

#define DO_AS_USUAL          0
#define CALL_READ_WHEN_WRITE 1
#define CALL_WRITE_WHEN_READ 2

extern ZClass ZStreamSsl__class;

/**
 * ZStream-derived class to handle connections over SSL.
 **/
typedef struct _ZStreamSsl
{
  ZStream super;

  guint what_if_called;
  gboolean shutdown;

  ZSSLSession *ssl;
  gchar error[ERR_buflen];

  /* List of handshake objects. Unfortunately OpenSSL callbacks cannot be
   * handed a destroy_notify callback so we generally cannot use
   * refcounting to manage the lifetime of handshake objects.
   *
   * Instead, we do store all handshake objects in this linked list in
   * the associated ZStreamSsl and make sure we delete these when we
   * can guarantee that the handshake is no longer needed
   * (referenced).
   *
   * Right now this means we delete handshake objects only when
   * closing the stream.
   */
  GList *handshakes;
} ZStreamSsl;

/**
 * ZStreamSsl class descriptor.
 **/
extern ZClass ZStreamSsl__class;

static gboolean
z_stream_ssl_read_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond, gpointer s)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  gboolean rc;

  z_enter();
  if (self->what_if_called == CALL_WRITE_WHEN_READ)
    rc = (*self->super.write_cb)(static_cast<_ZStream *>(s), poll_cond, self->super.user_data_write);
  else
    rc = (*self->super.read_cb)(static_cast<_ZStream *>(s), poll_cond, self->super.user_data_read);
  z_return(rc);
}

static gboolean
z_stream_ssl_write_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond, gpointer s)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  gboolean rc;
  
  z_enter();
  if (self->what_if_called == CALL_READ_WHEN_WRITE)
    rc = (*self->super.read_cb)(static_cast<_ZStream *>(s), poll_cond, self->super.user_data_read);
  else
    rc = (*self->super.write_cb)(static_cast<_ZStream *>(s), poll_cond, self->super.user_data_write);
  z_return(rc);
}

static gboolean
z_stream_ssl_pri_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond, gpointer s)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  gboolean rc;

  z_enter();
  rc = (*self->super.pri_cb)(static_cast<_ZStream *>(s), poll_cond, self->super.user_data_pri);
  z_return(rc);
}

/* virtual functions */

static GIOStatus
z_stream_ssl_read_method_impl(ZStreamSsl *self, void *buf, gsize count, gsize *bytes_read, GError **error)
{
  gint result;
  gint ssl_err;

  z_enter();
  result = SSL_read(self->ssl->ssl, buf, count);

  if (result < 0)
    {
      *bytes_read = 0;
      ssl_err = SSL_get_error(self->ssl->ssl, result);
      switch (ssl_err)
        {
        case SSL_ERROR_ZERO_RETURN:
          z_return(G_IO_STATUS_EOF);

        case SSL_ERROR_WANT_READ:
          z_return(G_IO_STATUS_AGAIN);

        case SSL_ERROR_WANT_WRITE:
          if (self->what_if_called == DO_AS_USUAL)
            {
              z_stream_set_cond(self->super.child, G_IO_OUT, TRUE);
            }
          self->what_if_called = CALL_READ_WHEN_WRITE;
          z_return(G_IO_STATUS_AGAIN);

        case SSL_ERROR_SYSCALL:
          if (z_errno_is(EAGAIN) || z_errno_is(EINTR))
            z_return(G_IO_STATUS_AGAIN);

          if (z_errno_is(0))
            z_return(G_IO_STATUS_EOF);
          /*LOG
            This message indicates that an OS level error occurred during the SSL read. 
           */
          g_set_error(error, G_IO_CHANNEL_ERROR, g_io_channel_error_from_errno(errno), "%s", g_strerror(errno));
          z_return(G_IO_STATUS_ERROR);

        case SSL_ERROR_SSL:
        default:
          z_ssl_get_error_str(self->error, ERR_buflen);
          ERR_clear_error();

          /*LOG
            This message indicates that an SSL error occurred during the SSL read. 
           */
          g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "SSL error occurred (%s)", self->error);
          z_return(G_IO_STATUS_ERROR);
        }
    }
  if (result == 0)
    {
      *bytes_read = result;
      ERR_clear_error();
      z_return(G_IO_STATUS_EOF);
    }

  if (self->what_if_called != DO_AS_USUAL)
    {
      z_stream_set_cond(self->super.child, G_IO_OUT, FALSE);
      self->what_if_called = DO_AS_USUAL;
    }
  *bytes_read = result;
  ERR_clear_error();
  z_return(G_IO_STATUS_NORMAL);
}

static GIOStatus
z_stream_ssl_read_method(ZStream *s, void *buf, gsize count,gsize *bytes_read, GError **error)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  gint result;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (self->what_if_called == CALL_WRITE_WHEN_READ)
    {
      /*LOG
        This message indicates an internal error. Please report this event to the Balabit
        QA Team (devel@balabit.com).
       */
      z_log(NULL, CORE_ERROR, 2, "Internal error; error='Read called, when only write might be called'");
    }

  if (self->shutdown)
    z_return(G_IO_STATUS_EOF);

  self->super.child->timeout = self->super.timeout;

  if (self->ssl)
    result = z_stream_ssl_read_method_impl(self, buf, count, bytes_read, error);
  else
    result = z_stream_read(self->super.child, buf, count, bytes_read, error);

  z_return(static_cast<GIOStatus>(result));
}


static GIOStatus
z_stream_ssl_write_method_impl(ZStreamSsl *self, const void *buf, gsize count, gsize *bytes_written, GError **error)
{
  gint result;
  gint ssl_err;

  z_enter();
  result = SSL_write(self->ssl->ssl, buf, count);

  if (result < 0)
    {
      *bytes_written = 0;
      ssl_err = SSL_get_error(self->ssl->ssl, result);
      switch (ssl_err)
        {
        case SSL_ERROR_ZERO_RETURN:
          z_return(G_IO_STATUS_EOF);

        case SSL_ERROR_WANT_READ:
          if (self->what_if_called == DO_AS_USUAL)
            z_stream_set_cond(self->super.child, G_IO_IN, TRUE);
          self->what_if_called = CALL_WRITE_WHEN_READ;
          z_return(G_IO_STATUS_AGAIN);

        case SSL_ERROR_WANT_WRITE:
          z_return(G_IO_STATUS_AGAIN);

        case SSL_ERROR_SYSCALL:
          if (z_errno_is(EAGAIN) || z_errno_is(EINTR))
            z_return(G_IO_STATUS_AGAIN);
          /*LOG
            This message indicates that an OS level error occurred during the SSL write.
           */
          g_set_error(error, G_IO_CHANNEL_ERROR, g_io_channel_error_from_errno(errno), "%s", g_strerror(errno));
          z_return(G_IO_STATUS_ERROR);

        case SSL_ERROR_SSL:
        default:
          z_ssl_get_error_str(self->error, ERR_buflen);
          ERR_clear_error();

          /*LOG
            This message indicates that an internal SSL error occurred during the SSL write.
           */
          g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "%s", self->error);
          z_return(G_IO_STATUS_ERROR);
        }
    }

  if (self->what_if_called != DO_AS_USUAL)
    {
      z_stream_set_cond(self->super.child, G_IO_IN, FALSE);
      self->what_if_called = DO_AS_USUAL;
    }
  *bytes_written = result;
  ERR_clear_error();
  z_return(G_IO_STATUS_NORMAL);
}

static GIOStatus
z_stream_ssl_write_method(ZStream *s, const void *buf, gsize count, gsize *bytes_written, GError **error)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  gint result;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  if (self->shutdown)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR,
                  g_io_channel_error_from_errno (ENOTCONN),
                  "%s",
                  g_strerror (ENOTCONN));
      z_return(G_IO_STATUS_ERROR);
    }

  self->super.child->timeout = self->super.timeout;

  if (self->ssl)
    result = z_stream_ssl_write_method_impl(self, buf, count, bytes_written, error);
  else
    result = z_stream_write(self->super.child, buf, count, bytes_written, error);

  z_return(static_cast<GIOStatus>(result));
}

/**
 * Close SSL connection.
 *
 * @param[in]  s ZStreamSsl instance
 * @param      i HOW parameter (unused)
 * @param[out] error error value
 *
 * @returns GLib I/O status value
 **/
static GIOStatus
z_stream_ssl_shutdown_method(ZStream *s, int i, GError **error)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  GIOStatus res;
  
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  if (!self->shutdown)
    {
      gboolean nonblock;
      gint original_timeout;

      /* NOTE: we set the stream to blocking mode for the SSL shutdown
       * sequence with 1 second hard-wired timeout, even if it is in
       * nonblocking mode. 
       * 
       * This may not be the optimal solution especially if we want to use
       * the ZStreamSsl in nonblocking mode, but our callers are not really
       * prepared to handle a G_IO_STATUS_AGAIN status code from
       * z_stream_shutdown. 
       *
       * One possible solution is to start up a dedicated thread for
       * shutting down SSL connections. */

      
      nonblock = z_stream_get_nonblock(s);
      original_timeout = s->timeout;
      z_stream_set_timeout(s->child, 1000);
      
      
      z_stream_set_nonblock(s, FALSE);
      if (self->ssl && SSL_shutdown(self->ssl->ssl) == 0)
        {
          /* if SSL_shutdown returns 0 it means that we still need to
           * process the shutdown alert by the peer, and to do that we need
           * to call SSL_shutdown again */
          
          SSL_shutdown(self->ssl->ssl);
        }
      z_stream_set_nonblock(s, nonblock);
      z_stream_set_timeout(s, original_timeout);

      if (self->ssl)
        ERR_clear_error();
      self->shutdown = TRUE;
    }
  res = z_stream_shutdown(self->super.child, i, error);
  z_return(res);
}

/**
 * Process stream control calls on ZStreamSsl object.
 *
 * @param[in]      s ZStream instance
 * @param[in]      function function selector
 * @param[in, out] value parameter to function
 * @param[in]      vlen length of value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_ssl_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  ZStreamSsl *self G_GNUC_UNUSED = Z_CAST(s, ZStreamSsl);
  gboolean ret = FALSE;
  
  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_SET_CALLBACK_READ:
    case ZST_CTRL_SET_CALLBACK_WRITE:
    case ZST_CTRL_SET_CALLBACK_PRI:
      ret = z_stream_ctrl_method(s, function, value, vlen);
      break;

    case ZST_CTRL_SSL_SET_SESSION:
      if (vlen == sizeof(ZSSLSession *))
        {
          ZSSLSession *ssl = (ZSSLSession *) value;
          BIO *bio;

          self->ssl = z_ssl_session_ref(ssl);

          if (self->super.child)
            {
              bio = z_ssl_bio_new(self->super.child);
              SSL_set_bio(self->ssl->ssl, bio, bio);
            }
        }
      break;

    case ZST_CTRL_SSL_ADD_HANDSHAKE:
      if (vlen == sizeof(ZStreamSslHandshakeData))
        {
          ZStreamSslHandshakeData *data = (ZStreamSslHandshakeData *) value;
          ZStreamSslHandshakeData *our_data = g_new0(ZStreamSslHandshakeData, 1);

          our_data->handshake = data->handshake;
          our_data->destroy_function = data->destroy_function;

          self->handshakes = g_list_append(self->handshakes, our_data);
        }
      break;

    case ZST_CTRL_GET_BUFFERED_BYTES:
      /* we stop propagation here: this ctrl message queries the bytes
         buffered *above* the SSL layer. */
      if (vlen == sizeof(gsize))
        {
          ret = TRUE;
        }
      break;

    default:
      ret = z_stream_ctrl_method(s, ZST_CTRL_MSG_FORWARD | function, value, vlen);
      break;
    }
  z_return(ret);
}

static gboolean 
z_stream_ssl_watch_prepare(ZStream *s, GSource *src G_GNUC_UNUSED, gint *timeout)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);

  z_enter();
  *timeout = -1;
  if (s->want_read)
    {
      if (self->shutdown)
        {
          *timeout = 0;
          z_return(TRUE);
        }
      if (self->ssl)
        {
          if (SSL_pending(self->ssl->ssl))
            {
              *timeout = 0;
              z_return(TRUE);
            }
        }
      else
        {
          z_stream_set_cond(s->child, G_IO_IN, s->want_read);
          z_stream_set_cond(s->child, G_IO_PRI, s->want_pri);
          if (s->want_write)
            {
              z_stream_set_cond(s->child, G_IO_OUT, TRUE);
              z_return(TRUE);
            }
          else
            z_stream_set_cond(s->child, G_IO_OUT, FALSE);
        }
    }
  z_return(FALSE);

}

static gboolean 
z_stream_ssl_watch_check(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);

  z_enter();
  if (s->want_read)
    {
      if (self->ssl)
        {
          if (SSL_pending(self->ssl->ssl))
            z_return(TRUE);
        }
    }
  z_return(FALSE);
}

static gboolean 
z_stream_ssl_watch_dispatch(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  gboolean rc = TRUE;

  z_enter();
  if (s->want_read && rc)
    rc = self->super.read_cb(s, G_IO_IN, self->super.user_data_read);
  z_return(rc);
}

static void
z_stream_ssl_set_child(ZStream *s, ZStream *new_child)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  BIO *bio;

  z_stream_ref(s);
  
  Z_SUPER(s, ZStream)->set_child(s, new_child);

  if (self->super.child)
    {
      if (self->ssl)
        {
          bio = z_ssl_bio_new(self->super.child);
          SSL_set_bio(self->ssl->ssl, bio, bio);
        }

      z_stream_set_callback(self->super.child, G_IO_IN, z_stream_ssl_read_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(self->super.child, G_IO_OUT, z_stream_ssl_write_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(self->super.child, G_IO_PRI, z_stream_ssl_pri_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
    }

  z_stream_unref(s);
}

static GIOStatus
z_stream_ssl_close_method(ZStream *s, GError **error)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);
  GList *p;

  /* free associated handshakes */
  for (p = self->handshakes; p; p = p->next)
    {
      ZStreamSslHandshakeData *data = (ZStreamSslHandshakeData *) p->data;

      data->destroy_function(data->handshake);

      g_free(data);
    }

  g_list_free(self->handshakes);
  self->handshakes = NULL;

  return Z_SUPER(s, ZStream)->close(s, error);
}

ZStream *
z_stream_ssl_new(ZStream *child, ZSSLSession *ssl)
{
  ZStreamSsl *self;

  z_enter();
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamSsl), "", G_IO_IN|G_IO_OUT), ZStreamSsl);

  if (ssl)
    self->ssl = z_ssl_session_ref(ssl);
  z_stream_set_child(&self->super, child);
  z_return((ZStream *) self);
}

/** destructor */
static void
z_stream_ssl_free_method(ZObject *s)
{
  ZStreamSsl *self = Z_CAST(s, ZStreamSsl);

  z_enter();
  if (self->ssl)
    z_ssl_session_unref(self->ssl);
  ERR_clear_error();
  z_stream_free_method(s);
  z_return();
}


/**
 * ZStreamSsl virtual methods.
 **/
ZStreamFuncs z_stream_ssl_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_ssl_free_method,
  },
  z_stream_ssl_read_method,
  z_stream_ssl_write_method,
  NULL,
  z_stream_ssl_write_method,
  z_stream_ssl_shutdown_method,
  z_stream_ssl_close_method,
  z_stream_ssl_ctrl_method,
  NULL, /* attach_source */
  NULL, /* detach_source */
  z_stream_ssl_watch_prepare,
  z_stream_ssl_watch_check,
  z_stream_ssl_watch_dispatch,
  NULL,
  NULL,
  NULL,
  NULL,
  z_stream_ssl_set_child,
  NULL
};

/**
 * ZStreamSsl class descriptor
 **/
Z_CLASS_DEF(ZStreamSsl, ZStream, z_stream_ssl_funcs);
