/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/streamgzip.h>
#include <zorpll/stream.h>
#include <zorpll/log.h>

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

/** gzip stream state */
enum
{
  Z_SGS_EOF_RECEIVED      = 0x0001,
  Z_SGS_COMPRESS_FINISHED = 0x0002,
  Z_SGS_HEADER_READ       = 0x0004,
  Z_SGS_HEADER_WRITTEN    = 0x0008,
  Z_SGS_READ_ERROR        = 0x0010,
  Z_SGS_WRITE_ERROR       = 0x0020,
};

extern ZClass ZStreamGzip__class;

typedef struct _ZStreamGzip
{
  ZStream super;

  guint flags;
  z_stream encode;
  z_stream decode;
  gsize buffer_length;
  /* FIXME: I think this is pointers below is NOT a void * */
  void *buffer_encode_out;
  void *buffer_encode_out_p;
  void *buffer_decode_in;

  guint32 state;
  gint shutdown;
  GIOCondition child_cond;

  guint32 encode_crc, encode_in;
  
  time_t gzip_timestamp;
  gsize gzip_extra_len;
  gchar *gzip_extra;
  gchar *gzip_origname;
  gchar *gzip_comment;
  /* FIXME: header crc not supported yet */
} ZStreamGzip;


/* NOTE: these values are defined but not exported by zlib */

#ifdef G_OS_WIN32
#define Z_GZ_OS_CODE 0x0b   /**< win32 */
#else
#define Z_GZ_OS_CODE 0x03   /**< unix */
#endif

#define Z_GZH_ASCII_FLAG   0x01 /**< bit 0 set: file probably ascii text */
#define Z_GZH_HEAD_CRC     0x02 /**< bit 1 set: header CRC present */
#define Z_GZH_EXTRA_FIELD  0x04 /**< bit 2 set: extra field present */
#define Z_GZH_ORIG_NAME    0x08 /**< bit 3 set: original file name present */
#define Z_GZH_COMMENT      0x10 /**< bit 4 set: file comment present */
#define Z_GZH_RESERVED     0xE0 /**< bits 5..7: reserved */

#define MAX_GZIP_HEADER_STRING  4096

/**
 * This is an internal function to read a gzip header style, NUL
 * terminated string from a stream.
 *
 * @param[in]  child stream to read data from (usually the gzip child stream
 * @param[out] dest return the gzip string here
 *
 * It is used to fetch the comment/origname fields in the gzip header.
 **/
static gboolean
z_stream_gzip_read_gzip_string(ZStream *child, gchar **dest)
{
  gchar buf[MAX_GZIP_HEADER_STRING];
  gsize rdlen = 0, br;
  GIOStatus status;
     
  while (rdlen < sizeof(buf) - 1 && 
         (status = z_stream_read(child, &buf[rdlen], 1, &br, NULL)) == G_IO_STATUS_NORMAL && 
         buf[rdlen] != 0)
    {
      rdlen += br;
    }

  if (buf[rdlen] != 0)
    {
      /* string longer than MAX_GZIP_HEADER_STRING, truncate buf */
      gchar ch;
      while ((status = z_stream_read(child, &ch, 1, &br, NULL)) == G_IO_STATUS_NORMAL && ch != 0)
        {
        }
    }
  if (status != G_IO_STATUS_NORMAL)
    return FALSE;
  buf[rdlen] = 0;
  *dest = strdup(buf);
  return TRUE;
}

/**
 * This function frees memory associated with the gzip header and sets
 * everything back to 'not-set' state.
 *
 * @param[in] self ZStreamGzip instance
 **/
static void
z_stream_gzip_reset_gzip_header(ZStreamGzip *self)
{
  if (self->gzip_origname)
    {
      g_free(self->gzip_origname);
      self->gzip_origname = NULL;
    }

  if (self->gzip_comment)
    {
      g_free(self->gzip_comment);
      self->gzip_comment = NULL;
    }

  if (self->gzip_extra)
    {
      g_free(self->gzip_extra);
      self->gzip_extra = NULL;
    }
  self->gzip_extra_len = 0;
}

