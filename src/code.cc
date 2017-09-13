// #include <zorpll/zorp.h>
#include <zorpll/log.h>

#include <zorpll/code.h>

#include <string.h>

/**
 * @file
 *
 * @todo
 * - use ZPacketBuffer as destination
 * - the possibility to fetch the results as an allocated memory block
 *   taking ownership (e.g. compress into ZCode's buffer and use it for
 *   other purposes without memory moves
 **/

/**
 * Resizes the buffer by a factor of n**2 until it reaches the desired size.
 *
 * @param[in] self ZCode instance
 * @param[in] reqlen requested buffer length
 *
 * @todo FIXME: no error handling yet
 *
 * @returns TRUE on success
 **/
gboolean
z_code_grow(ZCode *self, gint reqlen)
{
  gint    newlen;

  z_enter();
  newlen = self->buf_len;
  if (newlen <= 0)
    {
      newlen = 1;
    }
  
  while (newlen < reqlen)
    {
      newlen *= 2;
    }
  
  self->buf = static_cast<guchar *>(g_realloc(self->buf, newlen));
  self->buf_len = newlen;
  z_leave();
  return TRUE;
}

/**
 * Pushes back data to the buffer.
 *
 * @param[in] self this
 * @param[in] from source buffer
 * @param[in] fromlen number of bytes to push back to the buffer
 *
 * After this function runs, the data will be available for reading.
 *
 * @returns The number of bytes pushed back
 **/
void
z_code_unget_result(ZCode *self, const void *from, gsize fromlen)
{
  z_enter();
  if (fromlen > 0)
    {
      z_code_grow(self, self->buf_used + fromlen);
      g_memmove(self->buf + fromlen, self->buf, self->buf_used);
      g_memmove(self->buf, from, fromlen);
      self->buf_used += fromlen;
    }
  z_leave();
}

/**
 * Returns and removes a chunk of transformed bytes.
 *
 * @param[in] self this
 * @param[in] to destination buffer -- the results are returned here
 * @param[in] tolen length of the destination buffer
 *
 * @returns The number of bytes returned.
 **/
gsize
z_code_get_result(ZCode *self, void *to, gsize tolen)
{
  gsize res;
  
  z_enter();
  res = (tolen < self->buf_used) ? tolen : self->buf_used;
  if (res > 0)
    {
      z_log(NULL, CORE_DUMP, 8, "Reading ZCode data; len='%" G_GSIZE_FORMAT "', used='%" G_GSIZE_FORMAT "', partial='0x%02x'",
            res, self->buf_used, self->buf[self->buf_used]);
      z_log_data_dump(NULL, CORE_DEBUG, 8, self->buf, res);
      g_memmove(to, self->buf, res);
      self->buf_used -= res;
      g_memmove(self->buf, self->buf + res, self->buf_used + 1);
      z_log(NULL, CORE_DUMP, 8, "Remaining ZCode data; len='%" G_GSIZE_FORMAT "', used='%" G_GSIZE_FORMAT "', partial='0x%02x'",
            res, self->buf_used, self->buf[self->buf_used]);
    }
  z_leave();
  return res;
}

/**
 * Returns a pointer to the transformed data.
 *
 * @param[in] self this
 *
 * Returns a pointer to the transformed data (Non-destructive read
 * support). The length of the data returned by this function is
 * available by calling the z_code_get_result_length() function.
 *
 * @returns Pointer to the data
 **/
const void *
z_code_peek_result(ZCode *self)
{
  guchar *res;

  z_enter();
  res = self->buf;
  z_leave();
  return res;
}

/**
 * How much data is available in the internal buffer, e.g.\ how much
 * output the coder has generated so far.
 *
 * @param[in] self this
 *
 * @returns The amount of available data in bytes
 **/
gsize
z_code_get_result_length(ZCode *self)
{
  gsize res;

  z_enter();
  res = self->buf_used;
  z_leave();
  return res;
}

/**
 * Flush the specified number of bytes from the result buffer as if it
 * had been read using z_code_get_result()
 *
 * @param[in] self this
 * @param[in] flush_length the number of bytes to flush
 **/
void
z_code_flush_result(ZCode *self, gsize flush_length)
{
  if (flush_length == 0 || self->buf_used < flush_length)
    {
      self->buf_used = 0;
    }
  else if (self->buf_used >= flush_length)
    {
      g_memmove(self->buf, self->buf + flush_length, self->buf_used - flush_length);
      self->buf_used -= flush_length;
    }
}

/**
 * Initialize a caller-allocated ZCode structure with default values
 * for ZCode.
 *
 * @param[out] self ZCode instance
 * @param[in]  bufsize initial bufsize
 *
 * Usually used by actual ZCode implementations.
 **/
void
z_code_init(ZCode *self, gint bufsize)
{
  z_enter();
  self->buf_len = (bufsize <= 0) ? ZCODE_BUFSIZE_DEFAULT : bufsize;
  self->buf = g_new0(guchar, self->buf_len);
  self->buf_used = 0;
  self->error_counter = 0;
  z_leave();
}

/**
 * Constructor, creates a ZCode around the specified codec.
 *
 * @param[in] bufsize Initial buffer size. If this is <= 0, then
 * ZCODE_BUFSIZE_DEFAULT is used instead.
 *
 * @returns The new instance
 **/
ZCode*
z_code_new(gint bufsize)
{
  ZCode *self;

  z_enter();
  self = g_new0(ZCode, 1);
  z_code_init(self, bufsize);
  z_leave();
  return self;
}

/**
 * Free a ZCode instance by calling the virtual free_fn function.
 *
 * @param[in] self this
 **/
void
z_code_free(ZCode *self)
{
  z_enter();
  if (self)
    {
      if (self->free_fn)
        self->free_fn(self);
      g_free(self->buf);
      g_free(self);
    }
  z_leave();
}
