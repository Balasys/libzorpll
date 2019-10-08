/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <zorpll/zorplib.h>
#include <zorpll/misc.h>
#include <zorpll/log.h>
#include <zorpll/error.h>

#include <string.h>
#include <stdlib.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#define PARSE_STATE_START  0
#define PARSE_STATE_DASH   1
#define PARSE_STATE_END    2
#define PARSE_STATE_ESCAPE 3

/**
 * Initialize a ZCharSet by setting clearing the character set bitstring.
 *
 * @param[out] self ZCharSet instance to initialize
 **/
void 
z_charset_init(ZCharSet *self)
{
  memset(self, 0, sizeof(*self));
}

/**
 * This function parses an character set from its string representation.
 *
 * @param[in] self ZCharSet instance, previously initialized by z_charset_init
 * @param[in] interval_str string representation of the character set
 *
 * @returns true if interval_str parsed correctly (the parser didn't get into a
 *      nonexistent state and the parser returned to its starting state at the end)
 **/
gboolean 
z_charset_parse(ZCharSet *self, gchar *interval_str)
{
  guint i = 0;
  guchar j;
  guint state = PARSE_STATE_START;
  guint old_state = PARSE_STATE_START;
  guchar start_pos = 0;
  guchar end_pos = 0;
  
  z_enter();
  while (interval_str[i])
    {
      switch (state)
        {
        case PARSE_STATE_START:
          if (interval_str[i] == '\\' && old_state != PARSE_STATE_ESCAPE)
            {
              z_cp();
              old_state = state;
              state = PARSE_STATE_ESCAPE;
            }
          else
            {
              z_cp();
              start_pos = interval_str[i];
              state = PARSE_STATE_DASH;
              old_state = PARSE_STATE_START;
              i++;
            }
          break;
          
        case PARSE_STATE_DASH:
          if (interval_str[i] == '\\' && old_state != PARSE_STATE_ESCAPE)
            {
              z_cp();
              state = PARSE_STATE_END;
              i--;
            }
          else
            {
              z_cp();
              state = PARSE_STATE_END;
              old_state = PARSE_STATE_DASH;
              if (interval_str[i] == '-')
                i++;
              else
                i--;
            }
          break;
          
        case PARSE_STATE_END:
          if (interval_str[i] == '\\' && old_state != PARSE_STATE_ESCAPE)
            {
              z_cp();
              old_state = state;
              state = PARSE_STATE_ESCAPE;
            }
          else
            {
              z_cp();
              end_pos = interval_str[i];
              for (j = start_pos; j <= end_pos; j++)
                z_charset_enable(self, j);
              
              i++;
              state = PARSE_STATE_START;
              old_state = PARSE_STATE_END;
            }
          break;
          
        case PARSE_STATE_ESCAPE:
          z_cp();
          i++;
          state = old_state;
          old_state = PARSE_STATE_ESCAPE;
          break;

        default:
          z_return(FALSE);
        }
    }

  if (state == PARSE_STATE_DASH)
    {
      z_cp();
      z_charset_enable(self, start_pos);
      state = PARSE_STATE_START;
    }
  
  z_return(state == PARSE_STATE_START);
}

/**
 * This function checks whether the given string contains valid characters only.
 *
 * @param[in] self ZCharSet instance
 * @param[in] str string to check
 * @param[in] len string length
 *
 * @returns if the string contains valid characters only
 **/
gboolean 
z_charset_is_string_valid(ZCharSet *self, gchar *str, gint len)
{
  gint i;
  
  if (len < 0)
    len = strlen(str);
  
  for (i = 0; i < len; i++)
    {
      if (!z_charset_is_enabled(self, str[i]))
        return FALSE;
    }
  return TRUE;
}

/**
 * This function assigns the given string/length value to the specified GString.
 *
 * @param[in] s GString instance
 * @param[in] val string pointer
 * @param[in] len length of string
 *
 * This function should be defined in GLib, however no such function exists.
 *
 * @returns the GString instance 
 **/