/**
 * This internal function fetches a GZip header from the child stream
 * and stores the results in self.
 *
 * @param[in]  self ZStreamGzip instance
 * @param[out] error error value
 *
 * @returns FALSE to indicate failure (in which case it also sets error)
 **/
static gboolean
z_stream_gzip_read_gzip_header(ZStreamGzip *self, GError **error)
{
  ZStream *child = self->super.child;
  guchar buf[16];
  gsize br;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if ((self->flags & Z_SGZ_GZIP_HEADER) && (self->state & Z_SGS_HEADER_READ) == 0)
    {
      self->state |= Z_SGS_HEADER_READ;
      z_stream_gzip_reset_gzip_header(self);

      if (z_stream_read_chunk(child, buf, 4, &br, error) != G_IO_STATUS_NORMAL || br != 4)
        goto error;
      
      if (!GZIP_IS_GZIP_MAGIC(buf))
        goto error;

      if (z_stream_read_chunk(child, buf + 4, 6, &br, error) != G_IO_STATUS_NORMAL || br != 6)
        goto error;

      self->gzip_timestamp = (time_t) (((guint)buf[7]) << 24) | (((guint)buf[6]) << 16) | (((guint)buf[5]) << 8) | buf[4];

      if (buf[3] & Z_GZH_EXTRA_FIELD)
        {
          if (z_stream_read_chunk(child, buf, 2, &br, error) != G_IO_STATUS_NORMAL || br != 2)
            goto error;
          
          self->gzip_extra_len = buf[0] + (((guint)buf[1]) << 8);
          self->gzip_extra = g_new0(gchar, self->gzip_extra_len);
          
          if (z_stream_read_chunk(child, self->gzip_extra, self->gzip_extra_len, &br, error) != G_IO_STATUS_NORMAL || br != self->gzip_extra_len)
            goto error;
          
        }
      
      if (buf[3] & Z_GZH_ORIG_NAME)
        {
          if (!z_stream_gzip_read_gzip_string(child, &self->gzip_origname))
            goto error;
        }

      if (buf[3] & Z_GZH_COMMENT)
        {
          if (!z_stream_gzip_read_gzip_string(child, &self->gzip_comment))
            goto error;
        }

      if ((buf[3] & Z_GZH_HEAD_CRC) && (z_stream_read_chunk(child, buf, 2, &br, error) != G_IO_STATUS_NORMAL || br != 2))
        goto error;
    }
  z_return(TRUE);

 error:
  z_return(FALSE);
}

/**
 * Write a gzip header into a stream.
 *
 * @param[in]  self the stream
 * @param[out] error error value
 *
 * All args have default values, so if timestamp is zero, the current time is used, the string fields
 * are written as empty if NULL.
 *
 * @returns The status of the operation.
 **/
