#include <zorpll/code_gzip.h>

// #include <zorpll/zorp.h>
#include <zorpll/log.h>

#include <zlib.h>

/**
 * ZCode-derived class to facilitate gzip compression/decompression.
 **/
typedef struct ZCodeGzip
{
  ZCode super;
  gboolean encode;
  z_stream gzip;
  gboolean end_of_stream;
} ZCodeGzip;

/* decoder */

/**
 * Transform (gzip compress/encode or decompress/decode) the contents of buf into the buffer of the ZCodeGzip instance.
 *
 * @param[in] s ZCodeGzip instance
 * @param[in] buf input buffer
 * @param[in] buflen length of buf
 *
 * @return TRUE on success
 **/
static gboolean
z_code_gzip_transform(ZCode *s, const void *buf, gsize buflen)
{
  ZCodeGzip *self = (ZCodeGzip *) s;
  gint rc;

  if (!buflen)
    return TRUE;

  if (self->end_of_stream)
    {
      z_log(NULL, CORE_ERROR, 3, "Error during GZip transformation, data after EOF; mode='%d'", self->encode);
      return FALSE;
    }

  self->gzip.next_in = (guchar *) buf;
  self->gzip.avail_in = buflen;
  do
    {
      guint needed = self->encode ? self->gzip.avail_in : self->gzip.avail_in * 2;
      
      if ((s->buf_len - s->buf_used) < needed)
        z_code_grow(s, s->buf_len + needed);

      self->gzip.next_out = s->buf + s->buf_used;
      self->gzip.avail_out = s->buf_len - s->buf_used;
      
      rc = self->encode 
        ? deflate(&self->gzip, Z_SYNC_FLUSH) 
        : inflate(&self->gzip, Z_NO_FLUSH);

      if (rc < 0)
        {
          z_log(NULL, CORE_ERROR, 3, "Error in GZip transformation data; rc='%d', error='%s', avail_in='%d', avail_out='%d'", rc, Z_STRING_SAFE(self->gzip.msg), self->gzip.avail_in, self->gzip.avail_out);
          return FALSE;
        }
      if (rc == Z_STREAM_END)
        self->end_of_stream = TRUE;
      s->buf_used = s->buf_len - self->gzip.avail_out;
    }
  while (self->gzip.avail_in != 0);

  return TRUE;
}

/**
 * Destroy the ZCodeGzip instance.
 *
 * @param[in] s ZCodeGzip instance
 **/
static void
z_code_gzip_free(ZCode *s)
{
  ZCodeGzip *self = (ZCodeGzip *) s;

  if (self->encode) 
    deflateEnd(&self->gzip); 
  else 
    inflateEnd(&self->gzip);
}

/**
 * Finalize the transformation.
 *
 * @param[in] s ZCodeGzip instance.
 *
 * @returns TRUE if the transformation has consumed all data available
 *      (it should have by the end)
 **/
static gboolean
z_code_gzip_finish(ZCode *s)
{
  ZCodeGzip *self = (ZCodeGzip *) s;

  return self->gzip.avail_in == 0;
}


/**
 * Creates a new ZCodeGzip instance.
 *
 * @param[in] bufsize buffer size
 * @param[in] encode TRUE if encoding/compression is desired, FALSE if decoding/decompression is desired
 * @param[in] compress_level compression level (ignored for decoding)
 *
 * @returns ZCodeGzip instance
 **/
static ZCode *
z_code_gzip_init(gint bufsize, gboolean encode, gint compress_level)
{
  ZCodeGzip *self;

  z_enter();
  self = g_new0(ZCodeGzip, 1);
  z_code_init(&self->super, bufsize);
  self->super.transform = z_code_gzip_transform;
  self->super.finish = z_code_gzip_finish;
  self->super.free_fn = z_code_gzip_free;
  self->encode = encode;

  if (encode)
    g_assert(deflateInit(&self->gzip, compress_level) == Z_OK);
  else
    g_assert(inflateInit(&self->gzip) == Z_OK);
  z_leave();
  return &self->super;
}

/**
 * Creates a new ZCodeGzip instance set up for encoding/compression.
 *
 * @param[in] bufsize buffer size
 * @param[in] compress_level compression level (ignored for decoding)
 *
 * @returns ZCodeGzip instance
 **/
ZCode *
z_code_gzip_encode_new(gint bufsize, gint compress_level)
{
  return z_code_gzip_init(bufsize, TRUE, compress_level);
}

/**
 * Creates a new ZCodeGzip instance set up for decoding/decompression.
 *
 * @param[in] bufsize buffer size
 *
 * @returns ZCodeGzip instance
 **/
ZCode *
z_code_gzip_decode_new(gint bufsize)
{
  return z_code_gzip_init(bufsize, FALSE, 0);
}



