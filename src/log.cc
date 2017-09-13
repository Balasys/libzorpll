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

#include <zorpll/log.h>
#include <zorpll/thread.h>
#include <zorpll/error.h>
#include <zorpll/misc.h>

#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#ifdef G_OS_WIN32
#  include <windef.h>
#  include <winbase.h>
#  include <io.h>
#  include <sys/stat.h>
#  define close _close
#  define open _open
#else
#  include <signal.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#endif

#ifdef G_OS_WIN32
//Windows exception handler needs this
LONG WINAPI z_unhandled_exception_filter(PEXCEPTION_POINTERS);
typedef LONG WINAPI z_unhandled_exception_filter_function(PEXCEPTION_POINTERS);
z_unhandled_exception_filter_function *z_setup_unhandled_exception_filter = &z_unhandled_exception_filter;
#endif


#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "Zorp"

/**
 * Extremal dummy value for booleans for detecting whether they are changed.
 * Rationale: FALSE is 0, TRUE is some unknown value, none of 255, 256, 65535,
 * 65536, (1L<<32)-1, (1L<<32) is divisible by 7, so this formula will yield
 * a value different from both 0 and TRUE, even considering a byte, word or
 * doubleword wrap-around :)
 **/
#define Z_EXTREMAL_BOOLEAN ((gboolean)(7*(int)TRUE + 1))

/**
 * Per-thread GHashTable based logtag cache.
 **/
typedef struct _ZLogTagCache
{
  gboolean empty_hash;
  gboolean used;
  GHashTable *tag_hash;
} ZLogTagCache;

/**
 * Parsed item of a ZLogSpec, pattern and verbosity_level.
 **/
typedef struct _ZLogSpecItem
{
  gchar *pattern;
  gint verbose_level;
} ZLogSpecItem;

/**
 * Complete logspec with a list of ZLogSpecItems.
 **/
typedef struct _ZLogSpec
{
  GSList *items;
  gint verbose_level;
} ZLogSpec;

/**
 * Logging options.
 **/
typedef struct _ZLogOpts
{
  gint verbose_level;           /**< verbosity level */
  gboolean use_syslog;          /**< whether to use syslog */
  gboolean log_tags;            /**< whether to include message tag and verbosity level in log messages */
  const gchar *log_spec;        /**< logspec specification */
} ZLogOpts;

ZLogOpts log_opts = {0, FALSE, FALSE, NULL};

ZLogOpts log_opts_cmdline = {-1, Z_EXTREMAL_BOOLEAN, Z_EXTREMAL_BOOLEAN, NULL};

/** Protects logtag_caches array when grabbing and releasing a thread specific log cache */
G_LOCK_DEFINE_STATIC(logtag_cache_lock);
static GPrivate current_logtag_cache;
static GPtrArray *logtag_caches;

/** Protects log_spec/log_spec_str */
G_LOCK_DEFINE_STATIC(log_spec_lock);
static ZLogSpec log_spec;
static gchar *log_spec_str;
static gboolean log_escape_nonprintable_chars = FALSE;

static ZLogMapTagFunc log_map_tag;
static gint log_mapped_tags_count;
static guchar *log_mapped_tags_verb;

static gboolean stderr_syslog = FALSE;
static gboolean log_tags = FALSE;

gchar fake_session_id[256] = "nosession";

static GMainContext *log_context = NULL;

#ifndef G_OS_WIN32
/*
 * This is a private reimplementation of syslog() as that one had a
 * mysterious bug in it and my bug reports were ignored.
 */

#if HAVE_BUGGY_SYSLOG_IN_LIBC

#define SYSLOG_SOCKET "/dev/log"

const gchar *syslog_tag = NULL;
int syslog_fd = -1;


/**
 * Analogous to openlog(), open the syslog() connection to local syslogd.
 *
 * @param[in] tag program name used in log messages
 *
 * @returns TRUE when the operation succeeds
 **/
gboolean
z_open_syslog(const gchar *tag)
{
  struct sockaddr_un s_un;

  syslog_tag = tag;
  syslog_fd = socket(PF_UNIX, SOCK_STREAM, 0);

  if (syslog_fd == -1)
    {
      return FALSE;
    }

  s_un.sun_family = AF_UNIX;
  g_strlcpy(s_un.sun_path, SYSLOG_SOCKET, sizeof(s_un.sun_path));
  if (connect(syslog_fd, (struct sockaddr *) &s_un, sizeof(s_un)) == -1)
    {
      close(syslog_fd);
      syslog_fd = socket(PF_UNIX, SOCK_DGRAM, 0);
      if (connect(syslog_fd, (struct sockaddr *) &s_un, sizeof(s_un)) == -1)
        {
          close(syslog_fd);
          syslog_fd = -1;
          return FALSE;
        }
    }
  return TRUE;
}

/**
 * Close the connection to the local syslogd.
 *
 * @param[in] fd syslog connection to close
 *
 * The syslog connection is specified by the fd argument to avoid races when reopening
 * connections.
 *
 * @returns whether the operation was successful
 **/
gboolean
z_close_syslog_internal(int fd)
{
  if (fd != -1)
    {
      close(fd);
      return TRUE;
    }
  return FALSE;
}