static gboolean
z_stream_gzip_write_gzip_header(ZStreamGzip *self, GError **error)
{
  /* FIXME: writing of header crc not supported yet */
  ZStream *sc = self->super.child;
  gchar buf[16];
  GError *local_error = NULL;
  gsize bw;

  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if ((self->flags & Z_SGZ_GZIP_HEADER) && (self->state & Z_SGS_HEADER_WRITTEN) == 0)
    {
      self->state |= Z_SGS_HEADER_WRITTEN;
      g_snprintf(buf, sizeof(buf), "%c%c%c%c%c%c%c%c%c%c",
                 GZIP_MAGIC_1, GZIP_MAGIC_2, Z_DEFLATED,
                 (self->gzip_origname ? Z_GZH_ORIG_NAME : 0) | (self->gzip_comment ? Z_GZH_COMMENT : 0) | (self->gzip_extra ? Z_GZH_EXTRA_FIELD : 0), 
                 (gint) (self->gzip_timestamp & 0xff),
                 (gint) ((self->gzip_timestamp >> 8) & 0xff),
                 (gint) ((self->gzip_timestamp >> 16) & 0xff),
                 (gint) ((self->gzip_timestamp >> 24) & 0xff), 
                 0 /*xflags*/, Z_GZ_OS_CODE);

      if (z_stream_write_chunk(sc, buf, 10, &bw, &local_error) != G_IO_STATUS_NORMAL)
        goto error;

      if (self->gzip_extra)
        {
          buf[0] = self->gzip_extra_len & 0xff;
          buf[1] = (self->gzip_extra_len >> 8) & 0xff;
          if (z_stream_write_chunk(sc, buf, 2, &bw, &local_error) != G_IO_STATUS_NORMAL)
            goto error;

          if (z_stream_write_chunk(sc, self->gzip_extra, self->gzip_extra_len, &bw, &local_error) != G_IO_STATUS_NORMAL)
            goto error;
        }

      if (self->gzip_origname && z_stream_write_chunk(sc, self->gzip_origname, 1 + strlen(self->gzip_origname), &bw, &local_error) != G_IO_STATUS_NORMAL)
        goto error;

      if (self->gzip_comment && z_stream_write_chunk(sc, self->gzip_comment, 1 + strlen(self->gzip_comment), &bw, &local_error) != G_IO_STATUS_NORMAL)
        goto error;
    }
  z_return(TRUE);

 error:
  if (local_error)
    g_propagate_error(error, local_error);
  z_return(FALSE);
}

static gboolean
z_stream_gzip_write_gzip_trailer(ZStreamGzip *self, GError **error)
{
  gsize bw;
  
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if ((self->flags & Z_SGZ_GZIP_HEADER) && (self->state & Z_SGS_HEADER_WRITTEN) != 0)
    {
      gint j;
      guint32 x;
      guchar buf[8];
      
      x = self->encode_crc;
      for (j = 0; j < 4; j++) 
        {
          buf[j] = (x & 0xff);
          x >>= 8;
        }
      x = self->encode_in;
      for (j = 4; j < 8; j++) 
        {
          buf[j] = (x & 0xff);
          x >>= 8;
        }
      x = 8;
      if (z_stream_write_chunk(self->super.child, buf, 8, &bw, error) != G_IO_STATUS_NORMAL)
        return FALSE;
    }
  return TRUE;
}

/* I/O callbacks for stacked stream */

static gboolean
z_stream_gzip_read_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond G_GNUC_UNUSED, gpointer s)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);

  z_enter();
  self->child_cond = static_cast<GIOCondition>(self->child_cond | G_IO_IN);
  z_return(TRUE);
}

static gboolean
z_stream_gzip_write_callback(ZStream *stream G_GNUC_UNUSED, GIOCondition poll_cond G_GNUC_UNUSED, gpointer s)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);
  GIOStatus res;

  z_enter();
  /*
   * If some data in output buffer try to write it out.
   * If write is not success leave it, and doesn't set
   * it writeable.
   */
  if (self->encode.avail_out < self->buffer_length)
    {
      gint length = (gchar *)self->encode.next_out - (gchar *)self->buffer_encode_out_p;
      gsize writted_length;
      
      res = z_stream_write(self->super.child, self->buffer_encode_out_p, length, &writted_length, NULL);
      if (res == G_IO_STATUS_AGAIN)
        z_return(TRUE);
      if (res != G_IO_STATUS_NORMAL)
        {
          self->state |= Z_SGS_WRITE_ERROR;
          z_return(TRUE);
        }
      /*FIXME: See the same expression in stream.c */
      self->buffer_encode_out_p = (void *)(gchar *) ((gchar *)self->buffer_encode_out_p +  writted_length);
      if ((gchar *)self->buffer_encode_out_p != (gchar *)self->encode.next_out)
        z_return(TRUE);
    }
  self->child_cond = static_cast<GIOCondition>(self->child_cond | G_IO_OUT);
  z_return(TRUE);
}

/* virtual methods */