GString *
g_string_assign_len(GString *s, const gchar *val, gint len)
{
  g_string_truncate(s, 0);
  if (val && len)
    g_string_append_len(s, val, len);
  return s;
}

/**
 * @brief Assign a string from the specified GString.
 * @param target destination string
 * @param source source string
 * @return the GString instance (same as target)
 */
GString *
g_string_assign_gstring(GString *target, const GString *source)
{
  return g_string_assign_len(target, source->str, source->len);
}

#if !GLIB_CHECK_VERSION(2, 28, 0)
static void
z_misc_g_list_free_full_foreach_helper(gpointer data, gpointer user_data)
{
  GDestroyNotify free_func = (GDestroyNotify) user_data;
  free_func(data);
}

void
g_list_free_full(GList *list, GDestroyNotify free_func)
{
  g_list_foreach(list, z_misc_g_list_free_full_foreach_helper, free_func);
  g_list_free(list);
}
#endif

/**
 * Compares t1 and t2
 *
 * @param[in] t1 time value t1
 * @param[in] t2 time value t2
 *
 * @returns -1 if t1 is earlier than t2
 * @returns 0 if t1 equals t2
 * @returns 1 if t1 is later than t2
 **/
gint
g_time_val_compare(const GTimeVal *t1, const GTimeVal *t2)
{
  g_assert(t1);
  g_assert(t2);
  if (t1->tv_sec < t2->tv_sec)
    return -1;
  else if (t1->tv_sec > t2->tv_sec)
    return 1;
  else if (t1->tv_usec < t2->tv_usec)
    return -1;
  else if (t1->tv_usec > t2->tv_usec)
    return 1;
  return 0;
}

/**
 * Calculates the time difference between t1 and t2 in microseconds.
 *
 * @param[in] t1 time value t1
 * @param[in] t2 time value t2
 *
 * The result is positive if t1 is later than t2.
 *
 * @returns Time difference in microseconds
 **/
glong
g_time_val_diff(const GTimeVal *t1, const GTimeVal *t2)
{
  g_assert(t1);
  g_assert(t2);
  return (t1->tv_sec - t2->tv_sec) * G_USEC_PER_SEC + (t1->tv_usec - t2->tv_usec);
}

/**
 * g_timeval_subtract:
 * @result: the value of x - y
 * @x:
 * @y:
 *
 * Subtract y from x. The value of x is always greater or equals to y.
 * The result is non-negative.
 *
 * It cannot be used to calculate a negative result.
 *
 */
void
g_time_val_subtract(GTimeVal *result, const GTimeVal *x, const GTimeVal *y)
{
  result->tv_usec = x->tv_usec;
  result->tv_sec = x->tv_sec;

  if (x->tv_usec < y->tv_usec)
    {
      result->tv_usec += 1000000;
      result->tv_sec -= 1;
    }
  result->tv_usec -= y->tv_usec;
  result->tv_sec -= y->tv_sec;
}

/**
 * Produces one line of hex dump of the specified part buf in line.
 *
 * @param[out] line where the result will appear
 * @param[in]  linelen allocated length of line
 * @param[in]  i index in buf to dump from
 * @param[in]  buf raw binary data to dump
 * @param[in]  len length of buf
 *
 * This function will hexdump up to 16 bytes of buf on one line.
 * This will be followed with a dump of the characters where
 * unprintable characters will be replaced with '.'
 *
 * @returns The number of characters that were actually dumped on this line
 **/
static guint
z_hexdump(gchar *line, guint linelen, guint i, const char *buf, guint len)
{
  guint j;
  char *end = line;

  for (j = 0; j < 16 && (i + j < len); j++)
    {
      g_snprintf(end, linelen - (end - line), "%02X ", (unsigned char) buf[i+j]);
      end += 3;
    }
  for (; j < 16; j++)
    {
      g_snprintf(end, linelen - (end - line), "   ");
      end += 3;
    }
  g_snprintf(end, linelen - (end - line), " ");
  end++;
  for (j = 0; j < 16 && (i + j < len) && linelen > (guint)(end - line); j++)
    {
      *end = isprint(buf[i + j]) ? buf[i + j] : '.';
      end++;
    }
  *end='\0';

  return j;
}


