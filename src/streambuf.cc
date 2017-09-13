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

#include <zorpll/streambuf.h>
#include <zorpll/stream.h>
#include <zorpll/log.h>
#include <zorpll/zorplib.h>

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <assert.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <sys/poll.h>
#endif

#include <zorpll/ssl.h>

/**
 * @file
 *
 * ZStreamBuf is a stream that makes it easier to use non-blocking streams
 * for writing. It basically supports a blocking like mode of operation,
 * e.g. write never returns G_IO_STATUS_AGAIN the data to be written is
 * stored in an internal buffer which is flushed to the child stream in an
 * independent process.
 *
 * Write errors are propagated to the user of the stream by storing the
 * possible error condition and returning it on the subsequent
 * z_stream_write() calls.
 *
 * Flow control is ensured using the poll interface, the stream always
 * accepts data with G_IO_STATUS_NORMAL but indicates writability only of
 * the buffer has free space available. This limitation is not hard, the
 * user of this class can freely bypass this limit by calling
 * z_stream_write() multiple times without checking for writability using
 * poll.  Therefore the size of the buffer specified is only an advisory
 * limit, hence the name buf_threshold.
 *
 * If you are generating mass amounts of data in a single shot you should
 * check whether the internal buffer is being overflown by using the
 * z_stream_buf_space_avail() function.
 *
 * The stream supports write operations from threads independent of the
 * flush thread, e.g. it implements locking on its internal buffer. Please
 * note however that streams do not support this mode of operation in general.
 **/


#define MAX_BUF_LEN 262144

/**
 * Structure representing ZStreamBuf state.
 **/
typedef struct _ZStreamBuf
{
  ZStream super;

  guint32 flags;
  gsize buf_threshold;
  
  gsize pending_pos;
  GError *flush_error;
  gsize current_size;
  GList *buffers;
  GMutex buffer_lock;
} ZStreamBuf;

extern ZClass ZStreamBuf__class;

static GIOStatus z_stream_write_packet_internal(ZStream *s, ZPktBuf *packet, GError **error);


/**
 * Internal function to check if there's available buffer space.
 * 
 * @param[in] self ZStreamBuf instance
 *
 * @returns TRUE if there's available buffer space.
 *
 * @note although current_size is protected by a lock it is not absolutely
 * required to lock it, as it does not cause problems to overcommit the
 * buffer, it only increases memory usage slightly.
 **/
static inline gboolean
z_stream_buf_space_avail_internal(ZStreamBuf *self)
{
  return self->current_size < self->buf_threshold;
}

/**
 * This function searches the stream stack for the topmost ZStreamBuf
 * instance and returns whether its buffer con
 *
 * @param[in] s ZStream stack top
 **/
gboolean 
z_stream_buf_space_avail(ZStream *s)
{
  ZStreamBuf *self;

  /* NOTE: we return TRUE when a flush error occurred as in that case the
   * output buffer will never be emptied. By returning true, the caller will
   * attempt another write which will return with the error condition.
   */
  self = Z_CAST(z_stream_search_stack(s, G_IO_OUT, Z_CLASS(ZStreamBuf)), ZStreamBuf);
  return self->flush_error || z_stream_buf_space_avail_internal(self);
}


/**
 * This function attempts to flush the internal ZStreamBuf buffer to the
 * child stream.
 *
 * @param[in] self ZStreamBuf instance
 *
 * It is automatically invoked when the child becomes writable
 * and provided Z_SBF_IMMED_FLUSH is specified after each write() operation.
 **/
static void
z_stream_buf_flush_internal(ZStreamBuf *self)
{
  ZPktBuf *packet;
  guint i = 10;
  gsize write_len;
  GIOStatus res = G_IO_STATUS_NORMAL;
  GError *local_error = NULL;

  z_enter();
  g_mutex_lock(&self->buffer_lock);
  while (self->buffers && i && res == G_IO_STATUS_NORMAL)
    {
      packet = (ZPktBuf *) self->buffers->data;
      
      res = z_stream_write(self->super.child, packet->data + self->pending_pos, packet->length - self->pending_pos, &write_len, &local_error);
      if (res == G_IO_STATUS_NORMAL)
        {
          self->pending_pos += write_len;
          if (self->pending_pos >= packet->length)
            {
              self->current_size -= packet->length;
              z_pktbuf_unref(packet);
              self->pending_pos = 0;
              self->buffers = g_list_delete_link(self->buffers, self->buffers);
            }
        }
      else if (res != G_IO_STATUS_AGAIN)
        {
          self->flush_error = local_error;
          local_error = NULL;
        }
      i--;
    }
  g_mutex_unlock(&self->buffer_lock);
  z_return();
}

