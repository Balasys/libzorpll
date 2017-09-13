/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/stream.h>
#include <zorpll/streamline.h>
#include <zorpll/log.h>

#include <glib.h>

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

#define ZRL_SAVED_FLAGS_MASK    0x0000FFFF

#define ZRL_IGNORE_TILL_EOL     0x00010000
#define ZRL_LINE_AVAIL_SET      0x00020000
#define ZRL_LINE_AVAIL          0x00040000
#define ZRL_ERROR		0x00080000
#define ZRL_EOF                 0x00100000

extern ZClass ZStreamLine__class;

/**
 * ZStream-derived class for line-based I/O.
 **/
typedef struct _ZStreamLine
{
  ZStream super;

  guint flags;
  gchar *buffer;
  gsize bufsize, pos, end, oldpos;
  GIOCondition child_cond;
            
} ZStreamLine;

/**
 * ZStreamLine extra context data.
 **/
typedef struct _ZStreamLineExtra
{
  guint flags;
} ZStreamLineExtra;

/**
 * Check if a line can be read from the buffer.
 *
 * @param[in] self ZStreamLine instance
 *
 * @returns whether a line can be read
 **/
static inline gboolean
z_stream_line_have_line(ZStreamLine *self)
{
  z_enter();
  if (!(self->flags & ZRL_LINE_AVAIL_SET))
    {
      gsize avail = self->end - self->pos;
      gchar *eol;

      eol = static_cast<gchar *>(memchr(self->buffer + self->pos, self->flags & ZRL_EOL_NUL ? '\0' : '\n', avail));
      self->flags |= ZRL_LINE_AVAIL_SET;
      if (eol)
        self->flags |= ZRL_LINE_AVAIL;
      else
        self->flags &= ~ZRL_LINE_AVAIL;
    }
  z_return(!!(self->flags & ZRL_LINE_AVAIL));
}

/**
 * Check if buffer is empty.
 *
 * @param[in] self ZStreamLine instance
 *
 * @returns whether buffer is empty
 **/
static inline gboolean
z_stream_line_buf_empty(ZStreamLine *self)
{
  return self->pos == self->end;
}

/**
 * Internal function used by z_stream_line_get_internal()
 * to read a line from the buffer.
 *
 * @param[in]  self ZStreamLine instance
 * @param[out] line pointer to the line will be returned here
 * @param[out] length length of the line will be returned here
 * @param[out] error error value
 *
 * The string line points to won't be null-terminated.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_line_get_from_buf(ZStreamLine *self, gchar **line, gsize *length, GError **error)
{
  gsize avail = self->end - self->pos;
  gchar *eol = static_cast<gchar *>(memchr(self->buffer + self->pos, self->flags & ZRL_EOL_NUL ? '\0' : '\n', avail));
  gchar *nul;
  gint eol_len = 0;

  z_enter();

  /* if we encountered eof in the input stream, return all the buffer as a line */
  if (self->flags & ZRL_EOF)
    eol = self->buffer + self->end - 1;

  if (eol)
    {
      *length = eol - (self->buffer + self->pos) + 1;
      *line = self->buffer + self->pos;
      self->oldpos = self->pos;
      self->pos += *length;

      if (!(self->flags & ZRL_EOL_NUL))
        {
          nul = static_cast<gchar *>(memchr(*line, '\0', *length));
          if (nul)
            {
              if (!(self->flags & ZRL_NUL_NONFATAL))
                {
		  /*LOG
		    This message indicates that an invalid NUL character was found in the line, but the
		    policy prohibits it.
		   */
		  g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Invalid line, embedded NUL character found, buffer=[%.*s]", (gint) *length, *line);
                  z_return(G_IO_STATUS_ERROR);
                }
            }
        }

      if (!(self->flags & ZRL_EOF))
        {
          if ((self->flags & ZRL_EOL_NL) || (self->flags & ZRL_EOL_NUL))
            {
              (*length)--;
              eol_len++;
            }
          else if (self->flags & ZRL_EOL_CRLF)
            {
              (*length)--;
              eol_len++;
              if (eol - self->buffer >= 1 && *(eol - 1) == '\r')
                {
                  (*length)--;
                  eol_len++;
                }
              else if (self->flags & ZRL_EOL_FATAL)
                {
                  /*LOG
                    This message indicates that the CRLF sequence was invalid, because no LF character was
                    found before the CR character, but the policy requires it.
                    */
                  g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Invalid line, bad CRLF sequence, buffer=[%.*s]", (gint) *length, *line);
                  z_return(G_IO_STATUS_ERROR);
                }
            }
          if (self->flags & ZRL_RETURN_EOL)
            (*length) += eol_len;
        }
      z_return(G_IO_STATUS_NORMAL);
    }
  else if (self->pos)
    {
      *length = 0;
      memmove(self->buffer, self->buffer + self->pos, avail);
      self->end = avail;
      self->pos = 0;
      self->oldpos = 0;
    }
  z_return(G_IO_STATUS_AGAIN);
}