/**
 * This function generates hexdumps of the specified buffer to the system
 * log using z_log().
 *
 * @param[in] session_id session id to be used for the log messages
 * @param[in] klass log message class
 * @param[in] level log message verbosity level
 * @param[in] buf buffer
 * @param[in] len buffer length
 *
 * Used internally by z_log_data_dump().
 **/
void 
z_format_data_dump(const gchar *session_id, const char *klass, gint level, const void *buf, guint len)
{
  guint i, offs;
  gchar line[1024];
  
  i = 0;
  while (i < len)
    {
      offs = i;
      i += z_hexdump(line, sizeof(line), i, static_cast<const char *>(buf), len);
      /*NOLOG*/
      z_log((gchar *)session_id, klass, level, "data line 0x%04x: %s", offs, line);
    }
}

/**
 * This function generates textual dumps of the specified buffer to the system
 * log using z_log().
 *
 * @param[in] session_id session id to be used for the log messages
 * @param[in] klass log message class
 * @param[in] level log message verbosity level
 * @param[in] buf buffer
 * @param[in] len buffer length
 *
 * Used internally by z_log_text_dump().
 **/
void 
z_format_text_dump(const gchar *session_id, const char *klass, gint level, const void *buf, guint len)
{
  guint i, nl;
  gchar line[1024];
  const gchar *bufc = static_cast<const gchar *>(buf);
 
  while (len > 0)
    {
      for (nl = 0; (nl < len) && bufc[nl] && (bufc[nl] != '\r') && (bufc[nl] != '\n'); nl++)
        ;
      i = (nl < len) ? nl : nl - 1; /* # of chars to dump */
      if (i >= sizeof(line))
        i = sizeof(line) - 1;
      memcpy(line, bufc, i);
      line[i] = '\0';
      /*NOLOG*/
      z_log((gchar *)session_id, klass, level, "text line: %s", line);
      bufc += nl;
      len -= nl;
      if ((len > 0) && (bufc[0] == '\r'))
        {
          bufc++;
          len--;
        }
      if ((len > 0) && (bufc[0] == '\n'))
        {
          bufc++;
          len--;
        }
    }
}

/**
 * Escapes spaces to %_ and %-s to %% in s and returns the result.
 *
 * @param[in] s the string to escape characters in
 * @param[in] len length of s
 *
 * @returns the escaped string
 **/
gchar *
z_str_escape(const gchar *s, gint len)
{
  gchar *res;
  gint i = 0, j = 0;;
  
  z_enter();
  if (len < 0)
    len = strlen(s) + 1;
  res = g_new0(gchar, len * 2);
  while (i < len && s[i] != '\0')
    {
      switch (s[i])
        {
	  case ' ':
	    res[j++] = '%';
            res[j++] = '_';
	    break;
          
	  case '%':
	    res[j++] = '%';
	    res[j++] = '%';
	    break;
	  
	  default:
	    res[j++] = s[i];
	}
      i++;
    }
  z_return(res);
}

/**
 * Undoes the escaping done by z_str_escape() on s and returns the result.
 *
 * @param[in] s the string to unescape characters in
 * @param[in] len length of s
 *
 * @returns the compressed string
 **/
gchar *
z_str_compress(const gchar *s, gint len)
{
  gchar *res;
  gint i = 0, j = 0;;
  
  z_enter();
  if (len < 0)
    len = strlen(s) + 1;
  res = g_new0(gchar, len);
  while (i < len && s[i] != '\0')
    {
      if (s[i] == '%' && s[i+1] == '%')
        {
	  i++;
	  res[j++] = '%';
	}
      else if (s[i] == '%' && s[i+1] == '_')
        {
	  i++;
	  res[j++] = ' ';
	}
      else
        {
	  res[j++] = s[i];
	}
      i++;
    }
  z_return(res);
}