static GIOStatus
z_stream_gzip_read_method(ZStream *stream, void *buf, gsize count, gsize *bytes_read, GError **error)
{
  ZStreamGzip *self = Z_CAST(stream, ZStreamGzip);
  GIOStatus res;
  gint ret;
  GError *local_error = NULL;
  
  z_enter();
  self->child_cond = static_cast<GIOCondition>(self->child_cond & ~G_IO_IN);
  if (self->shutdown & G_IO_IN)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Read direction already shut down");
      z_return(G_IO_STATUS_ERROR);
    }
  
  if (self->state & Z_SGS_COMPRESS_FINISHED)
    z_return(G_IO_STATUS_EOF);

  if (self->state & Z_SGS_READ_ERROR)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Previously stored error condition");
      z_return(G_IO_STATUS_ERROR);
    }

  if (!z_stream_gzip_read_gzip_header(self, error))
    z_return(G_IO_STATUS_ERROR);

  self->decode.next_out = static_cast<Bytef *>(buf);
  self->decode.avail_out = count;

  if (self->decode.avail_in == 0 && (self->state & Z_SGS_EOF_RECEIVED) == 0)
    {
      gsize length;
          
      self->decode.next_in = static_cast<Bytef *>(self->buffer_decode_in);
      res = z_stream_read(self->super.child, self->decode.next_in, self->buffer_length, &length, &local_error);
      self->decode.avail_in = length;
      if (res == G_IO_STATUS_AGAIN)
        {
          z_return(G_IO_STATUS_AGAIN);
        }
      else if (res == G_IO_STATUS_EOF)
        {
          self->state |= Z_SGS_EOF_RECEIVED;
        }
      else if (res != G_IO_STATUS_NORMAL)
        {
          self->state |= Z_SGS_READ_ERROR;
          if (local_error)
            g_propagate_error(error, local_error);
          z_return(G_IO_STATUS_ERROR);
        }
    }
      
  ret = inflate(&self->decode, Z_NO_FLUSH);
  if ((ret != Z_OK) && (ret != Z_STREAM_END))
    {
      self->state |= Z_SGS_READ_ERROR;
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Error while inflating data (%s)", Z_STRING_SAFE(self->decode.msg));
      z_return(G_IO_STATUS_ERROR);
    }
  if (ret == Z_STREAM_END)
    self->state |= Z_SGS_COMPRESS_FINISHED;

  /* FIXME: the crc32 trailer is not read */
  *bytes_read = count - self->decode.avail_out;
  z_return(G_IO_STATUS_NORMAL);
}