/**
 * This function searches the stream stack for the topmost ZStreamBuf
 * instance and calls z_stream_buf_flush_internal()
 *
 * @param[in] s ZStream stack top
 **/
void
z_stream_buf_flush(ZStream *s)
{
  ZStreamBuf *self;

  self = Z_CAST(z_stream_search_stack(s, G_IO_OUT, Z_CLASS(ZStreamBuf)), ZStreamBuf);
  return z_stream_buf_flush_internal(self);
}


/**
 * This function is the z_stream_read() handler for ZStreamBuf, it basically
 * calls the child's read method.
 *
 * @param[in]  s ZStream instance
 * @param[in]  buf buffer to read data into
 * @param[in]  count size of buf
 * @param[out] bytes_read the number of bytes returned in buf
 * @param[out] error error state
 **/
static GIOStatus
z_stream_buf_read_method(ZStream *s, void *buf, gsize count, gsize *bytes_read, GError **error)
{
  ZStreamBuf *self = Z_CAST(s, ZStreamBuf);
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self->super.child->timeout = self->super.timeout;
  res = z_stream_read(self->super.child, buf, count, bytes_read, error);
  z_return(res);
}

/**
 * This function is the z_stream_write() handler for ZStreamBuf, it copies
 * the data from buf to the internal buffer.
 *
 * @param[in]  s ZStream instance
 * @param[in]  buf buffer to read data from
 * @param[in]  count size of buf
 * @param[out] bytes_written the number of bytes successfully written
 * @param[out] error error state
 *
 * It never returns G_IO_STATUS_AGAIN and basically always succeeds unless some error
 * occurred in a previous flush operation in which case it returns that error.
 **/
static GIOStatus
z_stream_buf_write_method(ZStream *s, const void *buf, gsize count, gsize *bytes_written, GError **error)
{
  ZStreamBuf *self = Z_CAST(s, ZStreamBuf);
  gboolean ret;
  GError *local_error = NULL;
  ZPktBuf *packet;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self->super.child->timeout = self->super.timeout;
  /* NOTE: we use internal functions to avoid logging the same data twice */
  packet = z_pktbuf_new();
  z_pktbuf_copy(packet, buf, count);
  ret = z_stream_write_packet_internal(s, packet, &local_error);
  if (ret == G_IO_STATUS_NORMAL)
    {
      *bytes_written = count;
      z_return(G_IO_STATUS_NORMAL);
    }
  if (local_error)
    g_propagate_error(error, local_error);
  z_return(static_cast<GIOStatus>(ret));
}

/** 
 * This function is the z_stream_shutdown handler for ZStreamBuf, it flushes
 * the output buffer while changing the child stream to nonblocking mode. 
 *
 * @param[in]  s ZStream instance
 * @param[in]  i shutdown mode
 * @param[out] error error state
 *
 * This means that this operation might block up to the timeout specified in
 * s->timeout.
 **/
static GIOStatus
z_stream_buf_shutdown_method(ZStream *s, int i, GError **error)
{
  ZStreamBuf *self = (ZStreamBuf *) s;
  GIOStatus res;
  
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  if (i == SHUT_WR || i == SHUT_RDWR)
    {
      self->super.child->timeout = self->super.timeout;
      z_stream_set_nonblock(self->super.child, FALSE);
      z_stream_buf_flush_internal(self);
    }
  res = z_stream_shutdown(self->super.child, i, error);
  z_return(res);
}