/**
 * Parse port range and check if the port number is valid.
 *
 * @param[in] port_range port range specification
 * @param[in] port port number to check
 *
 * This function parses the given port_range and returns TRUE to indicate
 * whether the specified port number is valid. The port specification is in
 * the format: port1[-port2]([,port1[-port2]])*
 *
 * @returns TRUE when the port number is valid
 **/
gboolean
z_port_enabled(gchar *port_range, guint port)
{
  long int portl, porth;
  gchar *tmp;
  gchar *err;
  
  if (strlen(port_range) == 0 )
    return FALSE;
  
  tmp = port_range;
  while (*tmp)
    {
      portl = strtol(tmp, &err, 10);
      tmp = err;
      if (*tmp == '-')
        {
          porth = strtol(tmp + 1, &err, 10);
          tmp = err;
        }
      else
        {
          porth = portl;
        }

      if (*tmp != 0 &&  *tmp != ',')
        return FALSE;
    
      if (*tmp)
        {
          tmp++;
          if (*tmp <= '0' && *tmp >= '9')
          return FALSE;
        }
        
      if ( portl <= (long int)port && (long int)port <= porth )
        return TRUE;

    }
  return FALSE;
}

/**
 * GLib hashtable hash function for case insensitive hashtables.
 *
 * @param[in] key
 **/
guint
z_casestr_hash(gconstpointer key)
{
  const char *p = static_cast<const char *>(key);
  guint h = toupper(*p);

  if (h)
    for (p += 1; *p != '\0'; p++)
      h = (h << 5) - h + toupper(*p);

  return h;
}

/**
 * This function returns a static character string which includes version
 * information and compilation settings. This function is _NOT_ reentrant.
 *
 * @returns a static string of the version information to be displayed to
 * the user
 **/
const gchar *
z_libzorpll_version_info(void)
{
  static gchar buf[512];
  
  g_snprintf(buf, sizeof(buf),
             "libzorpll %s\n"
             "Revision: %s\n"
             "Trace: %s\n"
             "MemTrace: %s\n"
             "Caps: %s\n"
             "Debug: %s\n"
             "StackDump: %s\n",

             ZORPLIBLL_VERSION, ZORPLIBLL_REVISION,
             ON_OFF_STR(ZORPLIB_ENABLE_TRACE),
             ON_OFF_STR(ZORPLIB_ENABLE_MEM_TRACE),
             ON_OFF_STR(ZORPLIB_ENABLE_CAPS),
             ON_OFF_STR(ZORPLIB_ENABLE_DEBUG),
             ON_OFF_STR(ZORPLIB_ENABLE_STACKDUMP));
  return buf;
}

/* NOTE: we deliberately do not define this function in a header file as we don't want these public */
void z_thread_add_option_group(GOptionContext *ctx);
void z_process_add_option_group(GOptionContext *ctx);
void z_log_add_option_group(GOptionContext *ctx);

/**
 * This function put the library specific options to the program
 * option context.
 *
 * @param[in] ctx A #GoptionContext container for the options
 * @param[in] disable_groups Exclude these groups.
 **/
void
z_libzorpll_add_option_groups(GOptionContext *ctx, guint disable_groups)
{
#ifndef G_OS_WIN32  
  /* we don't have process API on Windows */
  if ((disable_groups & Z_OG_PROCESS) == 0)
    z_process_add_option_group(ctx);
#endif

  if ((disable_groups & Z_OG_THREAD) == 0)
    z_thread_add_option_group(ctx);

  if ((disable_groups & Z_OG_LOG) == 0)
    z_log_add_option_group(ctx);
}