/**
 * Internal function used by z_stream_line_get() and z_stream_line_get_copy()
 * to get a line, reading to fill the buffer as necessary.
 *
 * @param[in]  self ZStreamLine instance
 * @param[out] line pointer to the line will be returned here
 * @param[out] length length of the line will be returned here
 * @param[out] error error value
 *
 * Also handles some flags like ZRL_IGNORE_TILL_EOL.
 *
 * The string line points to won't be null-terminated.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_line_get_internal(ZStreamLine *self, gchar **line, gsize *length, GError **error)
{
  gsize avail, bytes_read;
  GError *local_error = NULL;
  GIOStatus rc;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);  
  
  if (self->flags & ZRL_ERROR)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Previously stored error condition");
      z_return(G_IO_STATUS_ERROR);
    }
  
  if (self->flags & ZRL_EOF)
    z_return(G_IO_STATUS_EOF);

  self->child_cond = static_cast<GIOCondition>(0);
  self->flags &= ~ZRL_LINE_AVAIL_SET;
  if (self->end != self->pos)
    {
      /* we have something, try to return it */
      rc = z_stream_line_get_from_buf(self, line, length, &local_error);
      if (rc == G_IO_STATUS_NORMAL)
      	{
	  self->super.bytes_recvd += *length;
          z_return(rc);
	}

      if (rc == G_IO_STATUS_ERROR)
        {
          /* no need to log, we sent the exact error reason in get_from_buf() */
          if (local_error)
            g_propagate_error(error, local_error);
          self->flags |= ZRL_ERROR;
          z_return(rc);
        }
      /* the available data is now at the beginning of our buffer */
    }
  else
    {
      self->pos = self->end = self->oldpos = 0;
    }

  *length = 0;
  *line = NULL;

  while (1)
    {
      avail = self->bufsize - self->end;
      if (!avail)
        {
          /*
           * this means that there's no space in the buffer, and no eol could
           * be found
           */
          if (self->flags & ZRL_IGNORE_TILL_EOL)
            {
              self->pos = self->end = self->oldpos = 0;
              avail = self->bufsize;
            }
          else if (self->flags & ZRL_TRUNCATE)
            {
              *line = self->buffer;
              *length = self->bufsize;
              self->super.bytes_recvd += *length;
              self->pos = self->end = self->oldpos = 0;
              self->flags |= ZRL_IGNORE_TILL_EOL;
              z_return(G_IO_STATUS_NORMAL);
            }
          else if (self->flags & ZRL_SPLIT)
            {
              /**
	       * @todo FIXME: this hack violates the standard GIOStatus model, as
               * it returns G_IO_STATUS_AGAIN while consuming some of the
               * input data, callers detect this case by checking whether
               * line_len is not 0.
               **/
              *line = self->buffer;
              *length = self->bufsize;
              self->super.bytes_recvd += *length;
              self->pos = self->end = self->oldpos = 0;
              z_return(G_IO_STATUS_AGAIN);
            }
          else
            {
              /*LOG
                This message is sent when the proxy reading too long
                input line. This may be caused by a cracking attempt. But if
                not try to increase max_line_length.
               */
              g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Line too long, buffer=[%.*s], max_line_length=[%d]", (gint) self->bufsize, self->buffer, (gint) self->bufsize);
              
              *line = NULL;
              *length = 0;
              self->flags |= ZRL_ERROR;
              z_return(G_IO_STATUS_ERROR);
            }
        }

      self->super.child->timeout = self->super.timeout;
      rc = z_stream_read(self->super.child, self->buffer + self->end, avail, &bytes_read, &local_error);
      switch (rc)
        {
        case G_IO_STATUS_EOF:
          if ((self->flags & ZRL_EOF) || (self->pos == self->end))
            z_return(G_IO_STATUS_EOF);
          self->flags |= ZRL_EOF;
          bytes_read = 0;
          /* intentional fallthrough */

        case G_IO_STATUS_NORMAL:
          self->end += bytes_read;
maybe_another_line:
          rc = z_stream_line_get_from_buf(self, line, length, &local_error);
          switch (rc)
            {
            case G_IO_STATUS_NORMAL:
              if (self->flags & ZRL_IGNORE_TILL_EOL)
                {
                  self->flags &= ~ZRL_IGNORE_TILL_EOL;
                  goto maybe_another_line;
                }
              else
                {
                  self->super.bytes_recvd += *length;
                  z_return(rc);
                }
              
            case G_IO_STATUS_AGAIN:
              if (self->flags & ZRL_SINGLE_READ)
                {
                  *line = NULL;
                  *length = 0;
                  z_return(rc);
                }
              break;
              
            default:
              if (local_error)
                g_propagate_error(error, local_error);
              *line = NULL;
              *length = 0;
              z_return(rc);
            }
          break;
          
        case G_IO_STATUS_AGAIN:
          *line = NULL;
          *length = 0;
          z_return(G_IO_STATUS_AGAIN);
          
        default:
          if (local_error)
            g_propagate_error(error, local_error);
          self->flags |= ZRL_ERROR;
          z_return(G_IO_STATUS_ERROR);
        }
    }
}

