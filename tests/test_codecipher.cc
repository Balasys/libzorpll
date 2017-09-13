#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/code_cipher.h>
#include <string.h>

BOOST_AUTO_TEST_CASE(test_codecipher)
{
  gchar buf[4097], buf2[4097];
  unsigned char key[128], iv[128];
  gchar *encrypted = NULL;
  gsize length = 0;
  ZCode *cipher;
  gint i, j;
  const EVP_CIPHER *algo;
  EVP_CIPHER_CTX cipher_ctx;

  memset(buf, 'A', sizeof(buf));
  memset(buf2, 'B', sizeof(buf2));

  algo = EVP_aes_128_cbc();
  memset(key, '\x55', sizeof(key));
  memset(iv, '\xaa', sizeof(iv));
  g_assert((gint) sizeof(key) > algo->key_len);
  g_assert((gint) sizeof(iv) > algo->iv_len);

  EVP_CipherInit(&cipher_ctx, algo, key, iv, TRUE);
  cipher = z_code_cipher_new(&cipher_ctx);

  for (i = 0; i < 4096; i++)
    {
      BOOST_CHECK_MESSAGE(z_code_transform(cipher, buf, sizeof(buf)) && z_code_transform(cipher, buf2, sizeof(buf2)), "Encryption failed");
    }
  BOOST_CHECK_MESSAGE(z_code_finish(cipher), "Encryption failed\n");

  length = z_code_get_result_length(cipher);
  encrypted = static_cast<gchar *>(g_malloc(length));
  memcpy(encrypted, z_code_peek_result(cipher), length);
  z_code_free(cipher);

  EVP_CipherInit(&cipher_ctx, algo, key, iv, FALSE);
  cipher = z_code_cipher_new(&cipher_ctx);
  BOOST_CHECK_MESSAGE(z_code_transform(cipher, encrypted, length) && z_code_finish(cipher), "Decryption failed");
  BOOST_CHECK_MESSAGE(z_code_get_result_length(cipher) == 4096 * 2 * sizeof(buf),
      "Decryption resulted different length, than encrypted length=" <<4096*2*sizeof(buf)<<" result=" << z_code_get_result_length(cipher));
  for (i = 0; i < 4096; i++)
    {
      for (j = 0; j < 2; j++)
        {
          guchar check;

          memset(buf, 'C', sizeof(buf));
          length = z_code_get_result(cipher, buf, sizeof(buf));
          BOOST_CHECK_MESSAGE(length == sizeof(buf), "Expected some more data\n");
          if (j == 0)
            check = 'A';
          else
            check = 'B';
          for (i = 0; i < (gint) sizeof(buf); i++)
            {
              BOOST_CHECK_MESSAGE(buf[i] == check, "Invalid data returned from decryption\n");
            }
        }
    }
}