/**
 * Close the connection to the global syslogd.
 *
 * @returns TRUE to indicate success.
 **/
gboolean
z_close_syslog(void)
{
  return z_close_syslog_internal(syslog_fd);
}

/**
 * Send the specified message to syslog.
 *
 * @param[in] pri syslog priority
 * @param[in] msg syslog message
 **/
gboolean
z_send_syslog(gint pri, const gchar *msg)
{
  /*
   * NOTE: We limit the log message size to 8192 now. Hope it will be enough.
   * IIRC syslog-ng can only handle 8k long messages.
   */
  gchar buf[8192];
  const guchar *p;
  gchar timestamp[32];
  time_t now;
  struct tm t;
  guint len, attempt = 0;
  gint rc = 0;
  int sfd = syslog_fd;
  G_LOCK_DEFINE_STATIC(lock);

  now = time(NULL);
  z_localtime_r(&now, &t);

  strftime(timestamp, sizeof(timestamp), "%h %e %H:%M:%S", &t);

  g_snprintf(buf, sizeof(buf), "<%d>%s %s[%d]: ", pri, timestamp, syslog_tag, (int) getpid());
  if (log_escape_nonprintable_chars)
    {
      len = strlen(buf);
      for (p = (guchar *) msg; *p && len < sizeof(buf) - 5; p++)
        {
          if (*p >= 0x20 && *p <= 0x7F)
            {
              buf[len++] = *p;
            }
          else
            {
              g_snprintf(&buf[len], 5, "<%02X>", (guchar) *p);
              len += 4;
            }
        }
    }
  else
    {
      g_strlcat(buf, msg, sizeof(buf) - 1);
      len = strlen(buf);
    }
  buf[len++] = '\n';
  buf[len] = 0;
  do
    {
      attempt++;
      if (sfd != -1)
        rc = write(sfd, buf, len);
      if (sfd == -1 || (rc == -1 && errno != EINTR && errno != EAGAIN))
        {
          G_LOCK(lock);
          if (sfd == syslog_fd)
            {
              z_open_syslog(syslog_tag);
              z_close_syslog_internal(sfd);
            }

          sfd = syslog_fd;
          G_UNLOCK(lock);
        }
    }
  while (rc == -1 && attempt <= 1);
  return TRUE;
}

#else

/* use the syslog implementation in libc */

/**
 * Open a connection to the local system logging process.
 *
 * @param[in] tag program name to display in log messages
 *
 * This function uses the native syslog() implementation in libc to offer syslog functions.
 *
 * @returns always TRUE.
 **/
gboolean
z_open_syslog(const gchar *tag)
{
  openlog(tag, LOG_NDELAY | LOG_PID, 0);
  return TRUE;
}

/**
 * Close the connection to the local syslog process.
 *
 * @returns always TRUE.
 **/
gboolean
z_close_syslog(void)
{
  closelog();
  return TRUE;
}

/**
 * Send a message to syslogd. It uses the syslog() function in libc.
 *
 * @param[in] pri priority of the message
 * @param[in] msg message itself
 *
 * @returns always TRUE.
 **/
gboolean
z_send_syslog(gint pri, const gchar *msg)
{
  syslog(pri, "%s", msg);
  return TRUE;
}

#endif

#else

int syslog_fd = -1;

/**
 * Analogous to openlog(), open the syslog() connection to local syslogd.
 *
 * @param[in] tag program name used in log messages
 *
 * Windows implementation.
 *
 * @returns TRUE when the operation succeeds
 **/
gboolean
z_open_syslog(const gchar *tag)
{
  char fn[256];

  g_strlcpy(fn, getenv("windir"), sizeof(fn));
  g_strlcat(fn, "\\debug\\", sizeof(fn));
  g_strlcat(fn, tag, sizeof(fn));

  if((syslog_fd = open(fn, _O_APPEND | _O_RDWR | _O_CREAT, _S_IREAD | _S_IWRITE  )) == -1)
    {
      close(syslog_fd);
      syslog_fd = -1;
      return FALSE;
    }
  return TRUE;
}

/**
 * Close the connection to the local syslog process.
 **/
gboolean
z_close_syslog(void)
{
  if (syslog_fd != -1)
    {
      close(syslog_fd);
      return TRUE;
    }
  return FALSE;
}

#endif

/* logspec parsing and evaluation handling */

/**
 * Check when the pattern in a logspec matches the specified tag.
 *
 * @param[in] glob pattern to match tag against
 * @param[in] tag message tag
 **/
static gboolean
z_log_spec_glob_match(const gchar *glob, const gchar *tag)
{
  const gchar *p1, *p2;
  gint len1, len2;

  p1 = strchr(glob, '.');
  p2 = strchr(tag, '.');

  while (p1 && p2)
    {
      len1 = p1 - glob;
      len2 = p2 - tag;
      if (((len1 != 1) || (memcmp(glob, "*", 1) != 0)) &&
          ((len1 != len2) || memcmp(glob, tag, len1) != 0))
        return FALSE;

      glob = p1 + 1;
      tag = p2 + 1;

      p1 = strchr(glob, '.');
      p2 = strchr(tag, '.');
    }
  if (p1)
    len1 = p1 - glob;
  else
    len1 = strlen(glob);
  if (p2)
    len2 = p2 - tag;
  else
    len2 = strlen(tag);
  if (((len1 != 1) || (memcmp(glob, "*", 1) != 0)) &&
      ((len1 != len2) || memcmp(glob, tag, len1) != 0))
    return FALSE;
  glob += len1;
  tag += len2;
  if (strlen(glob) > strlen(tag))
    return FALSE;
  return TRUE;
}