/**
 * Read a line from a ZStream and give a pointer to it in the buffer.
 *
 * @param[in]  stream ZStream instance
 * @param[out] line pointer to the line will be returned here
 * @param[out] length length of the line will be returned here
 * @param[out] error error value
 *
 * The first child of stream that is of class ZStreamLine will be sought
 * and the line will be requested from that instance.
 *
 * The string line points to won't be null-terminated.
 *
 * @returns GIOStatus value
 **/
GIOStatus
z_stream_line_get(ZStream *stream, gchar **line, gsize *length, GError **error)
{
  ZStreamLine *self;
  GIOStatus res;
  GError *local_error = NULL;

  self = Z_CAST(z_stream_search_stack(stream, G_IO_IN, Z_CLASS(ZStreamLine)), ZStreamLine);
  res = z_stream_line_get_internal(self, line, length, &local_error);
  
  if (local_error)
    {
      z_log(self->super.name, CORE_ERROR, 3, "Error while fetching line; error='%s'", local_error->message);
      g_propagate_error(error, local_error);
    }
          
  if (res == G_IO_STATUS_NORMAL)
    z_stream_data_dump(&self->super, G_IO_IN, *line, *length);

  return res;
}


/**
 * Read a line from a ZStream and copy it into the buffer given.
 *
 * @param[in]      s ZStream instance
 * @param[in]      line buffer where the line will be copied
 * @param[in, out] length in: the size of the buffer provided in line; out: the length of the line returned
 * @param[out]     error error value
 *
 * The first child of stream that is of class ZStreamLine will be sought
 * and the line will be requested from that instance.
 *
 * The string line points to won't be null-terminated.
 *
 * @returns GIOStatus value
 **/