/**
 * @param[in]  s top of the stream stack
 * @param[in]  packet packet to be written (consumed)
 * @param[out] error error state if G_IO_STATUS_ERROR is returned
 *
 * This function appends a block to the internal buffer, consuming the
 * packet passed in packet. The error indication is not immediate however,
 * as the flushing process is independent from this operation. If flushing
 * fails the next write_packetuf operation fails with the error state stored by
 * flush.
 * 
 * @returns G_IO_STATUS_ERROR on failure, G_IO_STATUS_NORMAL on success
 **/
static GIOStatus
z_stream_write_packet_internal(ZStream *s, ZPktBuf *packet, GError **error)
{
  ZStreamBuf *self;
  
  z_enter();
  self = Z_CAST(z_stream_search_stack(s, G_IO_OUT, Z_CLASS(ZStreamBuf)), ZStreamBuf);
  g_mutex_lock(&self->buffer_lock);
  if (self->current_size > MAX_BUF_LEN)
    z_log(s->name, CORE_ERROR, 0, "Internal error, ZStreamBuf internal buffer became too large, continuing anyway; current_size='%zd'", self->current_size);
  if (self->flush_error)
    {
      if (error)
        *error = g_error_copy(self->flush_error);
      g_mutex_unlock(&self->buffer_lock);
      z_return(G_IO_STATUS_ERROR);
    }
  
  self->buffers = g_list_append(self->buffers, packet);
  self->current_size += packet->length;
  g_mutex_unlock(&self->buffer_lock);
  if (self->flags & Z_SBF_IMMED_FLUSH)
    z_stream_buf_flush_internal(self);
  z_return(G_IO_STATUS_NORMAL);
}

/**
 * This function appends a block to the internal buffer, consuming the
 * packet passed in packet.
 *
 * @param[in]  s top of the stream stack
 * @param[in]  packet packet to be written (consumed)
 * @param[out] error error state if G_IO_STATUS_ERROR is returned
 *
 * The error indication is not immediate however,
 * as the flushing process is independent from this operation. If flushing
 * fails the next write_packetuf operation fails with the error state stored by
 * flush.
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_stream_write_packet(ZStream *s, ZPktBuf *packet, GError **error)
{
  ZStreamBuf *self;
  GIOStatus res;

  self = Z_CAST(z_stream_search_stack(s, G_IO_OUT, Z_CLASS(ZStreamBuf)), ZStreamBuf);
  z_pktbuf_ref(packet);
  res = z_stream_write_packet_internal(s, packet, error);
  if (res == G_IO_STATUS_NORMAL)
    z_stream_data_dump(&self->super, G_IO_OUT, packet->data, packet->length);
  z_pktbuf_unref(packet);
  return res;
}

/**
 * This function appends a block to the internal buffer, possibly copying or
 * consuming the buffer passed in buf.
 *
 * @param[in]  s top of the stream stack
 * @param[in]  buf buffer to write (possibly consumed)
 * @param[in]  buflen size of buf
 * @param[in]  copy_buf whether to copy buf or it is a pointer which is freed by ZStreamBuf
 * @param[out] error error state if G_IO_STATUS_ERROR is returned
 *
 * The error indication is not
 * immediate however as the flushing process is independent from this
 * operation. If flushing fails the next write_buf operation fails with the
 * error state stored by flush.
 **/
GIOStatus
z_stream_write_buf(ZStream *s, void *buf, guint buflen, gboolean copy_buf, GError **error)
{
  ZStreamBuf *self;
  GIOStatus res;
  ZPktBuf *packet;

  self = Z_CAST(z_stream_search_stack(s, G_IO_OUT, Z_CLASS(ZStreamBuf)), ZStreamBuf);

  /* copying is done outside the protection of the lock */
  packet = z_pktbuf_new();
  if (copy_buf)
    z_pktbuf_copy(packet, buf, buflen);
  else
    z_pktbuf_relocate(packet, buf, buflen, FALSE);

  z_pktbuf_ref(packet);      
  res = z_stream_write_packet_internal(s, packet, error);

  if (res == G_IO_STATUS_NORMAL)
    z_stream_data_dump(&self->super, G_IO_OUT, packet->data, packet->length);
  z_pktbuf_unref(packet);
  return res;
}

