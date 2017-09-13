/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/zorplib.h>
#include <zorpll/misc.h>
#include <string.h>
#include <stdio.h>

#if HAVE_CRYPT_H
#include <crypt.h>
#endif

#if HAVE_CRYPT
/** mutex protecting crypt() invocation */
G_LOCK_DEFINE_STATIC(crypt_lock);
#else
#include <openssl/des.h>
#endif

#ifdef G_OS_WIN32
#define snprintf _snprintf
#endif

#if !HAVE_MD5_CRYPT

#include <openssl/md5.h>

#define CRYPT_MD5_MAGIC       "$1$"
#define CRYPT_MD5_MAGIC_LEN   3

static unsigned char md5_crypt_b64t[] =         /**< 0 to 63 => ascii - 64 */
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        

/**
 * Encode MD5 hash to the ASCII-based encoding defined by md5_crypt_b64t
 *
 * @param[out] s buffer where the result will be returned
 * @param[in]  v the MD5 hash to be encoded
 * @param[in]  n the size of the buffer
 *
 * @note This function won't write a terminating null to the buffer.
 *      However, it will always write n bytes, even writing 0-s if v runs out.
 **/
static inline void
md5_crypt_to64(char *s, unsigned int v, int n)
{
  while (--n >= 0) 
    {
      *s++ = md5_crypt_b64t[v & 0x3f];
      v >>= 6;
    }
}

/**
 * MD5 version of crypt(3) -- generates an MD5-based hash string (with salt) of the password.
 *
 * @param[in]  pw the password
 * @param[in]  salt the salt
 * @param[out] result buffer for the result
 * @param[in]  result_len size of the buffer
 **/
void
md5_crypt(const char *pw, const char *salt, char *result, size_t result_len)
{
  const char *sp, *ep;
  char *p;
  unsigned char final[16];
  int i, sl, pwl;
  unsigned int l;
	
  MD5_CTX ctx, alt_ctx;
	
  /* Refine the salt first */
  sp = salt;

  /* If it starts with the magic string, then skip that */
  if (strncmp(sp, CRYPT_MD5_MAGIC, CRYPT_MD5_MAGIC_LEN) == 0)
    sp += CRYPT_MD5_MAGIC_LEN;

  /* It stops at the first '$', max 8 chars */
  for (ep = sp; *ep != '\0' && *ep != '$' && ep < (sp + 8); ep++)
    continue;

  /* get the length of the true salt */
  sl = ep - sp;
	
  pwl = strlen(pw);
  MD5_Init(&ctx);
  MD5_Update(&ctx, pw, pwl);
  MD5_Update(&ctx, CRYPT_MD5_MAGIC, CRYPT_MD5_MAGIC_LEN);
  MD5_Update(&ctx, sp, sl);
  
  MD5_Init(&alt_ctx);
  MD5_Update(&alt_ctx, pw, pwl);
  MD5_Update(&alt_ctx, sp, sl);
  MD5_Update(&alt_ctx, pw, pwl);
  MD5_Final(final, &alt_ctx);
	
  for (i = pwl; i > 0; i -= 16)
    MD5_Update(&ctx, final, (i > 16 ? 16 : i));
  
  /* Then something really weird... */
  final[0] = 0;
  
  for (i = pwl; i != 0; i >>= 1)
    if ((i & 1) != 0)
      MD5_Update(&ctx, final, 1);
    else
      MD5_Update(&ctx, pw, 1);

  MD5_Final(final, &ctx);
	
  for (i = 0; i < 1000; i++) 
    {
      MD5_Init(&ctx);
	
      if ((i & 1) != 0)
        MD5_Update(&ctx, pw, pwl);
      else
        MD5_Update(&ctx, final, 16);
	
      if ((i % 3) != 0)
	MD5_Update(&ctx, sp, sl);
      
      if ((i % 7) != 0)
	MD5_Update(&ctx, pw, pwl);
	
      if ((i & 1) != 0)
	MD5_Update(&ctx, final, 16);
      else
	MD5_Update(&ctx, pw, pwl);
      
      MD5_Final(final, &ctx);
    }
  /* Now make the output string */
  i = snprintf(result, result_len, "%s%.*s$", CRYPT_MD5_MAGIC, sl, sp);
  if (i > 0 && result_len > (unsigned int) i)
    {
      p = result + i;
      
      if (result_len > 23)
        {

          l = (final[ 0]<<16) | (final[ 6]<<8) | final[12]; md5_crypt_to64(p,l,4); p += 4;
          l = (final[ 1]<<16) | (final[ 7]<<8) | final[13]; md5_crypt_to64(p,l,4); p += 4;
          l = (final[ 2]<<16) | (final[ 8]<<8) | final[14]; md5_crypt_to64(p,l,4); p += 4;
          l = (final[ 3]<<16) | (final[ 9]<<8) | final[15]; md5_crypt_to64(p,l,4); p += 4;
          l = (final[ 4]<<16) | (final[10]<<8) | final[ 5]; md5_crypt_to64(p,l,4); p += 4;
          l =	final[11]; 
          md5_crypt_to64(p,l,2); p += 2;
          *p = '\0';
        }

      /* Don't leave anything around in vm they could use. */
      memset(final, 0, sizeof(final));
    }
}


#endif

/**
 * This function is a reentrant version of crypt(3), ensuring thread
 * synchronization using a global mutex.
 *
 * @param[in]  key password to crypt
 * @param[in]  salt salt value
 * @param[out] result result is stored here
 * @param[in]  result_len length of the result buffer
 **/

/* based on actual crypt(3) */

#if HAVE_CRYPT && HAVE_MD5_CRYPT

void
z_crypt(const char *key, const char *salt, char *result, size_t result_len)
{
  G_LOCK(crypt_lock);
  g_strlcpy(result, crypt(key, salt), result_len);
  G_UNLOCK(crypt_lock);
}

#elif HAVE_CRYPT

/* have crypt, but no md5 in crypt */

void
z_crypt(const char *key, const char *salt, char *result, size_t result_len)
{
  if (strncmp(salt, CRYPT_MD5_MAGIC, CRYPT_MD5_MAGIC_LEN) == 0)
    {
      /* need md5 crypt */
      md5_crypt(key, salt, result, result_len);
    }
  else
    {
      /* ok, call libc crypt */
      G_LOCK(crypt_lock);
      g_strlcpy(result, crypt(key, salt), result_len);
      G_UNLOCK(crypt_lock);
    }
}


#else

/* does not have crypt at all */

void
z_crypt(const char *key, const char *salt, char *result, size_t result_len)
{
  if (strncmp(salt, CRYPT_MD5_MAGIC, CRYPT_MD5_MAGIC_LEN) == 0)
    {
      /* need md5 crypt */
      md5_crypt(key, salt, result, result_len);
    }
  else
    {
      g_assert(result_len >= 14);
      DES_fcrypt(key, salt, result);
    }
}

#endif