GIOStatus
z_stream_line_get_copy(ZStream *s, gchar *line, gsize *length, GError **error)
{
  gchar *b;
  gsize len;
  GIOStatus res;
  ZStreamLine *self;
  GError *local_error = NULL;

  z_enter();
  
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self = Z_CAST(z_stream_search_stack(s, G_IO_IN, Z_CLASS(ZStreamLine)), ZStreamLine);

  if (*length == 0)
    z_return(G_IO_STATUS_AGAIN);
    
  res = z_stream_line_get_internal(self, &b, &len, &local_error);
  
  if (res == G_IO_STATUS_NORMAL || (res == G_IO_STATUS_AGAIN && len > 0))
    {
      if (len > *length)
        {
          /**
	   * @todo FIXME: this uses the non-standard trick to return the part of
           * the line which fits the result buffer, e.g. it returns
           * G_IO_STATUS_AGAIN with line_len set to a non-zero value while
           * consuming the returned amount from the buffer.  this should be
           * cleaned up, but currently it is the less risky solution.
	   **/
          if (self->flags & ZRL_SPLIT)
            {
              if (self->end == 0)
                {
                  self->pos = *length;
                  self->end = len;
                }
              else
                {
                  self->pos = self->oldpos + *length;
                }
              len = *length;
              res = G_IO_STATUS_AGAIN;
            }
          else
            {
	      /*LOG
	        This message indicates that the line buffer is too small to hold the
		whole line. It is likely caused by some max size limit or by some
		standard default.
	       */
              g_set_error(&local_error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Line buffer too small, buffer=[%.*s]", (gint) len, b);
              z_return(G_IO_STATUS_ERROR);
            }
        }
      *length = len;
      memcpy(line, b, len);
      z_stream_data_dump(s, G_IO_IN, line, len);
    }
  else
    {
      *length = 0;
    }
  if (local_error)
    {
      z_log(self->super.name, CORE_ERROR, 3, "Error while fetching line; error='%s'", local_error->message);
      g_propagate_error(error, local_error);
    }
  z_return(res);
}

/**
 * Unget a packet into a ZStreamLine -- write its buffer to the beginning of the buffer of
 * the ZStreamLine and consume (unref) the packet.
 *
 * @param[in]  s ZStreamLine instance
 * @param[in]  packet ZPktBuf instance, will be consumed
 * @param[out] error error value
 *
 * It won't unget the packet if there isn't enough space in the buffer. In that
 * case the packet won't be consumed.
 *
 * @returns TRUE on success
 **/
gboolean
z_stream_line_unget_packet_method(ZStream *s, ZPktBuf *packet, GError **error)
{
  ZStreamLine *self = Z_CAST(s, ZStreamLine);
  gsize avail_before, avail_after;
  GError *local_error = NULL;
  gboolean res = FALSE;
                                                                                                                                          
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), FALSE);

  avail_before = self->pos;
  avail_after = self->bufsize - self->end;
  if (avail_before + avail_after > packet->length)
    {
      if (avail_before > packet->length)
        {
          memmove(&self->buffer[self->pos - packet->length], packet->data, packet->length);
          self->pos -= packet->length;
        }
      else
        {
          memmove(&self->buffer[packet->length], &self->buffer[self->pos], self->end - self->pos);
          memmove(self->buffer, packet->data, packet->length);
          self->end = self->end - self->pos + packet->length;
          self->pos = 0;
        }
      z_pktbuf_unref(packet);
      res = TRUE;
    }
  else
    {
      g_set_error(&local_error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Unget blob does not fit into ZStreamLine buffer");
    }

  if (local_error)
    {
      z_log(self->super.name, CORE_ERROR, 3, "Internal error while ungetting data into ZStreamLine buffer; error='%s'", local_error->message);
      g_propagate_error(error, local_error);
    }
  z_return(res);
}