/**
 * Evaluate the currently parsed logspec in self and return the verbosity level
 * associated with tag.
 *
 * @param[in] self ZLogSpec structure
 * @param[in] tag message to return verbosity for
 *
 * @returns the verbosity level associated with tag
 **/
static gint
z_log_spec_eval(ZLogSpec *self, const gchar *tag)
{
  GSList *l;
  ZLogSpecItem *lsi;

  l = self->items;
  while (l)
    {
      lsi = (ZLogSpecItem *) l->data;
      if (z_log_spec_glob_match(lsi->pattern, tag))
        {
          return lsi->verbose_level;
        }
      l = g_slist_next(l);
    }
  return self->verbose_level;
}

/**
 * Free all resources associated with self.
 *
 * @param[in] self ZLogSpec instance
 *
 * Does not free self as it is assumed to be allocated statically.
 **/
static void
z_log_spec_destroy(ZLogSpec *self)
{
  GSList *l, *l_next;
  ZLogSpecItem *lsi;

  l = self->items;
  while (l)
    {
      l_next = g_slist_next(l);
      lsi = (ZLogSpecItem *) l->data;

      g_free(lsi->pattern);
      g_free(lsi);
      g_slist_free_1(l);
      l = l_next;
    }
  self->items = NULL;
}

/**
 * Parse a user-specified logspec into self.
 *
 * @param[in] self ZLogSpec instance
 * @param[in] logspec_str logspec specification
 * @param[in] default_verbosity global verbosity level
 *
 * @returns sdf
 **/
static gboolean
z_log_spec_init(ZLogSpec *self, const gchar *logspec_str, gint default_verbosity)
{
  ZLogSpecItem *item;
  gchar *tmp = g_strdup(logspec_str ? logspec_str : ""), *src;
  gint new_level;

  src = tmp;
  self->items = NULL;
  self->verbose_level = default_verbosity;

  while (*src)
    {
      const gchar *glob, *num;
      gchar *end;

      while (*src == ',' || *src == ' ')
        src++;

      glob = src;
      while (isalnum((guchar) (*src)) || *src == '.' || *src == '*')
        src++;

      if (*src != ':')
        {
          /* invalid log spec */
          goto invalid_logspec;
        }
      *src = 0;
      src++;
      num = src;

      new_level = strtoul(num, &end, 10);

      item = g_new(ZLogSpecItem, 1);
      item->pattern = g_strdup(glob);
      item->verbose_level = new_level;
      self->items = g_slist_prepend(self->items, item);

      src = end;
      while (*src && *src != ',')
        src++;
    }
  self->items = g_slist_reverse(self->items);
  g_free(tmp);
  return TRUE;

 invalid_logspec:
  z_log_spec_destroy(self);
  g_free(tmp);
  return FALSE;
}


/* log tag cache
 *
 * Each thread has its own dedicated logtag cache to avoid locking. Caches are shared
 * which means that once a thread terminates it releases its cache which can be reused
 * by an independent thread.
 * */

/**
 * Clear all thread specific caches. It is called after changing the
 * verbosity level or the logspec.
 **/
void
z_log_clear_caches(void)
{
  guint i;

  G_LOCK(logtag_cache_lock);
  for (i = 0; i < logtag_caches->len; i++)
    {
      ZLogTagCache *lc = static_cast<ZLogTagCache *>(g_ptr_array_index(logtag_caches, i));

      lc->empty_hash = TRUE;
    }
  G_UNLOCK(logtag_cache_lock);
  if (log_mapped_tags_verb)
    {
      memset(log_mapped_tags_verb, 0, log_mapped_tags_count * sizeof(log_mapped_tags_verb[0]));
    }
}

/**
 * Grab a thread specific log-tag cache.
 **/
void
z_log_grab_cache(void)
{
  guint i;
  ZLogTagCache *lc = NULL;

  G_LOCK(logtag_cache_lock);

  for (i = 0; i < logtag_caches->len; i++)
    {
      lc = static_cast<ZLogTagCache *>(g_ptr_array_index(logtag_caches, i));

      if (!lc->used)
        break;
      else
        lc = NULL;
    }

  if (!lc)
    {
      lc = g_new0(ZLogTagCache, 1);
      lc->tag_hash = g_hash_table_new(g_str_hash, g_str_equal);
      g_ptr_array_add(logtag_caches, lc);
    }
  lc->used = 1;
  g_private_set(&current_logtag_cache, lc);

  G_UNLOCK(logtag_cache_lock);
}

/**
 * Release a thread specific log-tag cache to make it usable by other threads.
 **/
void
z_log_release_cache(void)
{
  ZLogTagCache *lc;

  G_LOCK(logtag_cache_lock);

  lc = static_cast<ZLogTagCache *>(g_private_get(&current_logtag_cache));
  if (lc)
    lc->used = 0;

  G_UNLOCK(logtag_cache_lock);
}