static GIOStatus
z_stream_gzip_write_method(ZStream *stream, const void *buf, gsize count, gsize *bytes_written, GError **error)
{
  ZStreamGzip *self = Z_CAST(stream, ZStreamGzip);
  GIOStatus res = G_IO_STATUS_NORMAL;
  gint rc;
  gsize length;
  gsize writted_length;
  GError *local_error = NULL;

  z_enter();
  self->child_cond = static_cast<GIOCondition>(self->child_cond & ~G_IO_OUT);
  if (self->shutdown & G_IO_OUT)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Write direction already shut down");
      z_return(G_IO_STATUS_ERROR);
    }

  if (self->state & Z_SGS_WRITE_ERROR)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Previously stored error condition");
      z_return(G_IO_STATUS_ERROR);
    }

  if (!z_stream_gzip_write_gzip_header(self, error))
    z_return(G_IO_STATUS_ERROR);

  if (self->encode.avail_out < self->buffer_length)
    {
      length = (gchar *)self->encode.next_out - (gchar *)self->buffer_encode_out_p;
      res = z_stream_write(self->super.child, self->buffer_encode_out_p, length, &writted_length, &local_error);
      if (res == G_IO_STATUS_AGAIN)
        z_return(G_IO_STATUS_AGAIN);

      if (res != G_IO_STATUS_NORMAL)
        {
          if (local_error)
            g_propagate_error(error, local_error);
          self->state |= Z_SGS_WRITE_ERROR;
          z_return(G_IO_STATUS_ERROR);
        }
      
      self->buffer_encode_out_p = (void *)(gchar *) ((gchar *)self->buffer_encode_out_p + writted_length);
      if ((gchar *)self->buffer_encode_out_p != (gchar *)self->encode.next_out)
        z_return(G_IO_STATUS_AGAIN);
    }
  
  self->encode.next_out = static_cast<Bytef *>(self->buffer_encode_out);
  self->encode.avail_out = self->buffer_length;
  self->encode.next_in = static_cast<Bytef *>(const_cast<void *>(buf));
  self->encode.avail_in = count;
  self->buffer_encode_out_p = self->buffer_encode_out;

  while (res == G_IO_STATUS_NORMAL && self->encode.avail_in)
    {
      if (self->encode.avail_out)
        {
          rc = deflate(&self->encode, self->flags & Z_SGZ_SYNC_FLUSH ? Z_SYNC_FLUSH : Z_NO_FLUSH);
          if (rc != Z_OK)
            {
              g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Error while deflating data (%s)", self->encode.msg);
              self->state |= Z_SGS_READ_ERROR;
              z_return(G_IO_STATUS_ERROR);
            }
        }
  
      length = self->buffer_length - self->encode.avail_out;
      res = z_stream_write(self->super.child, self->buffer_encode_out, length, &writted_length, &local_error);
      if (res == G_IO_STATUS_NORMAL)
        {
          self->buffer_encode_out_p = (void *)(gchar *) ((gchar *)self->buffer_encode_out_p + writted_length);
          if ((gchar *)self->buffer_encode_out_p == (gchar *)self->encode.next_out)
            {
              self->encode.next_out = static_cast<Bytef *>(self->buffer_encode_out);
              self->encode.avail_out = self->buffer_length;
              self->buffer_encode_out_p = self->buffer_encode_out;
            }
        }
      else if (res != G_IO_STATUS_AGAIN)
        {
          self->state |= Z_SGS_WRITE_ERROR;
          if (local_error)
            g_propagate_error(error, local_error);
          z_return(G_IO_STATUS_ERROR);
        }
    }
  
  *bytes_written = count - self->encode.avail_in;
  if (*bytes_written == 0)
    z_return(G_IO_STATUS_AGAIN);

  if (self->flags & Z_SGZ_GZIP_HEADER)
    {
      self->encode_crc = crc32(self->encode_crc, static_cast<const Bytef *>(buf), *bytes_written);
      self->encode_in += *bytes_written;
    }
  
  z_return(G_IO_STATUS_NORMAL);
}


