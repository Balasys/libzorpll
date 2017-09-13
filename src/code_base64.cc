// #include <zorpll/zorp.h>
#include <zorpll/log.h>

#include <zorpll/code_base64.h>

/**
 * ZCode-derived class to encode binary data to base64.
 **/
typedef struct ZCodeBase64Encode
{
  ZCode super;
  gint phase;
  gint linepos;
  gint linelen;
} ZCodeBase64Encode;

/**
 * ZCode-derived class to decode base64-encoded data to binary form.
 **/
typedef struct ZCodeBase64Decode
{
  ZCode super;
  gint phase;
  gboolean error_tolerant;
} ZCodeBase64Decode;


/**
 * @file
 * 
 * Base64: 3 x 8-bit -> 4 x 6-bit (in ascii representation)
 *
 * <pre>
 * |               |               |               |
 *  . . . . . . . . . . . . . . . . . . . . . . . . 
 * |           |           |           |           |
 * </pre>
 *
 * 6-bit values mapped to ascii:
 * <pre>
 * 0x00..0x19 -> 'A'..'Z' (0x41..0x5a)
 * 0x1a..0x33 -> 'a'..'z' (0x61..0x7a)
 * 0x34..0x3d -> '0'..'9' (0x30..0x39)
 *    0x3e    ->   '+'       (0x2b)
 *    0x3f    ->   '/'       (0x2f)
 * </pre>
 *
 * If the length of the binary isn't a multiplicate of 3, unspecified bits are
 * treated as 0 in the specified output bytes, the other output bytes (of the
 * last quadruple) are represented as '='.
 *
 * <pre>
 * (xxxxxxxx, yyyyyyyy, zzzzzzzz) -> (xxxxxx, xxyyyy, yyyyzz, zzzzzz)
 * (xxxxxxxx, yyyyyyyy), EOS      -> (xxxxxx, xxyyyy, yyyy00, '=')
 * (xxxxxxxx), EOS                -> (xxxxxx, xx0000, '=',    '=')
 * EOS                            -> nothing
 * </pre>
 *
 * Ascii output may be formatted to lines of any width, terminated by CRLF.
 *
 * During decoding spaces, tabs, CRs and LFs shall be ignored, any other char
 * may be treated as error or ignored as well.
 **/

/**
 * Used internally by z_code_base64_encode_finish() to encode the remaining bits
 * and add padding.
 *
 * @param[in] self ZCodeBase64Encode instance
 * @param[in] closure TRUE to write padding, FALSE to encode the remaining bits
 *
 * First it's called with closure=TRUE with the remaining bits written to the buffer
 * and the rest of the bits in that byte zeroed. Then it's called with closure=FALSE
 * as many times as many '='-s need to be written.
 *
 * Line breaks are written when necessary.
 **/
static void
z_code_base64_encode_fix(ZCodeBase64Encode *self, gboolean closure)
{
  static const char xlat[64] = 
    {
      'A', 'B', 'C', 'D',  'E', 'F', 'G', 'H',  'I', 'J', 'K', 'L',  'M', 'N', 'O', 'P',
      'Q', 'R', 'S', 'T',  'U' ,'V', 'W', 'X',  'Y', 'Z', 'a', 'b',  'c', 'd', 'e', 'f',
      'g', 'h', 'i', 'j',  'k', 'l', 'm', 'n',  'o', 'p', 'q', 'r',  's', 't', 'u', 'v',
      'w', 'x', 'y', 'z',  '0', '1', '2', '3',  '4', '5', '6', '7',  '8', '9', '+', '/'
    };

  self->super.buf[self->super.buf_used] = closure ? '=' : xlat[self->super.buf[self->super.buf_used] & 0x3f]; 
  self->super.buf_used++;
  if (self->linelen)
    {
      if (self->linepos++ >= self->linelen)
        {
          self->super.buf[self->super.buf_used++] = '\r';
          self->super.buf[self->super.buf_used++] = '\n';
          self->linepos = 0;
        }
    }
}

/**
 * Used internally to find out how big the buffer should be.
 *
 * @param[in] old_size size of data already encoded in the buffer
 * @param[in] orig_size size of new data to be encoded (before encoding)
 * @param[in] line_length length of lines in encoded output
 *
 * @returns the new size the buffer should be grown to
 **/
static inline gsize
z_code_calculate_growing(gsize old_size, gsize orig_size, gsize line_length)
{
  gsize new_size;
  gsize new_coded_size;
  
  new_size = old_size;                    /* original content length */
  new_coded_size = (orig_size*4 + 2) / 3; /* base64 create 4 octet from every 3 octet */
  new_size += new_coded_size;
  if (line_length)
    {
      new_size += 2*(new_size + line_length - 1) / line_length; /* Add newlines */
    }
  return new_size;
}