/**
 * Thread startup function called at thread startup time to allocate a logtag cache.
 *
 * @param thread (unused)
 * @param user_data (unused)
 **/
static void
z_log_thread_started(ZThread *thread G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
  z_log_grab_cache();
}

/**
 * Thread startup function called at thread shutdown time to release the logtag cache.
 *
 * @param thread (unused)
 * @param user_data (unused)
 **/
static void
z_log_thread_stopped(ZThread *thread G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
  z_log_release_cache();
}

/* global log state manipulation */

/**
 * This function changes the global verbosity level as specified by the
 * direction and value parameters.
 *
 * @param[in]  direction specifies the change direction (-1 decrease, 0 set, 1 increase)
 * @param[in]  value change with this value (or set to this value if direction == 0)
 * @param[out] new_value if not NULL, the resulting verbosity level is returned here
 *
 * @returns always TRUE.
 **/
gboolean
z_log_change_verbose_level(gint direction, gint value, gint *new_value)
{
  gint old_verbose_level = log_spec.verbose_level;

  G_LOCK(log_spec_lock);
  if (direction < 0)
    log_spec.verbose_level -= value;
  else if (direction == 0)
    log_spec.verbose_level = value;
  else
    log_spec.verbose_level += value;
  if (log_spec.verbose_level < 0)
    log_spec.verbose_level = 0;
  if (log_spec.verbose_level > 10)
    log_spec.verbose_level = 10;
  G_UNLOCK(log_spec_lock);

  if (old_verbose_level != log_spec.verbose_level)
    {
      z_log_clear_caches();
      /*LOG
        This message reports that Zorp changed its verbosity level.
       */
      z_log(NULL, CORE_INFO, 0, "Changing verbosity level; verbose_level='%d'", log_spec.verbose_level);
    }
  if (new_value)
    *new_value = log_spec.verbose_level;
  return TRUE;
}

/**
 * Change the logspec value and return the new setting.
 *
 * @param[in]  new_log_spec_str change the logspec to this value, leave it unchanged if set to NULL
 * @param[out] new_value if not NULL, the new logspec will be returned here
 *
 * @return TRUE on success
 **/
gboolean
z_log_change_logspec(const gchar *new_log_spec_str, const gchar **new_value)
{
  if (new_log_spec_str)
    {
      ZLogSpec new_spec;

      if (z_log_spec_init(&new_spec, new_log_spec_str, log_spec.verbose_level))
        {
          G_LOCK(log_spec_lock);
          z_log_spec_destroy(&log_spec);
          log_spec = new_spec;

          if (log_spec_str)
            g_free(log_spec_str);

          log_spec_str = g_strdup(new_log_spec_str);
          G_UNLOCK(log_spec_lock);
          z_log_clear_caches();
          /*LOG
            This message reports that Zorp changed its logspec.
           */
          z_log(NULL, CORE_INFO, 0, "Changing logspec; verbose_level='%d', logspec='%s'", log_spec.verbose_level, new_log_spec_str);
        }
      else
        {
          z_log(NULL, CORE_ERROR, 0, "Invalid logspec, reverting to old logspec; new_logspec='%s'", new_log_spec_str);
          return FALSE;
        }
    }

  if (new_value)
    *new_value = log_spec_str;
  return TRUE;
}

/**
 * This function enables the "tag_map cache" which makes tag caching very
 * efficient by using an array based lookup instead of GHashTable.
 *
 * @param[in] map_tags function to map message tags to IDs
 * @param[in] max_tag maximum ID value assigned to tags
 *
 * @note this function can only be called once, at startup time.
 **/
void
z_log_enable_tag_map_cache(ZLogMapTagFunc map_tags, gint max_tag)
{
  g_assert(!log_map_tag);

  log_map_tag = map_tags;
  log_mapped_tags_count = max_tag;
  log_mapped_tags_verb = g_new0(guchar, max_tag);
}

/**
 * Checks if a message with a given class/level combination would actually
 * be written to the log.
 *
 * @param[in] tag log message tag
 * @param[in] tag_len length of tag
 * @param[in] level log message level
 *
 * It can be used prior to constructing complex log
 * messages to decide whether the messages need to be constucted at all.
 * All results are cached, thus the second invocation will not parse the
 * log specifications again.
 *
 * @returns TRUE if the log would be written, FALSE otherwise
 **/
gboolean
z_log_enabled_len(const gchar *tag, gsize tag_len, gint level)
{
  return level <= z_log_get_tag_loglevel(tag ,tag_len);
}