static GIOStatus
z_stream_gzip_shutdown_method(ZStream *stream, int method, GError **error)
{
  ZStreamGzip *self = Z_CAST(stream, ZStreamGzip);
  GIOStatus res = G_IO_STATUS_ERROR, ret = G_IO_STATUS_NORMAL;
  GError *local_error = NULL;
  gint rc;
  
  /* FIXME: do not use res and ret at the same time */
  z_enter();
  
  if ((method == SHUT_RD || method == SHUT_RDWR) && (self->shutdown & G_IO_IN) == 0)
    {
      rc = inflateEnd(&self->decode);
      if (rc == Z_OK)
        res = G_IO_STATUS_NORMAL;
      self->shutdown |= G_IO_IN;
    }
  
  if ((method == SHUT_WR || method == SHUT_RDWR) && (self->shutdown & G_IO_OUT) == 0)
    {
      gboolean i;
      gsize length;
      
      i = z_stream_get_nonblock(self->super.child);
      z_stream_set_nonblock(self->super.child, FALSE);
      
      while ((gchar *)self->buffer_encode_out_p != (gchar *)self->encode.next_out && ret == G_IO_STATUS_NORMAL)
        {
          ret = z_stream_write(self->super.child, self->buffer_encode_out_p, (gchar *)self->encode.next_out - (gchar *)self->buffer_encode_out_p, &length, &local_error);
          if (ret == G_IO_STATUS_NORMAL)
            self->buffer_encode_out_p = (void *)(gchar *) ((gchar *)self->buffer_encode_out_p + length);
        }
      
      if (ret == G_IO_STATUS_NORMAL)
        {
          self->encode.avail_out = self->buffer_length;
          self->encode.next_out = static_cast<Bytef *>(self->buffer_encode_out);
          self->encode.avail_in = 0;
          self->encode.next_in = NULL;
          
          self->buffer_encode_out_p = self->buffer_encode_out;
          
          rc = deflate(&self->encode, Z_FINISH);
          if (rc == Z_STREAM_END)
            {
              if (self->encode.avail_out < self->buffer_length)
                {
                  while ((gchar *)self->buffer_encode_out_p != (gchar *)self->encode.next_out && ret == G_IO_STATUS_NORMAL)
                    {
                      ret = z_stream_write(self->super.child, self->buffer_encode_out_p, (gchar *)self->encode.next_out - (gchar *)self->buffer_encode_out_p, &length, &local_error);
                      if (ret == G_IO_STATUS_NORMAL)
                        self->buffer_encode_out_p = (void *)(gchar *) ((gchar *)self->buffer_encode_out_p + length);
                    }
                }

              if (self->flags & Z_SGZ_WRITE_EMPTY_HEADER)
                {
                  if (!z_stream_gzip_write_gzip_header(self, &local_error))
                    res = G_IO_STATUS_ERROR;
                }

              if (res == G_IO_STATUS_NORMAL && !z_stream_gzip_write_gzip_trailer(self, &local_error))
                res = G_IO_STATUS_ERROR;
              
            }
        }
      z_stream_set_nonblock(self->super.child, i);
      rc = deflateEnd(&self->encode);
      if (ret == G_IO_STATUS_NORMAL && rc == Z_OK)
        res = G_IO_STATUS_NORMAL;
      self->shutdown |= G_IO_OUT;
    }

  ret = z_stream_shutdown(self->super.child, method, error);
  if (ret != G_IO_STATUS_NORMAL)
    res = ret;
  
  if (local_error)
    g_propagate_error(error, local_error);
  z_return(res);

}

static GIOStatus
z_stream_gzip_close_method(ZStream *s, GError **error)
{
  GIOStatus st_shutdown, st_close;

  z_enter();
  st_shutdown = z_stream_gzip_shutdown_method(s, SHUT_RDWR, NULL);
  st_close = Z_SUPER(s, ZStream)->close(s, error);
  if (st_shutdown != G_IO_STATUS_NORMAL)
    z_return(st_shutdown);

  z_return(st_close);
}

