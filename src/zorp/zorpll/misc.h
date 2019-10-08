/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_MISC_H_INCLUDED
#define ZORP_MISC_H_INCLUDED

#include <zorpll/zorplib.h>

#include <glib.h>
#include <sys/types.h>
#ifdef G_OS_WIN32
#define _WINSOCKAPI_
#include <windows.h>
#include <time.h>
#endif

#include <cmath>
#include <limits>
#include <type_traits>
#include <algorithm>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __GNUC__
#define __attribute__(x)
#endif // GNUC

#define MAX_REF 512*1024

#define Z_STRING_SAFE(x) (x ? x : "(null)")

#define ON_OFF_STR(x) (x ? "on" : "off")
#define YES_NO_STR(x) (x ? "yes" : "no")

#ifndef G_GNUC_WARN_UNUSED_RESULT
# if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#  define G_GNUC_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
# else
#  define G_GNUC_WARN_UNUSED_RESULT
# endif
#endif

#define IGNORE_UNUSED_RESULT(x) if ((x)) { }
/**
 * Stores a character set in the form of a bitfield.
 *
 * ZCharSet is a simple representation of a character set. It can be parsed
 * from a string representation where regexp like expressions can be used. 
 * Checking whether a given character is in the character set is quite fast
 * as each character is represented as a bit in a bitstring.
 **/
typedef struct _ZCharSet
{
  guint32 enable_mask[256 / 32];
} ZCharSet;

void z_charset_init(ZCharSet *self);
gboolean z_charset_parse(ZCharSet *self, const gchar *spec);
gboolean z_charset_is_string_valid(ZCharSet *self, const gchar *str, gint len);

/**
 * Puts chr into the ZCharSet.
 *
 * @param[in] self ZCharSet instance
 * @param[in] chr the character to put into the set
 * 
 * Sets the bit corresponding to the character chr in self.
 **/
static inline void 
z_charset_enable(ZCharSet *self, char chr)
{
  guint ndx = ((guchar) chr) >> 5;              /* top 3 bits are the index of the byte */
  guint bit = 1 << (((guchar) chr) & 0x1F);     /* bit mask from the value of the lower 5 bits */
  
  self->enable_mask[ndx] |= bit;
}

/**
 * Checks if chr is in the ZCharSet.
 *
 * @param[in] self ZCharSet instance
 * @param[in] chr character to look up in self
 *
 * @returns TRUE if it's in the charset, FALSE if not
 **/
static inline gboolean
z_charset_is_enabled(ZCharSet *self, char chr)
{
  guint ndx = ((guchar) chr) >> 5;              /* top 3 bits are the index of the byte */
  guint bit = 1 << (((guchar) chr) & 0x1F);     /* bit mask from the value of the lower 5 bits */
  
  return !!(self->enable_mask[ndx] & bit);
}

void 
z_format_data_dump(const gchar *session_id, const gchar *class_, gint level, const void *buf, guint len);

void 
z_format_text_dump(const gchar *session_id, const gchar *class_, gint level, const void *buf, guint len);

GString *g_string_assign_len(GString *s, const gchar *val, gint len);
GString *g_string_assign_gstring(GString *target, const GString *source);

#if !GLIB_CHECK_VERSION(2, 28, 0)
void g_list_free_full(GList *list, GDestroyNotify free_func);
#endif


gint g_time_val_compare(const GTimeVal *t1, const GTimeVal *t2);
glong g_time_val_diff(const GTimeVal *t1, const GTimeVal *t2);
void g_time_val_subtract(GTimeVal *result, const GTimeVal *t1, const GTimeVal *t2);

gchar *z_str_escape(const gchar *s, gint len);
gchar *z_str_compress(const gchar *s, gint len);

gboolean z_port_enabled(gchar *port_range, guint port);

void z_crypt(const char *key, const char *salt, char *result, size_t result_len);

guint z_casestr_hash(gconstpointer key);

/**
 * Increase the reference count pointed to by ref.
 *
 * @param[in, out] ref
 *
 * Does some sanity checking.
 **/
static inline void
z_incref(guint *ref)
{
  g_assert(*ref < MAX_REF && *ref > 0);
  (*ref)++;
}

/**
 * Decrease the reference count pointed to by ref.
 *
 * @param[in, out] ref
 *
 * Does some sanity checking.
 *
 * @returns the new value of the reference count.
 **/
static inline guint
z_decref(guint *ref)
{
  g_assert(*ref < MAX_REF && *ref > 0);
  (*ref)--;
  
  return *ref;
}

/**
 * Reference counter.
 **/
typedef struct _ZRefCount
{
  gint counter;
} ZRefCount;

#define Z_REFCOUNT_INIT { 1 }

/**
 * Atomically increase a reference count.
 *
 * @param[in] ref ZRefCount instance
 *
 * This function atomically increases a reference count while checking its
 * value for sanity. If the value is insane, the program is aborted.
 **/
static inline void
z_refcount_inc(ZRefCount *ref)
{
  g_assert(ref->counter < MAX_REF && ref->counter > 0);
  g_atomic_int_inc(&ref->counter);
}

/**
 * Atomically decrease a reference count and check if it is zero already.
 *
 * @param[in] ref ZRefCount instance
 *
 * @returns TRUE if the reference count reached zero, e.g. the object
 * must be freed.
 **/
static inline gboolean
z_refcount_dec(ZRefCount *ref)
{
  g_assert(ref->counter < MAX_REF && ref->counter > 0);
  return g_atomic_int_dec_and_test(&ref->counter);
}

/**
 * Set the reference counter to the value specified.
 *
 * @param[in] ref ZRefCount instance
 * @param[in]      value set reference count to this value
 *
 * @note This function is not
 * atomic wrt _inc and _dec, and thus it should only be used during
 * initialization.
 **/
static inline void
z_refcount_set(ZRefCount *ref, gint value)
{
  ref->counter = value;
}

static inline void
g_ustrfreev(guchar **str)
{
  g_strfreev((gchar **)str);
}

static inline void
g_sstrfreev(gint8 **str)
{
  g_strfreev((gchar **)str);
}

const gchar *z_libzorpll_version_info(void);


typedef enum
{
  Z_OG_PROCESS = 0x1,
  Z_OG_THREAD  = 0x2, 
  Z_OG_LOG     = 0x4, 
} ZOptionGroupFlags;

void z_libzorpll_add_option_groups(GOptionContext *ctx, guint disable_groups);

struct tm *z_localtime_r(const time_t *timep, struct tm *result);

#ifndef G_OS_WIN32
int z_closefrom(const int lowfd);
#endif

#ifdef __cplusplus
}
#endif


/**
 * Helper for comparing real values
 * See: http://en.cppreference.com/w/cpp/types/numeric_limits/epsilon
 *
 * @param[in] x one item to compare
 * @param[in] y the other item to compare
 * @param[in] ulp the allowable difference in the last place
 * @returns whether x and y is 'almst the same'
 **/
template<class T>
typename std::enable_if<!std::numeric_limits<T>::is_integer, bool>::type
almost_equal(T x, T y, int ulp)
{
        // the machine epsilon has to be scaled to the magnitude of the values used
        // and multiplied by the desired precision in ULPs (units in the last place)
        return std::abs(x-y) < std::numeric_limits<T>::epsilon() * std::abs(x+y) * ulp
            // unless the result is subnormal
            || std::abs(x-y) < std::numeric_limits<T>::min();
}
#endif
