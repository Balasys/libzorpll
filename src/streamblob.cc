/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/streamblob.h>

#include <zorpll/log.h>
#include <zorpll/error.h>

#include <string.h>
#include <sys/types.h>

/**
 * A ZStream built around a ZBlob referred to by the instance.
 **/
typedef struct _ZStreamBlob
{
  ZStream       super;

  gint64        pos;
  ZBlob         *blob;
  GIOCondition  poll_cond;
} ZStreamBlob;

extern ZClass ZStreamBlob__class;

static gboolean
z_stream_blob_watch_prepare(ZStream *s, GSource *src G_GNUC_UNUSED, gint *timeout)
{
  ZStreamBlob   *self = Z_CAST(s, ZStreamBlob);
  gboolean      res;

  z_enter();
  if (timeout)
    *timeout = -1;
  res = FALSE;
  self->poll_cond = static_cast<GIOCondition>(0);
  if (self->super.want_read)
    {
      self->poll_cond = static_cast<GIOCondition>(self->poll_cond | G_IO_IN);
      res = TRUE;
    }
  if (self->super.want_write)
    {
      self->poll_cond = static_cast<GIOCondition>(self->poll_cond | G_IO_OUT);
      res = TRUE;
    }
  z_return(res);
}

static gboolean
z_stream_blob_watch_check(ZStream *s, GSource *src)
{
  gboolean res;

  /** @note Now (should be reconsidered!) it's the same as watch_prepare */
  z_enter();
  res = z_stream_blob_watch_prepare(s, src, NULL);
  z_return(res);
}

static gboolean
z_stream_blob_watch_dispatch(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamBlob *self = Z_CAST(s, ZStreamBlob);
  gboolean rc = TRUE;

  z_enter();
  if (self->super.want_read && (self->poll_cond & G_IO_IN) && rc)
    {
      if (self->super.read_cb)
        {
          rc = (*self->super.read_cb)(s, self->poll_cond, self->super.user_data_read);
        }
      else
        {
          /*LOG
            This message indicates an internal error, read event occurred, but no read
            callback is set. Please report this event to the BalaSys Development Team (devel@balasys.hu).
           */
          z_log(self->super.name, CORE_ERROR, 3, "Internal error, no read callback is set;");
        }
    }

  if (self->super.want_write && (self->poll_cond & G_IO_OUT) && rc)
    {
      if (self->super.write_cb)
        {
          rc &= (*self->super.write_cb)(s, self->poll_cond, self->super.user_data_write);
        }
      else
        {
          /*LOG
            This message indicates an internal error, write event occurred, but no write
            callback is set. Please report this event to the BalaSys Development Team (devel@balasys.hu).
           */
          z_log(self->super.name, CORE_ERROR, 3, "Internal error, no write callback is set;");
        }
    }
  z_return(rc);
}

/**
 * This method is called to read bytes from a ZStreamBlob.
 *
 * @param[in]  stream ZStreamBlob instance
 * @param[in]  buf destination buffer
 * @param[in]  count size of buf
 * @param[out] bytes_read number of bytes read
 * @param[out] error error value
 *
 * This function reads from the ZBlob referenced by the
 * ZStreamBlob and returns it in buf.
 *
 * @returns GLib I/O status
 **/
static GIOStatus
z_stream_blob_read_method(ZStream *stream,
                          void *buf,
                          gsize count,
                          gsize *bytes_read,
                          GError **error)
{
  ZStreamBlob *self = Z_CAST(stream, ZStreamBlob);

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (count == 0)
    {
      *bytes_read = 0;
    }
  else
    {
      if (self->pos >= self->blob->size)
        {
          *bytes_read = 0;
          z_return(G_IO_STATUS_EOF);
        }

      *bytes_read = z_blob_get_copy(self->blob, self->pos, static_cast<gchar *>(buf), count, self->super.timeout);
      if (*bytes_read == 0)
        {
          g_set_error (error, G_IO_CHANNEL_ERROR,
                       G_IO_CHANNEL_ERROR_FAILED,
                       "Channel read timed out");
          z_return(G_IO_STATUS_ERROR);
        }

      self->pos += *bytes_read;
    }
  z_return(G_IO_STATUS_NORMAL);
}