/**
 * Process stream control calls.
 *
 * @param[in]      stream ZStream instance
 * @param[in]      function function selector
 * @param[in, out] value parameter to function
 * @param[in]      vlen length of value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_gzip_ctrl_method(ZStream *stream, guint function, gpointer value, guint vlen)
{
  gboolean ret = FALSE;
  
  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_SET_CALLBACK_READ:
    case ZST_CTRL_SET_CALLBACK_WRITE:
    case ZST_CTRL_SET_CALLBACK_PRI:
      ret = z_stream_ctrl_method(stream, function, value, vlen);
      break;

    case ZST_CTRL_GET_BUFFERED_BYTES:
      if (vlen == sizeof(gsize))
        {
          gsize *size = (gsize *) value;
          ZStreamGzip *self = Z_CAST(stream, ZStreamGzip);

          *size += self->decode.avail_in;

          ret = TRUE;
          if (stream->child)
            ret = z_stream_ctrl(stream->child, ZST_CTRL_MSG_FORWARD | function, value, vlen);
        }
      break;
      
    default:
      ret = z_stream_ctrl_method(stream, ZST_CTRL_MSG_FORWARD | function, value, vlen);
      break;
    }
  z_return(ret);
}

static gboolean 
z_stream_gzip_watch_prepare(ZStream *s, GSource *src G_GNUC_UNUSED, gint *timeout)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);
  gboolean ret = FALSE;
  gboolean child_readable, child_enable;

  z_enter();
  
  *timeout = -1;

  if (s->want_read)
    {
      child_readable = !!(self->child_cond & G_IO_IN);

      if (self->decode.avail_in == 0 && !child_readable)
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
      child_enable = FALSE;
    }

  z_stream_set_cond(s->child, G_IO_IN, child_enable);

  if (s->want_write)
    {
      if (self->encode.avail_out == self->buffer_length)
        ret = TRUE;
    }
  
  if (self->encode.avail_out != self->buffer_length)
    z_stream_set_cond(s->child, G_IO_OUT, TRUE);
  else
    z_stream_set_cond(s->child, G_IO_OUT, FALSE);

  z_return(ret);
}

static gboolean 
z_stream_gzip_watch_check(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);
  gboolean ret = FALSE;
  gboolean child_readable, child_writeable;

  z_enter();
  if (s->want_read)
    {
      child_readable = !!(self->child_cond & G_IO_IN);
      if (self->decode.avail_in || child_readable)
        ret = TRUE;
    }

  if (s->want_write)
    {
      child_writeable = !!(self->child_cond & G_IO_OUT);
      if (self->encode.avail_out == self->buffer_length || child_writeable)
        ret = TRUE;
    }
  z_return(ret);
}

static
gboolean 
z_stream_gzip_watch_dispatch(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);
  gboolean rc = TRUE;
  gboolean child_readable, child_writeable;

  z_enter();
  if (s->want_read && rc)
    {
      child_readable = !!(self->child_cond & G_IO_IN);
      if (self->decode.avail_in || child_readable)
        rc = self->super.read_cb(s, G_IO_IN, self->super.user_data_read);
    }

  if (s->want_write && rc)
    {
      child_writeable = !!(self->child_cond & G_IO_OUT);
      if (self->encode.avail_out == self->buffer_length || child_writeable)
        rc = self->super.write_cb(s, G_IO_OUT, self->super.user_data_write);
    }
  z_return(rc);
}

static void
z_stream_gzip_set_child(ZStream *s, ZStream *new_child)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);

  z_stream_ref(s);
  
  Z_SUPER(s, ZStream)->set_child(s, new_child);
  if (new_child)
    {
      z_stream_set_callback(new_child, G_IO_IN, z_stream_gzip_read_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_OUT, z_stream_gzip_write_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);

      g_assert((self->flags & Z_SGZ_GZIP_HEADER) == 0 || z_stream_get_nonblock(new_child) == FALSE);
    }

  z_stream_unref(s);
}

/**
 * Get header fields.
 *
 * @param[in]  s stream top
 * @param[out] timestamp returned timestamp
 * @param[out] origname returned original name
 * @param[out] comment returned comment
 * @param[out] extra_len returned length of extra data
 * @param[out] extra returned extra data
 *
 * This function returns the gzip header values as stored in the
 * top-most gzip stream. The header fields are implicitly set by the
 * first read operation, or by an explicit call to
 * z_stream_gzip_fetch_header().
 *
 * Any NULL pointers passed for any of the [out] variables results
 * nothing being returned for those values.
 **/
void
z_stream_gzip_get_header_fields(ZStream *s, time_t *timestamp, gchar **origname, gchar **comment, gint *extra_len, gchar **extra)
{
  ZStreamGzip *self;

  self = Z_CAST(z_stream_search_stack(s, G_IO_IN, Z_CLASS(ZStreamGzip)), ZStreamGzip);
  
  if (timestamp)
    *timestamp = self->gzip_timestamp;

  if (origname)
    *origname = self->gzip_origname;

  if (comment)
    *comment = self->gzip_comment;

  if (extra_len && extra)
    { 
      *extra = self->gzip_extra;
      *extra_len = self->gzip_extra_len;
    }
}

/**
 * This function sets the gzip header fields to be written
 * automatically on the first write operation, or during shutdown.
 *
 * @param[in] s stream top
 * @param[in] timestamp timestamp
 * @param[in] origname original name
 * @param[in] comment comment
 * @param[in] extra_len length of extra data
 * @param[in] extra extra data
 **/