/**
 * binary -> base64 conversion
 *
 * @param[in] s this
 * @param[in] from_ source buffer
 * @param[in] fromlen source buffer length
 *
 * @returns Whether conversion succeeded
 **/
static gboolean
z_code_base64_encode_transform(ZCode *s, const void *from_, gsize fromlen)
{
  ZCodeBase64Encode *self = (ZCodeBase64Encode *) s;
  gsize pos, buf_used_orig;
  const guchar *from = static_cast<const guchar *>(from_);
  
  z_enter();
  z_code_grow((ZCode*)self, z_code_calculate_growing(self->super.buf_used, fromlen, self->linelen));
  
  /* This may allocate 4 excess bytes - but requires no further realloc()s.
   * Since the calculation uses buf_used, these 4 bytes won't accumulate, but
   * will be used in the future write()s, so the extra 4 is a grand total. */
 
  z_log(NULL, CORE_DUMP, 8, "Encoding base64 data; len='%" G_GSIZE_FORMAT"', phase='%d', used='%" G_GSIZE_FORMAT "', partial='0x%02x'",
        fromlen, self->phase, self->super.buf_used, self->super.buf[self->super.buf_used]);
  z_log_data_dump(NULL, CORE_DEBUG, 8, from, fromlen);

  buf_used_orig = self->super.buf_used;

  for (pos = 0; pos < fromlen; pos++)
    {
      switch (self->phase)
        {
        case 0: /* no previous partial content, (00.. ...., 00.. ....) -> (00xx xxxx, 00xx ....) */
          self->super.buf[self->super.buf_used] = from[pos] >> 2;
          z_code_base64_encode_fix(self, FALSE);
          self->super.buf[self->super.buf_used] = (from[pos] & 0x03) << 4;
          break;

        case 1: /* 2 upper bits already set, (00yy ...., 00.. ....) -> (00yy xxxx, 00xx xx..) */
          self->super.buf[self->super.buf_used] |= from[pos] >> 4;
          z_code_base64_encode_fix(self, FALSE);
          self->super.buf[self->super.buf_used] = (from[pos] & 0x0f) << 2;
          break;

        case 2: /* 4 upper bits already set, (00yy yy.., 00.. ....) -> (00yy yyxx, 00xx xxxx) */
          self->super.buf[self->super.buf_used] |= from[pos] >> 6;
          z_code_base64_encode_fix(self, FALSE);
          self->super.buf[self->super.buf_used] = from[pos] & 0x3f;
          z_code_base64_encode_fix(self, FALSE);
          break;
        }
      self->phase = (self->phase + 1) % 3;
    }
  z_log(NULL, CORE_DUMP, 8, "Encoded base64 data; len='%" G_GSIZE_FORMAT "', phase='%d', used='%" G_GSIZE_FORMAT "', partial='0x%02x'",
        self->super.buf_used - buf_used_orig, self->phase, self->super.buf_used, self->super.buf[self->super.buf_used]);
  z_log_data_dump(NULL, CORE_DEBUG, 8, self->super.buf + buf_used_orig, self->super.buf_used - buf_used_orig);

  z_leave();
  return TRUE;
}

/**
 * Finish encoding to Base64.
 *
 * @param[in] s ZCodeBase64Encode instance.
 *
 * Finishes encoding, including adding padding and newline if necessary.
 *
 * @returns always TRUE
 **/
static gboolean
z_code_base64_encode_finish(ZCode *s)
{
  ZCodeBase64Encode *self = (ZCodeBase64Encode *) s;

  z_enter();
  switch (self->phase)
    {
    case 0: /* no previous partial content, (00.. ....) -> nothing */
      break;

    case 1: /* upper 2 bits already set, (00yy ....) -> (00yy 0000)== */
      self->super.buf[self->super.buf_used] &= 0x30;
      z_code_base64_encode_fix(self, FALSE);
      z_code_base64_encode_fix(self, TRUE);
      z_code_base64_encode_fix(self, TRUE);
      break;

    case 2: /* upper 4 bits already set, (00yy yy..) -> (00yy yy00)= */
      self->super.buf[self->super.buf_used] &= 0x3c;
      z_code_base64_encode_fix(self, FALSE);
      z_code_base64_encode_fix(self, TRUE);
      break;
    }
  if (self->linepos != 0)
    {
      self->super.buf[self->super.buf_used++] = '\r';
      self->super.buf[self->super.buf_used++] = '\n';
    }
  self->linepos = 0;
  self->phase = 0;
  z_leave();
  return TRUE;
}

