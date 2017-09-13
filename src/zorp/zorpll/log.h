/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_LOG_H_INCLUDED
#define ZORP_LOG_H_INCLUDED

#include <zorpll/zorplib.h>

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENABLE_TRACE
#if ZORPLIB_ENABLE_TRACE
  #define ENABLE_TRACE 1
#endif /* ZORPLIB_ENABLE_TRACE */
#endif /* ENABLE_TRACE */

LIBZORPLL_EXTERN gchar fake_session_id[256];

typedef gint (*ZLogMapTagFunc)(const gchar *tag, gsize len);

#define ZORP_SYSLOG_FACILITY LOG_LOCAL6

#define ZLF_SYSLOG      0x0001
#define ZLF_TAGS        0x0002
#define ZLF_THREAD      0x0004
#define ZLF_STDERR      0x0008
#define ZLF_WINDEBUG    0x0010
#define ZLF_ESCAPE      0x0020

#ifndef G_OS_WIN32
  #define z_debug(level, format, args...)   z_llog("core.debug", level, format, ##args)
  #define z_warning(level, format, args...) z_llog("core.warning", level, format, ##args)
  #define z_message(level, format, args...) z_llog("core.message", level, format, ##args)
#else
  #define z_debug(level, format, args)   z_llog("core.debug", level, format, ##args)
  #define z_warning(level, format, args) z_llog("core.warning", level, format, ##args)
  #define z_message(level, format, args) z_llog("core.message", level, format, ##args)
#endif

gboolean z_log_init(const gchar *syslog_name, guint flags);

void z_log_enable_syslog(const gchar *syslog_name);
void z_log_enable_stderr_redirect(gboolean threaded);
void z_log_enable_tag_map_cache(ZLogMapTagFunc map_tags, gint max_tag);


void z_logv(const gchar *class_, int level, const gchar *format, va_list ap);

#ifndef G_OS_WIN32
  void z_llog(const gchar *class_, int level, const gchar *format, ...) __attribute__ ((format(printf, 3, 4)));
#else
  void z_llog(const gchar *class_, int level, const gchar *format, ...);
#endif

gboolean z_log_enabled_len(const gchar *class_, gsize class_len, int level);

/**
 * Checks if a message with a given class/level combination would actually be written to the log -- with null-terminated tag string.
 *
 * @param[in] class_ message tag
 * @param[in] level log message level
 *
 * @see z_log_enabled_len in log.c
 *
 * @returns TRUE if the log would be written, FALSE otherwise
 **/
#ifndef G_OS_WIN32
  #define z_log_enabled(class_, level)                                          \
    ({                                                                          \
      gboolean __res;                                                           \
      if (__builtin_constant_p(class_))                                         \
        __res = z_log_enabled_len(class_, __builtin_strlen(class_), level);     \
      else                                                                      \
        __res = z_log_enabled_len(class_, strlen(class_), level);               \
      __res;                                                                    \
    })
#else
  #define z_log_enabled(class_, level)                                          \
    z_log_enabled_len(class_, strlen(class_), level)
#endif

gboolean z_log_change_verbose_level(gint direction, gint value, gint *new_value);
gboolean z_log_change_logspec(const gchar *log_spec, const gchar **new_value);

void z_log_clear_caches(void);
void z_log_destroy(void);

const gchar *z_log_session_id(const gchar *session_id);

/**
 * This function generates hexdumps of the specified buffer to the system
 * log using z_log().
 *
 * @param[in] session_id session id to be used for the log messages
 * @param[in] class_ log message class
 * @param[in] level log message verbosity level
 * @param[in] buf buffer
 * @param[in] len buffer length
 *
 * This inline function checks if the dump would be logged before generating it.
 *
 * @see z_format_data_dump()
 **/
static inline void
z_log_data_dump(const gchar *session_id, const gchar *class_, gint level, const void *buf, guint len)
{
  if (z_log_enabled(class_, level))
    z_format_data_dump(session_id, class_, level, buf, len);
}

/**
 * This function generates textual dumps of the specified buffer to the system
 * log using z_log().
 *
 * @param[in] session_id session id to be used for the log messages
 * @param[in] class_ log message class
 * @param[in] level log message verbosity level
 * @param[in] buf buffer
 * @param[in] len buffer length
 *
 * This inline function checks if the dump would be logged before generating it.
 *
 * @see z_format_text_dump()
 **/
static inline void
z_log_text_dump(const gchar *session_id, const gchar *class_, gint level, const char *buf, guint len)
{
  if (z_log_enabled(class_, level))
    z_format_text_dump(session_id, class_, level, buf, len);
}

const gchar *
z_log_trace_indent(gint dir);

/**
 * Platform-dependent z_log() macro.
 *
 * @see the non-Win32 implementation as z_llog() in log.c.
 *      Whether the message would really be written is checked
 *      and the string form of session id is prepended to the format string
 *      and argument list before calling z_llog().
 * @see the Win32 implementation as z_log() in log.c.
 **/
#ifdef G_OS_WIN32
  void z_log(const gchar* session_id, const gchar* class_, int level, const gchar* format, ...);
#else
  #define z_log(session_id, class_, level, format, args...) \
    do \
      { \
        if (z_log_enabled(class_, level)) \
	  /*NOLOG*/ \
          z_llog(class_, level, "(%s): " format, z_log_session_id(session_id) , ##args); \
      } \
    while (0)

#endif

#if ENABLE_TRACE
  #ifndef G_OS_WIN32
    #define z_trace(session_id, args...) z_log(session_id , CORE_TRACE, 7, ##args)
  #else
    #define z_trace
  #endif
  #define z_session_enter(s) z_log(s, CORE_TRACE, 7, "%sEnter %s (%s:%d)", z_log_trace_indent(1), __FUNCTION__, __FILE__, __LINE__)
  #define z_session_leave(s) z_log(s, CORE_TRACE, 7, "%sLeave %s (%s:%d)", z_log_trace_indent(-1), __FUNCTION__, __FILE__, __LINE__)
  #define z_session_cp(s) z_log(s, CORE_TRACE, 7, "%sCheckpoint %s (%s:%d)", z_log_trace_indent(0), __FUNCTION__, __FILE__, __LINE__)
  #define z_enter() z_session_enter(NULL)
  #define z_leave() z_session_leave(NULL)
  #define z_cp() z_session_cp(NULL)
#else
  #ifndef G_OS_WIN32
    #define z_trace(session_id, args...)
  #else
    #define z_trace
  #endif
  #define z_enter()
  #define z_leave()
  #define z_cp()
  #define z_session_enter(s)
  #define z_session_leave(s)
  #define z_session_cp(s)

#endif

#ifdef G_OS_WIN32
  /* disable C4003: not enough actual parameters for macro 'z_return' */
#pragma warning(disable: 4003)
#endif
#define z_return(retval)      do { z_leave(); return retval; } while (0)

void z_log_set_defaults(gint verbose_level, gboolean use_syslog, gboolean n_log_tags, const gchar *n_log_spec);

void z_log_set_use_syslog(gboolean use_syslog);

gint z_log_get_verbose_level(void);
gboolean z_log_get_use_syslog(void);
const gchar * z_log_get_log_spec(void);
gboolean z_log_get_log_tags(void);
gint z_log_get_tag_loglevel(const gchar *tag, gsize tag_len);

#ifdef __cplusplus
}
#endif

#endif