void
z_stream_gzip_set_header_fields(ZStream *s, time_t timestamp, const gchar *origname, const gchar *comment, gint extra_len, const gchar *extra)
{
  ZStreamGzip *self;

  z_enter();
  self = Z_CAST(z_stream_search_stack(s, G_IO_OUT, Z_CLASS(ZStreamGzip)), ZStreamGzip);
  z_stream_gzip_reset_gzip_header(self);
  self->gzip_timestamp = timestamp ? timestamp : time(NULL);
  self->gzip_origname = g_strdup(origname);
  self->gzip_comment = g_strdup(comment);
  if (extra_len > 0)
    {
      self->gzip_extra_len = extra_len;
      memcpy(self->gzip_extra, extra, extra_len);
    }
  z_return();
}

/**
 * This function explicitly requests the gzip header to be read if
 * Z_SGZ_GZIP_HEADER is set without actually attempting to decompress
 * data.
 *
 * @param[in]  s stream top
 * @param[out] error error value
 *
 * It is useful when the contents of the gzip header are needed
 * before issuing the first read operation.
 **/
gboolean
z_stream_gzip_fetch_header(ZStream *s, GError **error)
{
  ZStreamGzip *self;

  self = Z_CAST(z_stream_search_stack(s, G_IO_IN, Z_CLASS(ZStreamGzip)), ZStreamGzip);
  return z_stream_gzip_read_gzip_header(self, error);
}

/** constructor */
/*
 * Flags:
 * - Automatic flush
 */
ZStream *
z_stream_gzip_new(ZStream *child, gint flags, guint level, guint buffer_length)
{
  ZStreamGzip *self;

  z_enter();
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamGzip), child ? child->name : "", G_IO_IN | G_IO_OUT), ZStreamGzip);
  self->flags = flags;
  if (flags & Z_SGZ_GZIP_HEADER)
    {
      deflateInit2(&self->encode, level, Z_DEFLATED, -MAX_WBITS, level, Z_DEFAULT_STRATEGY);
      inflateInit2(&self->decode, -MAX_WBITS);
      /* windowBits is passed < 0 to suppress zlib header */
    }
  else
    {
      deflateInit(&self->encode, level);
      inflateInit(&self->decode);
    }
    
  self->gzip_timestamp = 0;
  self->gzip_extra_len = 0;
  self->gzip_extra = self->gzip_origname = self->gzip_comment = NULL;

  self->buffer_length = buffer_length;
  
  self->buffer_encode_out = g_new(gchar, self->buffer_length);
  self->buffer_decode_in  = g_new(gchar, self->buffer_length);
  
  self->encode.next_out = static_cast<Bytef *>(self->buffer_encode_out);
  self->encode.avail_out = self->buffer_length;
  self->buffer_encode_out_p = self->buffer_encode_out;
  
  z_stream_set_child(&self->super, child);
  z_return((ZStream *) self);
}

/** destructor */
static void
z_stream_gzip_free_method(ZObject *s)
{
  ZStreamGzip *self = Z_CAST(s, ZStreamGzip);

  z_enter();
  g_free(self->buffer_encode_out);
  g_free(self->buffer_decode_in);
  z_stream_gzip_reset_gzip_header(self);
  z_stream_free_method(s);
  z_return();
}


/**
 * ZStreamGzip virtual methods.
 **/
ZStreamFuncs z_stream_gzip_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_gzip_free_method,
  },
  z_stream_gzip_read_method,
  z_stream_gzip_write_method,
  NULL,
  NULL,
  z_stream_gzip_shutdown_method,
  z_stream_gzip_close_method,
  z_stream_gzip_ctrl_method,
  
  NULL, /* attach_source */
  NULL, /* detach_source */
  z_stream_gzip_watch_prepare,
  z_stream_gzip_watch_check,
  z_stream_gzip_watch_dispatch,
  NULL,
  NULL,
  NULL,
  NULL,
  z_stream_gzip_set_child,
  NULL,
};

Z_CLASS_DEF(ZStreamGzip, ZStream, z_stream_gzip_funcs);