/**
 * localtime_r replacement in case we don't have one.
 *
 * @param[in]  timep time in seconds since epoch form to convert
 * @param[out] result tm struct to convert date/timestamp to
 *
 * localtime_r is implemented by locking and calling localtime.
 *
 * @returns a pointer to result
 **/
struct tm *
z_localtime_r(const time_t *timep, struct tm *result)
{
#ifdef HAVE_LOCALTIME_R
  return localtime_r(timep, result);
#else
  static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

  g_static_mutex_lock (&mutex);
  memcpy(result, localtime(timep), sizeof(struct tm));
  g_static_mutex_unlock (&mutex);
  return result;
#endif
}

// FreeBSD 8.0 or later (https://www.freebsd.org/cgi/man.cgi?query=closefrom)
// NetBSD 3.0 or later (http://netbsd.gw.com/cgi-bin/man-cgi?closefrom++NetBSD-3.0)
// OpenBSD 3.5 or later (https://man.openbsd.org/closefrom.2)
// Solaris (https://docs.oracle.com/cd/E86824_01/html/E54766/closefrom-3c.html)
#ifdef HAVE_CLOSEFROM
int
z_closefrom(const int lowfd)
{
  closefrom(lowfd);
  z_log(nullptr, CORE_DEBUG, 6, "File descriptors successfully closed; lowfd='%d'", lowfd);

  return 0;
}

// AIX 7.1 or later (https://www.ibm.com/support/knowledgecenter/en/ssw_aix_71/com.ibm.aix.base/whatsnew.htm)
// IRIX (http://polarhome.com/service/man/?qf=fcntl&tf=2&of=IRIX&sf=2)
#elif HAVE_FCNTL_CLOSEM
int
z_closefrom(const int lowfd)
{
  int ret = 0;

  if ((ret = fcntl(lowfd, F_CLOSEM, 0)) < 0)
      z_log(nullptr, CORE_ERROR, 2, "Failed to call fcntl for F_CLOSEM; error='%s'", g_strerror(z_errno_get()));
  else
      z_log(nullptr, CORE_DEBUG, 6, "File descriptors successfully closed; lowfd='%d'", lowfd);

  return ret;
}

// Since Linux do not have closefrom() or F_CLOSEM we use the /proc directly
// https://www.kernel.org/doc/Documentation/filesystems/proc.txt
#elif __linux__
int
z_closefrom(const int lowfd)
{
  int ret = 0;
  std::int64_t maxfd = 0;
  const char *fd_path = "/proc/self/fd";
  DIR *dir = opendir(fd_path);
  if (dir)
    {
      dirent *entry = nullptr;
      while ((entry = readdir(dir)) != nullptr)
        {
          if (!(entry->d_type & DT_DIR))
            {
              char *end = nullptr;
              const long fd = std::strtol(entry->d_name, &end, 10);
              if (entry->d_name != end &&
                  *end == '\0' &&
                  fd >= 0 &&
                  fd < INT_MAX &&
                  fd >= lowfd)
                {
                  close(fd);
                  maxfd = fd;
                }
            }
        }
      closedir(dir);
      z_log(nullptr, CORE_DEBUG, 6, "File descriptors successfully closed; lowfd='%d', maxfd='%ld'", lowfd, maxfd);
    }
  else
    {
      ret = z_errno_get();
      z_log(nullptr, CORE_ERROR, 2, "Failed to open dir; path='%s', error='%s'", fd_path, g_strerror(ret));
    }

  return ret;
}

// Generic POSIX fallback (http://pubs.opengroup.org/onlinepubs/009695399/functions/sysconf.html)
#else
int
z_closefrom(const int lowfd)
{
  std::int64_t maxfd = sysconf(_SC_OPEN_MAX);
  if (maxfd < 0)
    maxfd = INT_MAX;

  for (std::int64_t i = lowfd; i < maxfd; ++i)
    close(i);

  z_log(nullptr, CORE_DEBUG, 6, "File descriptors successfully closed; lowfd='%d', maxfd='%ld'", lowfd, maxfd);

  return 0;
}
#endif
