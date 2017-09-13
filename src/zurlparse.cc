#include <zorpll/zurlparse.h>

#include <ctype.h>
#include <stdlib.h>

/**
 * z_url_xdigit_value:
 * @c: possible hexadecimal character
 *
 * Return the hexadecimal value of @c or return -1 if not a hexadecimal character.
 **/
static inline gint 
z_url_xdigit_value(char c)
{
  c = tolower(c);
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

/**
 * z_url_decode_hex_byte:
 * @dst: store decoded value here
 * @src: read hexadecimal numbers from here
 * @reason: error reason text if the operation fails
 *
 * Convert a hexadecimal encoded byte to the equivalent value.
 **/
static inline gboolean
z_url_decode_hex_byte(guchar *dst, const gchar *src, GError **error)
{
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  if (isxdigit(*src) && isxdigit(*(src+1)))
    {
      *dst = (z_url_xdigit_value(*src) << 4) + z_url_xdigit_value(*(src+1));
    }
  else
    {
      g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Invalid hexadecimal encoding");
      return FALSE;
    }
  return TRUE;
}

/**
 * g_string_assign_url_decode:
 * @part: store the decoded string here
 * @permit_invalid_hex_escape: whether to treat invalid url encoding sequences as errors
 * @src: source string to decode
 * @len: length of string pointed to by @src
 * @reason: terror reason text if the operation fails
 *
 * Decodes an URL part such as username, password or host name. Assumes
 * single byte destination encoding, e.g. US-ASCII with the 128-255 range
 * defined.
 **/
static gboolean
g_string_assign_url_decode(GString *part, const gchar *src, gint len, GError **error)
{
  gchar *dst;
  gint left = len;

  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
  
  /* url decoding shrinks the string, using len is a good bet */
  g_string_set_size(part, len);
  dst = part->str;
  while (left)
    {
      guchar c = (guchar) *src;
      
      if (*src == '%')
        {
          if (left < 2 || !z_url_decode_hex_byte(&c, src+1, error))
            {
              g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Hexadecimal encoding too short");
              return FALSE;
            }
          else
            {
              src += 2;
              left -= 2;
            }
        }
      *dst = c;
      dst++; 
      src++;
      left--;
    }
  *dst = 0;
  part->len = dst - part->str;
  /* some space might still be allocated at the end of the string 
   * but we don't care to avoid reallocing and possible data copy */
  return TRUE;
}


/**
 * z_url_parse:
 * @url: store URL parts to this structure
 * @permit_unicode_url: permit IIS style unicode character encoding
 * @permit_invalid_hex_escape: permit invalid hexadecimal escaping, treat % in these cases literally
 * @url_str: URL to parse
 * @reason: parse error 
 *
 * Parse the URL specified in @url_str and store the resulting parts in
 * @url. Scheme, username, password, hostname and filename are stored in
 * decoded form (UTF8 in permit_unicode_url case), query and fragment are
 * stored in URL encoded, but canonicalized form.
 *
 * Returns: TRUE for success, FALSE otherwise setting @reason to the explanation
 **/
gboolean
z_url_parse(ZURL *url, const gchar *url_str, GError **error)
{
  const gchar *p, *part[4], *sep[4], *file_start;
  gchar *end;
  int i;

  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  g_string_truncate(url->scheme, 0);
  g_string_truncate(url->user, 0);
  g_string_truncate(url->passwd, 0);
  g_string_truncate(url->host, 0);
  g_string_truncate(url->file, 0);
  url->port = 0;

  p = url_str;
  while (*p && *p != ':')
    p++;
  if (!*p)
    {
      g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "URL has no scheme, colon missing");
      return FALSE;
    }
  if (*(p + 1) != '/' || *(p + 2) != '/')
    {
      /* protocol not terminated by '//' */
      g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Scheme not followed by '//'");
      return FALSE;
    }
  g_string_assign_len(url->scheme, url_str, p - url_str);
  p += 3;

  for (i = 0; i < 4; i++)
    {
      part[i] = p;
      while (*p && *p != ':' && *p != '/' && *p != '@' && *p != '?' && *p != '#')
        p++;
      sep[i] = p;
      if (!*p || *p == '/')
        break;
      p++;
    }
  switch (i)
    {
    case 0:
      /* hostname only */
      if (!g_string_assign_url_decode(url->host, part[0], sep[0] - part[0], error))
        return FALSE;

      break;

    case 1:
      /* username && host || hostname && port number */
      if (*sep[0] == ':')
        {
          if (!g_string_assign_url_decode(url->host, part[0], sep[0] - part[0], error))
            return FALSE;
          /* port number */
          url->port = strtoul(part[1], &end, 10);
          if (end != sep[1])
            {
              g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Error parsing port number");
              return FALSE;
            }
        }
      else if (*sep[0] == '@')
        {
          /* username */
          if (!g_string_assign_url_decode(url->user, part[0], sep[0] - part[0], error) ||
              !g_string_assign_url_decode(url->host, part[1], sep[1] - part[1], error))
            return FALSE;
        }
      else
        {
          g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Unrecognized URL construct");
          return FALSE;
        }
      break;
    case 2:
      /* username && host && port || username && password && host */
      if (*sep[0] == '@' && *sep[1] == ':')
        {
          /* username, host, port */
          if (!g_string_assign_url_decode(url->user, part[0], sep[0] - part[0], error) ||
              !g_string_assign_url_decode(url->host, part[1], sep[1] - part[1], error))
            return FALSE;
          url->port = strtoul(part[2], &end, 10);
          if (end != sep[2])
            {
              g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Error parsing port number");
              return FALSE;
            }
        }
      else if (*sep[0] == ':' && *sep[1] == '@')
        {
          /* username, password, host */
          if (!g_string_assign_url_decode(url->user, part[0], sep[0] - part[0], error) ||
              !g_string_assign_url_decode(url->passwd, part[1], sep[1] - part[1], error) ||
              !g_string_assign_url_decode(url->host, part[2], sep[2] - part[2], error))
            return FALSE;
        }
      else
        {
          g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Unrecognized URL construct");
          return FALSE;
        }
      break;

    case 3:
      /* username && password && hostname && port */
      if (*sep[0] == ':' && *sep[1] == '@' && *sep[2] == ':')
        {
          if (!g_string_assign_url_decode(url->user, part[0], sep[0] - part[0], error) ||
              !g_string_assign_url_decode(url->passwd, part[1], sep[1] - part[1], error) ||
              !g_string_assign_url_decode(url->host, part[2], sep[2] - part[2], error))
            return FALSE;
          url->port = strtoul(part[3], &end, 10);
          if (end != sep[3])
            {
              g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Error parsing port number");
              return FALSE;
            }
        }
      else
        {
          g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Unrecognized URL construct");
          return FALSE;
        }
      break;

    default:
      g_assert_not_reached();
    }

  file_start = p;
  if (*file_start != '/')
    {
      if (*file_start == '\0')
        {
          g_string_assign(url->file, "/");
          return TRUE;
        }
      g_set_error(error, ZURL_ERROR, ZURL_ERROR_FAILED, "Invalid path component in URL");
      return FALSE;
    }
  g_string_assign(url->file, file_start);    
  return TRUE;
}

void 
z_url_init(ZURL *self)
{
  self->scheme = g_string_sized_new(0);
  self->user = g_string_sized_new(0);
  self->passwd = g_string_sized_new(0);
  self->host = g_string_sized_new(0);
  self->port = 0;
  self->file = g_string_sized_new(0);
}

void 
z_url_free(ZURL *self)
{
  g_string_free(self->scheme, TRUE);
  g_string_free(self->user, TRUE);
  g_string_free(self->passwd, TRUE);
  g_string_free(self->host, TRUE);
  g_string_free(self->file, TRUE);
}

GQuark 
z_url_error_quark(void)
{
  return g_quark_from_static_string("gurl-error-quark");
}