/* I/O callbacks for stacked stream */

static gboolean
z_stream_line_read_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond G_GNUC_UNUSED, gpointer s)
{
  ZStreamLine *self = (ZStreamLine *) s;

  z_enter();
  self->child_cond = static_cast<GIOCondition>(self->child_cond | G_IO_IN);
  z_return(TRUE);
}

static gboolean
z_stream_line_pri_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond G_GNUC_UNUSED, gpointer s)
{
  ZStreamLine *self = (ZStreamLine *) s;

  z_enter();
  self->child_cond = static_cast<GIOCondition>(self->child_cond | G_IO_PRI);
  z_return(TRUE);
}

static gboolean
z_stream_line_write_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond G_GNUC_UNUSED, gpointer s)
{
  ZStreamLine *self = (ZStreamLine *) s;
  gboolean rc;

  z_enter();
  rc = (*self->super.write_cb)(static_cast<_ZStream *>(s), poll_cond, self->super.user_data_write);
  z_return(rc);
}

/* virtual methods */

/**
 * Read from ZStreamLine (not line-based).
 *
 * @param[in]  stream ZStreamLine instance
 * @param[in]  buf buffer to return data in
 * @param[in]  count number of bytes to read
 * @param[out] bytes_read number of bytes actually read will be returned here
 * @param[out] error error value
 *
 * Reads from own buffer if there's data to read or from child if there isn't.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_line_read_method(ZStream *stream,
                          void   *buf,
                          gsize   count,
                          gsize   *bytes_read,
                          GError  **error)
{
  ZStreamLine *self = (ZStreamLine *) stream;
  GIOStatus res;
  gsize avail = self->end - self->pos;
  
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  
  if (avail)
    {
      *bytes_read = MIN(count, avail);
      memmove(buf, self->buffer + self->pos, *bytes_read);
      
      self->oldpos = self->pos;
      self->pos += *bytes_read;

      if (self->pos == self->end)
        self->pos = self->end = 0;
        
      self->flags &= ~ZRL_LINE_AVAIL_SET;

      res = G_IO_STATUS_NORMAL;
      z_stream_data_dump(&self->super, G_IO_IN, buf, *bytes_read);
    }
  else
    {
      /** @todo FIXME: What to do if the ZRL_IGNORE_TILL_EOL flag set */
      self->child_cond = static_cast<GIOCondition>(0);
      self->super.child->timeout = self->super.timeout;
      res = z_stream_read(self->super.child, buf, count, bytes_read, error);
    }
  z_return(res);
}