gint
z_log_get_tag_loglevel(const gchar *tag, gsize tag_len)
{
  gint verbose;
  ZLogTagCache *lc;
  GHashTable *tag_hash;

  if (G_LIKELY(!log_spec.items))
    {
      /* fastpath, no logspec, decision is simple */
      return log_spec.verbose_level;
    }
  if (G_LIKELY(log_map_tag))
    {
      /* somewhat less fast path, map_tag is defined, use it for caching */
      gint tag_ndx = log_map_tag(tag, tag_len);
      if (G_LIKELY(tag_ndx != -1))
        {
          /* known keyword, use id indexed array to lookup tag specific verbosity */
          verbose = log_mapped_tags_verb[tag_ndx];
          if (G_LIKELY(verbose))
            verbose--;
          else
            {
              G_LOCK(log_spec_lock);
              verbose = z_log_spec_eval(&log_spec, tag);
              log_mapped_tags_verb[tag_ndx] = (guchar) (verbose & 0xFF) + 1;
              G_UNLOCK(log_spec_lock);
            }
          return verbose;
        }
    }
  /* check slow ghashtable based cache */
  lc = ((ZLogTagCache *) g_private_get(&current_logtag_cache));
  if (!lc)
    {
      return log_spec.verbose_level;
    }
  if (lc->empty_hash)
    {
      g_hash_table_destroy(lc->tag_hash);
      lc->tag_hash = g_hash_table_new(g_str_hash, g_str_equal);
      lc->empty_hash = FALSE;
    }
  tag_hash = lc->tag_hash;
  verbose = GPOINTER_TO_INT(g_hash_table_lookup(tag_hash, (gconstpointer) tag));
  if (!verbose)
    {
      /* slooooww path, evaluate logspec */
      G_LOCK(log_spec_lock);
      verbose = z_log_spec_eval(&log_spec, tag);
      G_UNLOCK(log_spec_lock);
      g_hash_table_insert(tag_hash, (gchar *) tag, GUINT_TO_POINTER(verbose + 1));
    }
  else
    verbose--;

  return verbose;
}

/* Main entry points for logging */

/**
 * Get the current session id.
 *
 * @param[in] session_id default session_id
 *
 * This helper function is used by the z_log() macro (or function, in
 * the case of Win32) to get the current session id.
 *
 * @returns If the argument is NULL, the session_id assigned to the
 * current thread, otherwise the value of the argument is returned.
 **/
const gchar *
z_log_session_id(const gchar *session_id)
{
  if (session_id == NULL || session_id[0] == 0)
    {
      ZThread *thread = z_thread_self();
      if (thread == NULL)
        return fake_session_id;
      else
        return thread->name;
    }
  return session_id;
}


/**
 * This function sends a message formatted as printf format string and
 * arguments to the syslog.
 *
 * @param[in] klass log message class
 * @param[in] level log message verbosity level
 * @param[in] format log message format specified in printf form
 * @param[in] ap format arguments va_list
 *
 * The associated class/level pair is checked
 * whether the message really needs to be written.
 **/
void
z_logv(const gchar *klass, int level, const gchar *format, va_list ap)
{
  int saved_errno = errno;

  if (log_tags)
    {
      gchar *msgbuf;
      msgbuf = g_strdup_vprintf(format, ap);

#if ZORPLIB_ENABLE_TRACE
      g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "%p -> %s(%d): %s", g_thread_self(), klass, level, msgbuf);
#else
      g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "%s(%d): %s", klass, level, msgbuf);
#endif
      g_free(msgbuf);
    }
  else
    {
      g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, format, ap);
    }
  errno = saved_errno;
}

/**
 * This message is the same as z_logv() but format string and arguments
 * are specified directly.
 *
 * @param[in] klass log message class
 * @param[in] level log message verbosity level
 * @param[in] format log message format specified in printf form
 *
 * @see z_log() in log.h
 **/
void
z_llog(const gchar *klass, int level, const gchar *format, ...)
{
  va_list l;

  va_start(l, format);
  z_logv(klass, level, format, l);
  va_end(l);
}

#ifdef G_OS_WIN32

/**
 * Win32 implementation of z_log().
 *
 * @param[in] session_id session id
 * @param[in] klass log message class
 * @param[in] level log message verbosity level
 * @param[in] format log message format specified in printf form
 *
 * This function checks if the message would be logged; if so, sends
 * a message formatted as printf format string and
 * arguments (including session id) to the syslog.
 *
 * @see z_log() in log.h
 **/
void
z_log(const gchar *session_id, const gchar *klass, int level, const gchar *format, ...)
{
  va_list l;
  gchar msgbuf[2048];

  if (!z_log_enabled(klass, level))
    return;

  va_start(l, format);

  g_vsnprintf(msgbuf, sizeof(msgbuf), format, l);

  g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "%p -> %s(%d): (%s): %s", g_thread_self(),
        klass, level, z_log_session_id(session_id), msgbuf);
  va_end(l);
}

#endif /* G_OS_WIN32 */

/**
 * This function is registered as a GLib log handler and sends all GLIB
 * messages to stderr.
 *
 * @param     log_domain not used
 * @param     log_flags not used
 * @param[in] message message
 * @param     user_data not used
 **/
static void
z_log_func_nosyslog(const gchar *log_domain G_GNUC_UNUSED,
	   GLogLevelFlags log_flags G_GNUC_UNUSED,
	   const gchar *message,
	   gpointer user_data G_GNUC_UNUSED)
{
  gchar timestamp[32];
  time_t now;
  struct tm tmnow;

  /* prepend timestamp */
  time(&now);
  strftime(timestamp, sizeof(timestamp), "%b %d %H:%M:%S", z_localtime_r(&now, &tmnow));
  fprintf(stderr, "%s %s\n", timestamp, message);
}


