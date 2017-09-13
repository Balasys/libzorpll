#ifndef ZORP_PROXY_CODE_H
#define ZORP_PROXY_CODE_H

// #include <zorpll/zorp.h>

#include <glib.h>

/** default buffer size */
#define ZCODE_BUFSIZE_DEFAULT       256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ZCode ZCode;

/**
 * The ZCode class facilitates transforming (encoding or decoding) binary data.
 **/
struct _ZCode
{
  guchar *buf;
  gsize buf_len, buf_used;
  gint error_counter;
  gboolean (*transform)(ZCode *self, const void *from, gsize fromlen);
  gboolean (*finish)(ZCode *self);
  void (*free_fn)(ZCode *self);
};

gboolean z_code_grow(ZCode *self, gint reqlen);

gsize z_code_get_result_length(ZCode *self);
gsize z_code_get_result(ZCode *self, void *to, gsize tolen);
const void *z_code_peek_result(ZCode *self);
void z_code_unget_result(ZCode *self, const void *from, gsize fromlen);
void z_code_flush_result(ZCode *self, gsize flush_length);

ZCode *z_code_new(gint bufsize);
void z_code_init(ZCode *self, gint bufsize);
void z_code_free(ZCode *self);

/**
 * Get the error counter of the ZCode instance.
 *
 * @param[in] self ZCode instance
 *
 * @returns error counter
 **/
static inline gint
z_code_get_errors(ZCode *self)
{
  return self->error_counter;
}

/**
 * Reset the error counter.
 *
 * @param[in] self ZCode instance
 **/
static inline void
z_code_clear_errors(ZCode *self)
{
  self->error_counter = 0;
}

/**
 * Transforms data to the internal buffer.
 *
 * @param[in] self this
 * @param[in] from source buffer
 * @param[in] fromlen number of bytes to transform
 *
 * Can be used any number of
 * times, the internal buffer grows automatically.
 *
 * @returns The number of bytes written
 */
static inline gboolean
z_code_transform(ZCode *self, const void *from, gsize fromlen)
{
  return self->transform(self, from, fromlen);
}

/**
 * Finalizes the output in the internal buffer.
 *
 * @param[in] self this
 *
 * Should be called once at the end of the encoding process.
 */
static inline gboolean
z_code_finish(ZCode *self)
{
  if (self->finish)
    return self->finish(self);
  return TRUE;
}

#ifdef __cplusplus
}
#endif

#endif