/**
 * Write data to ZStreamLine.
 *
 * @param[in]  stream ZStreamLine instance
 * @param[in]  buf data to write
 * @param[in]  count length of buffer
 * @param[out] bytes_written number of bytes written will be returned here
 * @param[out] error error value
 *
 * Writes to child, as nothing special needs to be done to write a line anyway.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_line_write_method(ZStream     *stream,
                           const void *buf,
                           gsize       count,
                           gsize       *bytes_written,
                           GError      **error)
{
  ZStreamLine *self = (ZStreamLine *) stream;
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self->super.child->timeout = self->super.timeout;
  res = z_stream_write(self->super.child, buf, count, bytes_written, error);
  z_return(res);
}

/**
 * Write priority data to ZStreamLine.
 *
 * @param[in]  stream ZStreamLine instance
 * @param[in]  buf data to write
 * @param[in]  count length of buffer
 * @param[out] bytes_written number of bytes written will be returned here
 * @param[out] error error value
 *
 * Writes to child, as nothing special needs to be done to write a line anyway.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_line_write_pri_method(ZStream     *stream,
                               const void *buf,
                               gsize       count,
                               gsize       *bytes_written,
                               GError      **error)
{
  ZStreamLine *self = (ZStreamLine *) stream;
  GIOStatus res;
  
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self->super.child->timeout = self->super.timeout;
  res = z_stream_write_pri(self->super.child, buf, count, bytes_written, error);
  z_return(res);
}

/**
 * Shutdown the ZStream (ZStreamLine in this case).
 *
 * @param[in]  stream ZStreamLine instance
 * @param[in]  i HOW parameter to shutdown
 * @param[out] error error value
 *
 * The shutdown method will be called on the child.
 *
 * @see z_stream_shutdown
 *
 * The action to perform is specified by i as follows:
 * - i == 0: Stop receiving data.
 * - i == 1: Stop trying to transmit data.
 * - i == 2: Stop both reception and transmission.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_line_shutdown_method(ZStream *stream, int i, GError **error)
{
  ZStreamLine *self = (ZStreamLine *) stream;
  GIOStatus res;
  
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  res = z_stream_shutdown(self->super.child, i, error);
  z_return(res);
}

/**
 * Process stream control calls on ZStreamLine objects.
 *
 * @param[in]      s ZStream instance
 * @param[in]      function function selector
 * @param[in, out] value parameter to function
 * @param[in]      vlen length of value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_line_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  ZStreamLine *self = Z_CAST(s, ZStreamLine);
  gboolean ret = FALSE;
  
  z_enter();

  switch (ZST_CTRL_MSG(function))
    {
    case ZST_LINE_SET_TRUNCATE:
      if (vlen == sizeof(gboolean))
        {
          gboolean flag = *((gboolean *)value);
          if (flag)
            self->flags |= ZRL_TRUNCATE;
          else
            self->flags &= ~ZRL_TRUNCATE;
          z_return(TRUE);
        }
      break;
      
    case ZST_LINE_SET_NUL_NONFATAL:
      if (vlen == sizeof(gboolean))
        {
          gboolean flag = *((gboolean *)value);
          if (flag)
            self->flags |= ZRL_NUL_NONFATAL;
          else
            self->flags &= ~ZRL_NUL_NONFATAL;
          z_return(TRUE);
        }
      break;
      
    case ZST_LINE_SET_SPLIT:
      if (vlen == sizeof(gboolean))
        {
          gboolean flag = *((gboolean *)value);
          if (flag)
            self->flags |= ZRL_SPLIT;
          else
            self->flags &= ~ZRL_SPLIT;
          z_return(TRUE);
        }
      break;

    case ZST_LINE_SET_SINGLE_READ:
      if (vlen == sizeof(gboolean))
        {
          gboolean flag = *((gboolean *)value);
          if (flag)
            self->flags |= ZRL_SINGLE_READ;
          else
            self->flags &= ~ZRL_SINGLE_READ;
          z_return(TRUE);
        }
      break;

    case ZST_LINE_SET_POLL_PARTIAL:
      if (vlen == sizeof(gboolean))
        {
          gboolean flag = *((gboolean *)value);
          if (flag)
            self->flags |= ZRL_POLL_PARTIAL;
          else
            self->flags &= ~ZRL_POLL_PARTIAL;
          z_return(TRUE);
        }
      break;

    case ZST_LINE_SET_RETURN_EOL:
      if (vlen == sizeof(gboolean))
        {
          gboolean flag = *((gboolean *)value);
          if (flag)
            self->flags |= ZRL_RETURN_EOL;
          else
            self->flags &= ~ZRL_RETURN_EOL;
          z_return(TRUE);
        }
      break;

    case ZST_LINE_GET_TRUNCATE:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = !!(self->flags & ZRL_TRUNCATE);
          z_return(TRUE);
        }
      break;

    case ZST_LINE_GET_SPLIT:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = !!(self->flags & ZRL_SPLIT);
          z_return(TRUE);
        }
      break;

     case ZST_LINE_GET_NUL_NONFATAL:
       if (vlen == sizeof(gboolean))
         {
           *(gboolean *)value = !!(self->flags & ZRL_NUL_NONFATAL);
           z_return(TRUE);
         }
       break;
      
    case ZST_LINE_GET_SINGLE_READ:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = !!(self->flags & ZRL_SINGLE_READ);
          z_return(TRUE);
        }
      break;

    case ZST_LINE_GET_POLL_PARTIAL:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = !!(self->flags & ZRL_POLL_PARTIAL);
          z_return(TRUE);
        }
      break;

    case ZST_LINE_GET_RETURN_EOL:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = !!(self->flags & ZRL_RETURN_EOL);
          z_return(TRUE);
        }
      break;
      
    case ZST_CTRL_SET_CALLBACK_READ:
    case ZST_CTRL_SET_CALLBACK_WRITE:
    case ZST_CTRL_SET_CALLBACK_PRI:
      ret = z_stream_ctrl_method(s, function, value, vlen);
      break;

    case ZST_CTRL_GET_BUFFERED_BYTES:
      if (vlen == sizeof(gsize))
        {
          gsize *size = (gsize *) value;

          *size += self->end - self->pos;

          ret = TRUE;
          if (s->child)
            ret = z_stream_ctrl(s->child, ZST_CTRL_MSG_FORWARD | function, value, vlen);
        }
      break;

    default:
      ret = z_stream_ctrl_method(s, ZST_CTRL_MSG_FORWARD | function, value, vlen);
      break;
    }
  z_return(ret);
}

static gboolean 
z_stream_line_watch_prepare(ZStream *s, GSource *src G_GNUC_UNUSED, gint *timeout)
{
  ZStreamLine *self = Z_CAST(s, ZStreamLine);
  gboolean ret = FALSE;
  gboolean child_enable = FALSE, child_readable;

  z_enter();
  
  *timeout = -1;

  if (s->want_read)
    {
      child_readable = !!(self->child_cond & G_IO_IN);
      if (self->flags & ZRL_POLL_PARTIAL)
        {
          if (z_stream_line_buf_empty(self) && !child_readable)
            {
              child_enable = TRUE;
            }
          else
            {
              child_enable = FALSE;
              ret = TRUE;
            }
        }
      else
        {
          if (!z_stream_line_have_line(self) && !child_readable)
            {
              child_enable = TRUE;
            }
          else
            {
              child_enable = FALSE;
              ret = TRUE;
            }
        }
    }
  else
    child_enable = FALSE;
  
  if (s->want_pri && (self->child_cond & G_IO_PRI))
    ret = TRUE;

  z_stream_set_cond(s->child, G_IO_IN, child_enable);
  
  if (s->want_write)
    z_stream_set_cond(s->child, G_IO_OUT, TRUE);
  else
    z_stream_set_cond(s->child, G_IO_OUT, FALSE);
  
  z_return(ret);
}

static gboolean 
z_stream_line_watch_check(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamLine *self = (ZStreamLine *) s;
  gboolean ret = FALSE, child_readable;

  z_enter();

  if (s->want_read)
    {
      child_readable = !!(self->child_cond & G_IO_IN);
      if (self->flags & ZRL_POLL_PARTIAL)
        {
          if (!z_stream_line_buf_empty(self) || child_readable)
            {
              ret = TRUE;
            }
        }
      else
        {
          if (z_stream_line_have_line(self) || child_readable)
            {
              ret = TRUE;
            }
        }
    }
  
  if (s->want_pri && (self->child_cond & G_IO_PRI))
    ret = TRUE;

  z_return(ret);
}

static gboolean 
z_stream_line_watch_dispatch(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamLine *self = (ZStreamLine *) s;
  gboolean rc = TRUE;

  z_enter();
  if (s->want_read && rc)
    rc = self->super.read_cb(s, G_IO_IN, self->super.user_data_read);
  else if (s->want_pri && rc)
    rc = self->super.pri_cb(s, G_IO_PRI, self->super.user_data_pri);
  z_return(rc);
}

/**
 * Get size of extra context data.
 *
 * @param[in] s ZStreamLine instance
 *
 * @returns size of extra context data
 **/