#ifndef G_OS_WIN32
/**
 * This function is registered as a GLib log handler and sends all GLIB
 * messages to messages to syslog.
 *
 * @param     log_domain GLIB log domain (unused)
 * @param[in] log_flags GLIB log flags
 * @param[in] message message
 * @param     user_data not used
 *
 * Zorp itself does not use GLIB logging, it calls z_log() directly.
 **/
static void
z_log_func(const gchar *log_domain G_GNUC_UNUSED,
	   GLogLevelFlags log_flags,
	   const gchar *message,
	   gpointer user_data G_GNUC_UNUSED)
{
  int pri = LOG_INFO;
  if (log_flags & G_LOG_LEVEL_DEBUG)
    pri = LOG_DEBUG;
  else if (log_flags & G_LOG_LEVEL_WARNING)
    pri = LOG_WARNING;
  else if (log_flags & G_LOG_LEVEL_ERROR)
    pri = LOG_ERR;

  z_send_syslog(pri | ZORP_SYSLOG_FACILITY, message);
}

#else

G_LOCK_DEFINE_STATIC(win32_log_handler_mutex);

/**
 * Log handler function to send Win32 debug message.
 *
 * @param     log_domain unused
 * @param     log_flags unused
 * @param[in] message debug message to send
 * @param     user_data unused
 **/
static void
z_log_win32_debugmsg(const gchar *log_domain,
                 GLogLevelFlags  log_flags,
                    const gchar *message,
                       gpointer  user_data)
{
  G_LOCK(win32_log_handler_mutex);

  OutputDebugString(message);
  OutputDebugString("\n");

  G_UNLOCK(win32_log_handler_mutex);
}

/**
 * Log handler function to send Win32 syslog message.
 *
 * @param     log_domain unused
 * @param     log_flags unused
 * @param[in] message syslog message to send
 * @param     user_data unused
 **/
static void
z_log_win32_syslogmsg(const gchar *log_domain,
                 GLogLevelFlags  log_flags,
                    const gchar *message,
                       gpointer  user_data)
{
  time_t now;
  struct tm *t;
  gchar tstamp[64];
  gchar buf[2048];
  int nchars;

  G_LOCK(win32_log_handler_mutex);

  now = time(NULL);
  t = localtime(&now);
  strftime(tstamp, sizeof(tstamp), "%Y %b %d %H:%M:%S", t);

  G_UNLOCK(win32_log_handler_mutex);

  nchars = g_snprintf(buf, sizeof(buf), "%s %s\n", tstamp, message);

  write(syslog_fd, buf, nchars);
}

#endif

/**
 * Fetch messages line-by-line and send them to log via z_log().
 *
 * @param[in] channel the read end of the STDERR pipe
 * @param     condition the I/O condition triggering this callback (unused)
 * @param     arg not used
 *
 * This function is registered as a read callback of the STDERR pipe to
 * fetch messages sent to stderr. It fetches messages line-by-line and
 * uses z_log() to send messages to log.
 *
 * @returns TRUE to indicate further reading is needed, FALSE otherwise
 **/
gboolean
z_fetch_stderr(GIOChannel *channel, GIOCondition condition G_GNUC_UNUSED, gpointer arg G_GNUC_UNUSED)
{
  gchar *line = NULL;
  GIOStatus status = G_IO_STATUS_NORMAL;
  GError *err = NULL;

  status = g_io_channel_read_line(channel, &line, NULL, NULL, &err);

  switch (status)
    {
    case G_IO_STATUS_NORMAL:
      /*NOLOG*/
      z_log(NULL, CORE_STDERR, 3, "%s", line);
      break;
    case G_IO_STATUS_AGAIN:
      break;
    case G_IO_STATUS_EOF:
      /*LOG
        This message indicates that the program closed its stderr. No further
        message on the stderr will be logged.
       */
      z_log(NULL, CORE_STDERR, 4, "The program closed its stderr. No further stderr logging is possible.");
      return FALSE;
    default:
      /*LOG
        This message indicates that an error occurred while reading from stderr.
       */
      z_log(NULL, CORE_STDERR, 3, "Can not read from stderr; result='%s'", (err != NULL) ? ((err)->message) : ("Unknown error"));
      return FALSE;
    }
  g_free(line);
  return TRUE;
}

/**
 * Creates the source watching the stderr pipe.
 *
 * @param[in] fd read side of the stderr pipe
 **/
GSource *
z_log_source_new(gint fd)
{
  GIOChannel *channel;
  GSource *source;

  channel = g_io_channel_unix_new(fd);
  g_io_channel_set_encoding(channel, NULL, NULL);
  g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);
  source = g_io_create_watch(channel, G_IO_IN);
  g_source_set_callback(source, (GSourceFunc) z_fetch_stderr, NULL, NULL);
  return source;
}

/**
 * STDERR reading thread function.
 *
 * @param[in] user_data thread data pointer, assumed to point to the stderr fd
 *
 * @returns always NULL.
 **/