/**
 * Initialize ZCodeBase64Encode instance.
 *
 * @param[in] self ZCodeBase64Encode instance
 * @param[in] bufsize initial buffer size
 * @param[in] linelen line length in output
 **/
static void
z_code_base64_encode_init(ZCodeBase64Encode *self, gint bufsize, gint linelen)
{
  z_enter();
  z_code_init(&self->super, bufsize);
  self->super.transform = z_code_base64_encode_transform;
  self->super.finish = z_code_base64_encode_finish;
  self->phase = 0;
  self->linepos = 0;
  self->linelen = linelen;
  z_leave();
}

/**
 * Create a new ZCodeBase64Encode instance.
 *
 * @param[in]      bufsize initial buffer size
 * @param[in]      linelen line length in output
 * 
 * @returns new object
 **/
ZCode*
z_code_base64_encode_new(gint bufsize, gint linelen)
{
  ZCodeBase64Encode *self;

  z_enter();
  self = g_new0(ZCodeBase64Encode, 1);
  z_code_base64_encode_init(self, bufsize, linelen);
  z_leave();
  return &self->super;
}

/* ---------------------------------------------------------------------- */

/**
 * base64 -> binary conversion
 *
 * @param[in] s this
 * @param[in] from_ source buffer
 * @param[in] fromlen source buffer length
 *
 * @returns FALSE if it aborted because of an error (if self->error_tolerant is unset); TRUE otherwise.
 **/
static gboolean
z_code_base64_decode_transform(ZCode *s, const void *from_, gsize fromlen)
{
  ZCodeBase64Decode *self = (ZCodeBase64Decode *) s;
  static const int  xlat[256] =  /* -3=error, -2=end of stream, -1=ignore, other=value */
    {   -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -1,   -1,   -3,    -3,   -1,   -3,   -3, /* 0x00 - 0x0f */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0x10 - 0x1f */
        -1,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3, 0x3e,    -3,   -3,   -3, 0x3f, /* 0x20 - 0x2f */
      0x34, 0x35, 0x36, 0x37,  0x38, 0x39, 0x3a, 0x3b,  0x3c, 0x3d,   -3,   -3,    -3,   -2,   -3,   -3, /* 0x30 - 0x3f */
        -3, 0x00, 0x01, 0x02,  0x03, 0x04, 0x05, 0x06,  0x07, 0x08, 0x09, 0x0a,  0x0b, 0x0c, 0x0d, 0x0e, /* 0x40 - 0x4f */
      0x0f, 0x10, 0x11, 0x12,  0x13, 0x14, 0x15, 0x16,  0x17, 0x18, 0x19,   -3,    -3,   -3,   -3,   -3, /* 0x50 - 0x5f */
        -3, 0x1a, 0x1b, 0x1c,  0x1d, 0x1e, 0x1f, 0x20,  0x21, 0x22, 0x23, 0x24,  0x25, 0x26, 0x27, 0x28, /* 0x60 - 0x6f */
      0x29, 0x2a, 0x2b, 0x2c,  0x2d, 0x2e, 0x2f, 0x30,  0x31, 0x32, 0x33,   -3,    -3,   -3,   -3,   -3, /* 0x70 - 0x7f */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0x80 - 0x8f */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0x90 - 0x9f */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0xa0 - 0xaf */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0xb0 - 0xbf */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0xc0 - 0xcf */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0xd0 - 0xdf */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3, /* 0xe0 - 0xef */
        -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3,    -3,   -3,   -3,   -3  /* 0xf0 - 0xff */
    };
  
  gsize pos, buf_used_orig;
  gint value;
  const guchar *from = static_cast<const guchar *>(from_);

  z_enter();
  
  z_code_grow(s, self->super.buf_used + /* original content */
                 ((fromlen*3 + 3) / 4) + /* resulting data */
                 16);
  /* This may allocate an excess of 3 bytes plus 0.75 for each non-base64
   * character, but they won't accumulate, see z_code_base64_write() for
   * details. */
  
  z_log(NULL, CORE_DUMP, 8, "Decoding base64 data; len='%" G_GSIZE_FORMAT "'", fromlen);
  z_log_data_dump(NULL, CORE_DEBUG, 8, from, fromlen);
        
  buf_used_orig = self->super.buf_used;
    
  value = -1;
  for (pos = 0; pos < fromlen; pos++)
    {
      value = xlat[(guchar)from[pos]];
      if (value == -1)        /* ignore */
        continue;
      else if (value == -2)   /* end of stream */
        {
          switch (self->phase)
            {
            case 0: /* (=...) may not happen */
            case 1: /* (x=..) may not happen */
              z_log(NULL, CORE_ERROR, 3, "Base64 closing character in illegal phase; phase='%d', pos='0x%06" G_GSIZE_MODIFIER "x'", self->phase, pos);
              if (!self->error_tolerant)
                {
                  self->super.error_counter++;
                  z_leave();
                  return FALSE;
                }
              break;
              
            case 2: /* (..=.) */
              self->phase = 4;
              break;

            case 3: /* (...=) */
            case 4: /* (..==) */
              self->phase = 0;
              break;
            }
          continue;
        }
      else if ((value < 0) || (0x3f < value))     /* invalid char */
        {
          z_log(NULL, CORE_ERROR, 3, "Illegal base64 character; char='%c', pos='0x%06" G_GSIZE_MODIFIER "x'", from[pos], pos);
          if (self->error_tolerant)
            {
              continue;
            }
          else
            {
              self->super.error_counter++;
              z_leave();
              return FALSE;
            }
        }

      if (self->phase == 4) /* special case: (yy=) already happened */
        {
          z_log(NULL, CORE_ERROR, 3, "Base64 character in closing phase; char='%c', pos='0x%06" G_GSIZE_MODIFIER "x'", from[pos], pos);
          if (self->error_tolerant)
            {
              self->phase = 0; /* reset and try to go on */
            }
          else
            {
              self->super.error_counter++;
              z_leave();
              return FALSE;
            }
        }

      switch (self->phase)
        {
        case 0: /* no previous partial content, (.... ....) -> (xxxx xx..) */
          self->super.buf[self->super.buf_used] = value << 2;
          break;

        case 1: /* upper 6 bits already set, (yyyy yy.., .... ....) -> (yyyy yyxx, xxxx ....) */
          self->super.buf[self->super.buf_used++] |= value >> 4;
          self->super.buf[self->super.buf_used] = value << 4;
          break;

        case 2: /* upper 4 bits already set, (yyyy ...., .... ....) -> (yyyy xxxx, xx.. ....) */
          self->super.buf[self->super.buf_used++] |= value >> 2;
          self->super.buf[self->super.buf_used] = value << 6;
          break;

        case 3: /* upper 2 bits already set, (yy.. ....) -> (yyxx xxxx) */
          self->super.buf[self->super.buf_used++] |= value;
          break;
        }
      self->phase = (self->phase + 1) % 4;
    }
  
  z_log(NULL, CORE_DUMP, 8, "Decoded base64 data; len='%" G_GSIZE_FORMAT "'", self->super.buf_used - buf_used_orig);
  z_log_data_dump(NULL, CORE_DEBUG, 8, self->super.buf + buf_used_orig, self->super.buf_used - buf_used_orig);

  z_leave();
  return TRUE;
}