static gsize
z_stream_line_extra_get_size_method(ZStream *s)
{
  return Z_SUPER(s, ZStream)->extra_get_size(s) + sizeof(ZStreamLineExtra);
}

static gsize
z_stream_line_extra_save_method(ZStream *s, gpointer extra)
{
  ZStreamLine *self = Z_CAST(s, ZStreamLine);
  ZStreamLineExtra *line_extra;
  gsize ofs;
  
  ofs = Z_SUPER(s, ZStream)->extra_save(s, extra);
  
  line_extra = (ZStreamLineExtra *) (((gchar *) extra) + ofs);
  line_extra->flags = self->flags & ZRL_SAVED_FLAGS_MASK;
  return ofs + sizeof(ZStreamLineExtra);
}

static gsize
z_stream_line_extra_restore_method(ZStream *s, gpointer extra)
{
  ZStreamLine *self = Z_CAST(s, ZStreamLine);
  ZStreamLineExtra *line_extra;
  gsize ofs;
  
  ofs = Z_SUPER(s, ZStream)->extra_restore(s, extra);
  
  line_extra = (ZStreamLineExtra *) (((gchar *) extra) + ofs);
  self->flags = (self->flags & ~ZRL_SAVED_FLAGS_MASK) | (line_extra->flags & ZRL_SAVED_FLAGS_MASK);
  return ofs + sizeof(ZStreamLineExtra);
}