void *
z_log_run(gpointer user_data)
{
  GMainContext *c, *old_context;
  gint *fd = (gint *)user_data;
  GSource *source;

  old_context = g_main_context_new();
  g_main_context_ref(old_context);
  log_context = old_context;

  g_main_context_acquire(log_context);

  source = z_log_source_new(*fd);
  g_source_attach(source, log_context);
  g_source_unref(source);

  do
    {
      /* NOTE: although log_context might already be unrefed by the thread
       * running z_log_destroy() we still hold a reference through
       * old_context, therefore it is not a problem if we use a stale value
       * in log_context, at most we run another iteration */

      c = log_context;

      if (c)
        g_main_context_iteration(c, TRUE);
    }
  while (c);

  g_main_context_release(old_context);
  g_main_context_unref(old_context);

  return NULL;
}

/**
 * This function can be called after z_log_init() to redirect internal
 * messages to syslog.
 *
 * @param[in] syslog_name syslog program name
 **/
void
z_log_enable_syslog(const gchar *syslog_name)
{
  z_open_syslog(syslog_name);
#ifndef G_OS_WIN32
  g_log_set_handler(G_LOG_DOMAIN, static_cast<GLogLevelFlags>(0xff), z_log_func, NULL);
#else
  g_log_set_handler(G_LOG_DOMAIN, static_cast<GLogLevelFlags>(0xff), z_log_win32_syslogmsg, NULL);
#endif
}

/**
 * This function can be called after z_log_init() to redirect stderr
 * messages messages to the system log.
 *
 * @param[in] threaded specifies whether to use a separate thread for stderr reading
 **/
void
z_log_enable_stderr_redirect(gboolean threaded)
{
#ifndef G_OS_WIN32
  static int grab[2];

  if (pipe(grab) < 0)
    {
      /*LOG
        This message is indicates that pipe creation failed. It is likely
	that the system runs out of free fds.
       */
      z_log(NULL, CORE_ERROR, 3, "Error creating stderr-syslog pipe;");
      return;
    }
  stderr_syslog = TRUE;
  dup2(grab[1], 1);
  dup2(grab[1], 2);
  if (grab[1] != 2 && grab[1] != 1)
    close(grab[1]);

  if (threaded)
    {
      if (!z_thread_new("stderr", z_log_run, &grab[0]))
        threaded = FALSE;
    }

  if (!threaded)
    {
      log_context = g_main_context_default();
      if (!g_main_context_acquire(log_context))
        {
          log_context = g_main_context_new();
          g_main_context_acquire(log_context);
        }
      g_main_context_ref(log_context);
      z_log_source_new(grab[0]);
    }
#endif
}


/**
 * Initialize the logging subsystem according to the options
 * in specified in the flags parameter.
 *
 * @param[in] syslog_name the program name to appear in syslogs
 * @param[in] flags log flags (ZLF_* macros)
 *
 * @returns TRUE on success
 **/
gboolean
z_log_init(const gchar *syslog_name, guint flags)
{
#ifndef G_OS_WIN32
  struct sigaction sa;
  gint i;

  i = sigaction(SIGPIPE, NULL, &sa);
  if (i)
    {
      z_log(NULL, CORE_ERROR, 0, "Can't get SIGPIPE handler; error='%s'", strerror(errno));
    }
  else if (sa.sa_handler == SIG_DFL)
    {
      sa.sa_handler = SIG_IGN;
      if (sigaction(SIGPIPE, &sa, NULL))
        z_log(NULL, CORE_ERROR, 0, "Can't set SIGPIPE handler; error='%s'", strerror(errno));
    }
#endif

  if (!z_log_spec_init(&log_spec, z_log_get_log_spec(), z_log_get_verbose_level()))
    {
      z_log(NULL, CORE_ERROR, 0, "Invalid logspec; logspec='%s'", z_log_get_log_spec());
      return FALSE;
    }
  log_spec_str = z_log_get_log_spec() ? g_strdup(z_log_get_log_spec()) : NULL;
  log_tags = z_log_get_log_tags();

  logtag_caches = g_ptr_array_new();

  z_log_grab_cache();
  z_thread_register_start_callback((GFunc) z_log_thread_started, NULL);
  z_thread_register_stop_callback((GFunc) z_log_thread_stopped, NULL);

  if (z_log_get_use_syslog())
    {
      z_log_enable_syslog(syslog_name);

#ifndef G_OS_WIN32
      if (flags & ZLF_STDERR)
        z_log_enable_stderr_redirect(flags & ZLF_THREAD);
#endif
    }
  else
    {
#ifdef G_OS_WIN32
      if (flags & ZLF_WINDEBUG)
        {
          g_log_set_handler(G_LOG_DOMAIN, static_cast<GLogLevelFlags>(0xff), z_log_win32_debugmsg, NULL);
        }
      else
#endif
        {
          g_log_set_handler(G_LOG_DOMAIN, static_cast<GLogLevelFlags>(0xff), z_log_func_nosyslog, NULL);
        }
    }

  if (flags & ZLF_ESCAPE)
    log_escape_nonprintable_chars = TRUE;
  return TRUE;
}

/**
 * Deinitialize the logging subsystem.
 **/
void
z_log_destroy(void)
{
  GMainContext *c;
#ifndef G_OS_WIN32
  if (stderr_syslog)
    {
      close(1);
      close(2);
    }
#endif
  z_close_syslog();

  /* NOTE: log_context is freed in the log thread */
  c = log_context;
  log_context = NULL;
  if (c)
    {
      g_main_context_wakeup(c);
      g_main_context_unref(c);
    }
}