/**
 * Process stream control calls.
 *
 * @param[in]      s ZStream instance
 * @param[in]      function stream control function to perform
 * @param[in, out] value value associated with function
 * @param[in]      vlen size of the buffer pointed to by value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_buf_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  ZStreamBuf *self G_GNUC_UNUSED = Z_CAST(s, ZStreamBuf);
  gboolean ret;
  
  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_SET_CALLBACK_READ:
    case ZST_CTRL_SET_CALLBACK_WRITE:
    case ZST_CTRL_SET_CALLBACK_PRI:
    case ZST_CTRL_SET_COND_READ:
    case ZST_CTRL_SET_COND_WRITE:
    case ZST_CTRL_SET_COND_PRI:
      ret = z_stream_ctrl_method(s, function, value, vlen);
      break;

    default:
      ret = z_stream_ctrl_method(s, ZST_CTRL_MSG_FORWARD | function, value, vlen);
      break;
    }
  z_return(ret);
}

/* callbacks specified to the child stream */

/**
 * This function is called whenever our child stream indicates readability. 
 *
 * @param     s child ZStream instance (unused)
 * @param[in] poll_cond condition that triggered this callback
 * @param[in] user_data ZStream instance as a gpointer
 *
 * It basically calls our own callback as we rely on our child to trigger read/pri
 * callbacks.
 **/
static gboolean
z_stream_buf_read_callback(ZStream *s G_GNUC_UNUSED, GIOCondition poll_cond, gpointer user_data)
{
  ZStreamBuf *self = Z_CAST(user_data, ZStreamBuf);
  gboolean rc;

  z_enter();
  rc = (*self->super.read_cb)(&self->super, poll_cond, self->super.user_data_read);
  z_return(rc);
}

/**
 * This function is called whenever our child stream indicates writability.
 *
 * @param     s child ZStream instance (unused)
 * @param     poll_cond condition that triggered this callback (unused)
 * @param[in] user_data ZStream instance as a gpointer
 *
 * It basically calls z_stream_buf_flush_internal() to write blocks in our buffer to
 * s->child.
 * 
 * @returns always TRUE
 **/
static gboolean
z_stream_buf_write_callback(ZStream *s G_GNUC_UNUSED, GIOCondition poll_cond G_GNUC_UNUSED, gpointer user_data)
{
  ZStreamBuf *self = Z_CAST(user_data, ZStreamBuf);
  
  z_enter();
  z_stream_buf_flush_internal(self);
  z_return(TRUE);
}

/**
 * This function is called whenever our child stream indicates that priority
 * data became available.
 *
 * @param     s child ZStream instance (unused)
 * @param[in] poll_cond condition that triggered this callback
 * @param[in] user_data ZStream instance as a gpointer
 *
 * It basically calls our own callback as we rely on
 * our child to trigger read/pri callbacks.
 **/
static gboolean
z_stream_buf_pri_callback(ZStream *s G_GNUC_UNUSED, GIOCondition poll_cond, gpointer user_data)
{
  ZStreamBuf *self = Z_CAST(user_data, ZStreamBuf);
  gboolean rc;

  z_enter();
  rc = (*self->super.pri_cb)(&self->super, poll_cond, self->super.user_data_pri);
  z_return(rc);
}

/* source helper functions */

/**
 * This function is called to prepare for a poll iteration.
 *
 * @param[in]  s ZStream instance
 * @param      src GSource associated with s (unused)
 * @param[out] timeout timeout value for this poll iteration
 *
 * It checks our internal state and prepares the watch conditions for s->child, it also
 * checks whether we are currently writable and indicates the immediate
 * availability of an event by returning TRUE.
 *
 * @returns TRUE if an event is immediately available
 **/
static gboolean 
z_stream_buf_watch_prepare(ZStream *s, GSource *src G_GNUC_UNUSED, gint *timeout)
{
  ZStreamBuf *self = Z_CAST(s, ZStreamBuf);
  gboolean ret = FALSE;
  
  *timeout = -1;
  z_stream_set_cond(s->child, G_IO_IN, s->want_read);
  z_stream_set_cond(s->child, G_IO_PRI, s->want_pri);
  z_stream_set_cond(s->child, G_IO_OUT, !!self->current_size && self->flush_error == NULL);
  
  if (s->want_write && z_stream_buf_space_avail_internal(self))
    ret = TRUE;
    
  return ret;
}