/**
 * Stack a new stream (will become child stream) beneath self.
 *
 * @param[in] s parent ZStream
 * @param[in] new_child will be set as child
 **/
static void
z_stream_line_set_child(ZStream *s, ZStream *new_child)
{
  z_stream_ref(s);

  Z_SUPER(s, ZStream)->set_child(s, new_child);
  
  if (new_child)
    {
      z_stream_set_callback(new_child, G_IO_IN, z_stream_line_read_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_OUT, z_stream_line_write_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_PRI, z_stream_line_pri_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
    }

  z_stream_unref(s);
}

/**
 * Constructs a new ZStreamLine instance, reading from child, using a buffer
 * sized according to bufsize.
 *
 * @param[in] child 
 * @param[in] bufsize 
 * @param[in] flags
 *
 * @returns the new ZStreamLine instance
 **/
ZStream *
z_stream_line_new(ZStream *child, gsize bufsize, guint flags)
{
  ZStreamLine *self;

  z_enter();
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamLine), child ? child->name : "", G_IO_IN), ZStreamLine);
  self->flags = flags;
  self->bufsize = bufsize;
  self->buffer = g_new(gchar, bufsize);
  z_stream_set_child(&self->super, child);
  z_return((ZStream *) self);
}

/**
 * Destructor of ZStreamLine.
 *
 * @param[in] s ZStreamLine instance
 **/
static void
z_stream_line_free_method(ZObject *s)
{
  ZStreamLine *self = Z_CAST(s, ZStreamLine);

  z_enter();
  g_free(self->buffer);
  z_stream_free_method(s);
  z_return();
}

/**
 * ZStreamLine virtual methods.
 **/
ZStreamFuncs z_stream_line_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_line_free_method,
  },
  z_stream_line_read_method,
  z_stream_line_write_method,
  NULL,
  z_stream_line_write_pri_method,
  z_stream_line_shutdown_method,
  NULL, /* close */
  z_stream_line_ctrl_method,
  
  NULL, /* attach_source */
  NULL, /* detach_source */
  z_stream_line_watch_prepare,
  z_stream_line_watch_check,
  z_stream_line_watch_dispatch,
  NULL,
  z_stream_line_extra_get_size_method,
  z_stream_line_extra_save_method,
  z_stream_line_extra_restore_method,
  z_stream_line_set_child,
  z_stream_line_unget_packet_method,
};

/**
 * ZStreamLine class descriptor.
 **/
Z_CLASS_DEF(ZStreamLine, ZStream, z_stream_line_funcs);