/**
 * This method is called to write bytes to a ZStreamBlob.
 *
 * @param[in]  stream ZStream instance
 * @param[in]  buf source buffer
 * @param[in]  count size of buf
 * @param[out] bytes_written number of bytes written
 * @param[out] error error value
 *
 * This function writes from buf to the ZBlob referenced by the
 * ZStreamBlob.
 *
 * @returns GLib I/O status
 **/
static GIOStatus
z_stream_blob_write_method(ZStream *stream,
                           const void *buf,
                           gsize count,
                           gsize *bytes_written,
                           GError **error)
{
  ZStreamBlob *self = Z_CAST(stream, ZStreamBlob);

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (count == 0)
    {
      *bytes_written = 0;
    }
  else
    {
      *bytes_written = z_blob_add_copy(self->blob, self->pos, static_cast<const gchar *>(buf), count, self->super.timeout);
      if (*bytes_written == 0)
        {
          g_set_error (error, G_IO_CHANNEL_ERROR,
                       G_IO_CHANNEL_ERROR_FAILED,
                       "Channel write timed out");
          z_return(G_IO_STATUS_ERROR);
        }
      self->pos += *bytes_written;
    }
  z_return(G_IO_STATUS_NORMAL);
}

/**
 * Process stream control calls.
 *
 * @param[in] s ZStream instance
 * @param[in] function function selector
 * @param[in] value parameter to function
 * @param[in] vlen length of value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_blob_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  ZStreamBlob *self = Z_CAST(s, ZStreamBlob);

  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_SET_NONBLOCK:
      if (vlen == sizeof(gboolean))
        {
          self->super.timeout = *((gboolean *)value) ? 0 : -1;
          z_return(TRUE);
        }
      /*LOG
        This message indicates that an internal error occurred, during setting NONBLOCK mode
        on a stream, because the size of the parameter is wrong. Please report this event to
        the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for setting NONBLOCK mode; size='%d'", vlen);
      break;

    case ZST_CTRL_GET_NONBLOCK:
      if (vlen == sizeof(gboolean))
        {
          *((gboolean *) value) = (self->super.timeout ==0);
          z_return(TRUE);
        }
      /*LOG
        This message indicates that an internal error occurred, during getting NONBLOCK mode status
        on a stream, because the size of the parameter is wrong. Please report this event to
        the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for getting the NONBLOCK mode; size='%d'", vlen);
      break;

    default:
      if (z_stream_ctrl_method(s, function, value, vlen))
        z_return(TRUE);
      /*LOG
        This message indicates that an internal error occurred, because an invalid control was called.
        Please report this event to the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, unknown stream ctrl; ctrl='%d'", ZST_CTRL_MSG(function));
      break;
    }
  z_return(FALSE);
}

/**
 * Allocate and initialize a ZStreamBlob instance with the given blob and name.
 *
 * @param[in] blob blob to create the stream around
 * @param[in] name name to identify the stream in the logs
 *
 * @returns The new stream instance
 */
ZStream *
z_stream_blob_new(ZBlob *blob, const gchar *name)
{
  ZStreamBlob *self;

  z_enter();
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamBlob), name, G_IO_IN|G_IO_OUT), ZStreamBlob);
  self->blob = z_blob_ref(blob);
  self->pos = 0;
  self->poll_cond = static_cast<GIOCondition>(0);
  z_return(&self->super);
}

/** destructor */
static void
z_stream_blob_free_method(ZObject *s)
{
  ZStreamBlob *self = Z_CAST(s, ZStreamBlob);

  z_enter();
  z_blob_unref(self->blob);
  z_stream_free_method(s);
  z_return();
}

/**
 * ZStreamBlob virtual methods.
 **/
static ZStreamFuncs 
z_stream_blob_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_blob_free_method   /* ok */
  },
  z_stream_blob_read_method,   /* ok */
  z_stream_blob_write_method,   /* ok */
  NULL,
  NULL,
  NULL,
  NULL,                        /* ok */
  z_stream_blob_ctrl_method,   /* ok */

  NULL, /* attach_source */
  NULL, /* detach_source */
  z_stream_blob_watch_prepare,
  z_stream_blob_watch_check,
  z_stream_blob_watch_dispatch,
  NULL,

  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/**
 * ZStreamBlob class descriptor.
 **/
Z_CLASS_DEF(ZStreamBlob, ZStream, z_stream_blob_funcs);