/**
 * Finalize Base64 decoding.
 *
 * @param[in] s ZCodeBase64Decode instance
 *
 * Checks if the input was complete (if so, we returned to phase 0).
 * If it wasn't, the error is logged; and if we're not error_tolerant,
 * the return value also indicates this error condition.
 *
 * @return always TRUE if error_tolerant; otherwise FALSE if the input was incomplete
 **/
static gboolean
z_code_base64_decode_finish(ZCode *s)
{
  ZCodeBase64Decode *self = (ZCodeBase64Decode *) s;
  
  z_enter();
  if (self->phase != 0)
    {
      z_log(NULL, CORE_ERROR, 3, "Unfinished base64 encoding; phase='%d'", self->phase);
      self->phase = 0;
      if (!self->error_tolerant)
        {
          z_leave();
          return FALSE;
        }
    }

  z_leave();
  return TRUE;
}

/**
 * Initialize ZCodeBase64Decode instance.
 *
 * @param[in] self ZCodeBase64Decode instance
 * @param[in] bufsize initial buffer size
 * @param[in] error_tolerant whether to be tolerant of errors in input
 **/
static void
z_code_base64_decode_init(ZCodeBase64Decode *self, gint bufsize, gboolean error_tolerant)
{
  z_enter();
  z_code_init(&self->super, bufsize);
  self->super.transform = z_code_base64_decode_transform;
  self->super.finish = z_code_base64_decode_finish;
  self->phase = 0;
  self->error_tolerant = error_tolerant;
  z_leave();
}

/**
 * Create a new ZCodeBase64Decode instance.
 *
 * @param[in]      bufsize initial buffer size
 * @param[in]      error_tolerant whether to be tolerant of errors in input
 *
 * @returns new object
 **/
ZCode *
z_code_base64_decode_new(gint bufsize, gboolean error_tolerant)
{
  ZCodeBase64Decode *self;

  z_enter();
  self = g_new0(ZCodeBase64Decode, 1);
  z_code_base64_decode_init(self, bufsize, error_tolerant);
  z_leave();
  return &self->super;
}