/**
 * Keep track of indentation level and return the appropriate number of spaces.
 *
 * @param[in] dir direction and amount to indent
 *
 * The indentation level has to be in [0, 128], changes beyond that won't be performed.
 * The indentation level is tracked separately per thread.
 *
 * @returns pointer to static array of a number of spaces equal to the indentation level
 **/
const gchar *
z_log_trace_indent(gint dir)
{
  static const gchar *spaces128 =
    "                                                                "
    "                                                                ";

  static GPrivate current_indent_key = G_PRIVATE_INIT(g_free);
  int *current_indent = static_cast<int*>(g_private_get (&current_indent_key));
  const gchar *res;

  if (!current_indent)
    {
      current_indent = g_new (int,1);
      *current_indent = 0;
      g_private_set (&current_indent_key, current_indent);
    }

  if (dir > 0)
    {
      res = spaces128 + 128 - *current_indent;
      if (*current_indent < (128 - dir))
        *current_indent += dir;
    }
  else
    {
      if (*current_indent >= -dir)
        *current_indent += dir;
      res = spaces128 + 128 - *current_indent;
    }
  return res;
}

/**
 * Command line options for logging.
 **/
static GOptionEntry z_log_option_entries[] =
{
  { "verbose",    'v',                          0, G_OPTION_ARG_INT,      &log_opts_cmdline.verbose_level,    "Set verbosity level", "<verbosity>" },
  { "no-syslog",  'l',      G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE,     &log_opts_cmdline.use_syslog,       "Do not send messages to syslog, implies foreground mode", NULL },
  { "log-spec",   's',                          0, G_OPTION_ARG_STRING,   &log_opts_cmdline.log_spec,         "Set log specification", "<logspec>" },
  { "logspec",    's',      G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,    &log_opts_cmdline.log_spec,         "Alias for log-spec", "<logspec>" },
  { "log-tags",   'T',                          0, G_OPTION_ARG_NONE,     &log_opts_cmdline.log_tags,         "Enable logging of message tags", NULL },
  { NULL,          0,                           0,                   static_cast<GOptionArg>(0),   NULL, NULL, NULL }
};

/**
 * Set default options in log_opts.
 *
 * @param[in] verbose_level verbosity level
 * @param[in] use_syslog whether to use syslog
 * @param[in] n_log_tags whether to include message tag and verbosity level in log messages
 * @param[in] n_log_spec logspec specification
 **/
void
z_log_set_defaults(gint verbose_level, gboolean use_syslog, gboolean n_log_tags, const gchar *n_log_spec)
{
  log_opts.verbose_level = verbose_level;
  log_opts.use_syslog = use_syslog;
  log_opts.log_tags = n_log_tags;
  log_opts.log_spec = n_log_spec;
}

/**
 * Add the logging-specific command line options to the option context.
 *
 * @param[in] ctx GOptionContext instance
 **/
void
z_log_add_option_group(GOptionContext *ctx)
{
  GOptionGroup *group;

  /* initialise commandline arg variables to extremal values to be able to detect
   * whether they are changed or not */
  log_opts_cmdline.verbose_level = -1;
  log_opts_cmdline.use_syslog = Z_EXTREMAL_BOOLEAN;
  log_opts_cmdline.log_spec = NULL;
  log_opts_cmdline.log_tags = Z_EXTREMAL_BOOLEAN;

  group = g_option_group_new("log", "Log options", "Log options", NULL, NULL);
  g_option_group_add_entries(group, z_log_option_entries);
  g_option_context_add_group(ctx, group);
}

/**
 * Get verbose level setting in effect.
 *
 * @returns the setting from the command line option if one was given and the default otherwise.
 **/
gint
z_log_get_verbose_level(void)
{
  return (log_opts_cmdline.verbose_level == -1) ? log_opts.verbose_level : log_opts_cmdline.verbose_level;
}

/**
 * Get syslog setting in effect.
 *
 * @returns the setting from the command line option if one was given and the default otherwise.
 **/
gboolean
z_log_get_use_syslog(void)
{
  return (log_opts_cmdline.use_syslog == Z_EXTREMAL_BOOLEAN) ? log_opts.use_syslog : log_opts_cmdline.use_syslog;
}

/**
 * Get log tags setting in effect.
 *
 * @returns the setting from the command line option if one was given and the default otherwise.
 **/
const gchar *
z_log_get_log_spec(void)
{
  return (log_opts_cmdline.log_spec == NULL) ? log_opts.log_spec : log_opts_cmdline.log_spec;
}

/**
 * Get logspec string in effect.
 *
 * @returns the setting from the command line option if one was given and the default otherwise.
 **/
gboolean
z_log_get_log_tags(void)
{
  return (log_opts_cmdline.log_tags == Z_EXTREMAL_BOOLEAN) ? log_opts.log_tags : log_opts_cmdline.log_tags;
}

/**
 * Set the value of the use_syslog option.
 *
 * @param[in] use_syslog the new value
 **/
void
z_log_set_use_syslog(gboolean use_syslog)
{
  log_opts.use_syslog = use_syslog;
}
