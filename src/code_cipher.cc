#include <zorpll/code_cipher.h>

/**
 * ZCode-derived class to encrypt/decrypt data using the OpenSSL EVP library.
 **/
typedef struct _ZCodeCipher
{
  ZCode super;
  EVP_CIPHER_CTX *cipher_ctx;
} ZCodeCipher;

static void z_code_cipher_free(ZCode *s);

/**
 * Transform the contents of buf into the buffer of the ZCode instance using an EVP cipher.
 *
 * @param[in] s ZCode instance
 * @param[in] buf input buffer
 * @param[in] buflen length of buf
 *
 * @returns TRUE on success
 **/
static gboolean
z_code_cipher_transform(ZCode *s, const void *buf, gsize buflen)
{
  ZCodeCipher *self = (ZCodeCipher *) s;
  gint out_length;
  gboolean result = TRUE;
  
  if (buflen)
    {
      z_code_grow(s, s->buf_used + ((buflen / self->cipher_ctx->cipher->block_size) + 1) * self->cipher_ctx->cipher->block_size);

      out_length = s->buf_len - s->buf_used;
      result = !!EVP_CipherUpdate(self->cipher_ctx, s->buf + s->buf_used, &out_length, static_cast<const unsigned char *>(buf), buflen);
      s->buf_used += out_length;
    }
  return result;
}

/**
 * Do the final step of transformation via the EVP cipher -- that is, deal with padding.
 *
 * @param[in] s ZCode instance
 *
 * @returns TRUE on success
 **/
static gboolean
z_code_cipher_finish(ZCode *s)
{
  ZCodeCipher *self = (ZCodeCipher *) s;
  gboolean result;
  gint out_length;
  
  z_code_grow(s, s->buf_used + self->cipher_ctx->cipher->block_size);

  out_length = s->buf_len - s->buf_used;
  result = !!EVP_CipherFinal(self->cipher_ctx, s->buf + s->buf_used, &out_length);
  s->buf_used += out_length;
  return result;
}

/**
 * Create a new ZCodeCipher instance using the specified EVP cipher context.
 *
 * @param[in] cipher_ctx the cipher context
 *
 * @returns the new ZCodeCipher instance
 **/
ZCode *
z_code_cipher_new(EVP_CIPHER_CTX *cipher_ctx)
{
  ZCodeCipher *self;

  self = g_new0(ZCodeCipher, 1);

  z_code_init(&self->super, 0);
  self->super.transform = z_code_cipher_transform;
  self->super.finish = z_code_cipher_finish;
  self->super.free_fn = z_code_cipher_free;
  self->cipher_ctx = cipher_ctx;

  return &self->super;
}

/**
 * Destroy a ZCodeCipher instance.
 *
 * @param[in] s ZCodeCipher instance
 *
 * The cipher context also gets cleaned up.
 **/
static void 
z_code_cipher_free(ZCode *s)
{
  ZCodeCipher *self = (ZCodeCipher *) s;

  EVP_CIPHER_CTX_cleanup(self->cipher_ctx);
}