/**
 * This function is called after a poll iteration.
 *
 * @param[in] s ZStream instance
 * @param     src GSource associated with s (unused)
 *
 * It checks whether space is available in our buffer and our user wants notification about
 * writability. If it is true an event is trigggered by returning TRUE.
 *
 * @returns TRUE if an event needs to be triggered, see above
 **/
static gboolean 
z_stream_buf_watch_check(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamBuf *self = Z_CAST(s, ZStreamBuf);
  gboolean ret = FALSE;
  
  if (s->want_write && z_stream_buf_space_avail_internal(self))
    ret = TRUE;
  return ret;
}

/**
 * This function is called after a poll iteration if check returned TRUE.
 *
 * @param[in] s ZStream instance
 * @param     src GSource associated with s (unused)
 *
 * It basically calls our write callback as we don't trigger any other event
 * ourselves, but rely on our child stream to trigger those for us.
 **/
static gboolean 
z_stream_buf_watch_dispatch(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  gboolean ret = TRUE;
  
  if (s->want_write && ret)
    ret = s->write_cb(s, G_IO_OUT, s->user_data_read);
    
  return ret;
}

/**
 * This function is called to change our child stream.
 *
 * @param[in] s ZStream instance
 * @param[in] new_child new child stream
 *
 * It basically sets up appropriate callbacks and switches new_child to nonblocking mode.
 **/
static void
z_stream_buf_set_child(ZStream *s, ZStream *new_child)
{
  z_stream_ref(s);

  Z_SUPER(s, ZStream)->set_child(s, new_child);

  if (new_child)
    {
      z_stream_set_callback(new_child, G_IO_IN, z_stream_buf_read_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_OUT, z_stream_buf_write_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_PRI, z_stream_buf_pri_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_nonblock(new_child, TRUE);
    }

  z_stream_unref(s);
}

/**
 * This function constructs a new ZStreamBuf instance.
 *
 * @param[in] child child stream
 * @param[in] buf_threshold buffer threshold
 * @param[in] flags one of Z_SBF_* flags
 *
 * @returns the new ZStreamBuf instance
 **/
ZStream *
z_stream_buf_new(ZStream *child, gsize buf_threshold, guint32 flags)
{
  ZStreamBuf *self;

  z_enter();
  g_return_val_if_fail(buf_threshold <= MAX_BUF_LEN, NULL);
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamBuf), child ? child->name : "", G_IO_OUT), ZStreamBuf);
  self->buf_threshold = buf_threshold;
  self->flags = flags;
  g_mutex_init(&self->buffer_lock);
  z_stream_set_child(&self->super, child);
  z_return((ZStream *) self);
}

/**
 * Frees dynamically allocated variables associated with s.
 *
 * @param[in] s ZStream instance
 **/
static void
z_stream_buf_free_method(ZObject *s)
{
  ZStreamBuf *self = Z_CAST(s, ZStreamBuf);

  z_enter();
  while (self->buffers)
    {
      ZPktBuf *packet = (ZPktBuf *) self->buffers->data;
      z_pktbuf_unref(packet);
      self->buffers = g_list_delete_link(self->buffers, self->buffers);
    }
  if (self->flush_error)
    g_error_free(self->flush_error);
  g_mutex_clear(&self->buffer_lock);
  z_stream_free_method(s);
  z_return();
}


/**
 * ZStreamBuf virtual methods.
 **/
ZStreamFuncs z_stream_buf_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_buf_free_method,
  },
  z_stream_buf_read_method,
  z_stream_buf_write_method,
  NULL,
  NULL,
  z_stream_buf_shutdown_method,
  NULL, /* close */
  z_stream_buf_ctrl_method,
  NULL, /* attach_source */
  NULL, /* detach_source */
  z_stream_buf_watch_prepare,
  z_stream_buf_watch_check,
  z_stream_buf_watch_dispatch,
  NULL,
  NULL,
  NULL,
  NULL,
  z_stream_buf_set_child,
  NULL
};

/**
 * ZStreamBuf class descriptor.
 **/
Z_CLASS_DEF(ZStreamBuf, ZStream, z_stream_buf_funcs);
