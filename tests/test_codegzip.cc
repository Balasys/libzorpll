#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/code_gzip.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

BOOST_AUTO_TEST_CASE(test_codegzip)
{
  gchar buf[4096], buf2[4096];
  gchar *compressed = NULL;
  gsize length = 0;
  ZCode *gz;
  guint i, j;

  memset(buf, 'A', sizeof(buf));
  memset(buf2, 'B', sizeof(buf2));
  gz = z_code_gzip_encode_new(1024, 1);

  length = 0;
  for (i = 0; i < 4096; i++)
    {
      BOOST_CHECK_MESSAGE(z_code_transform(gz, buf, sizeof(buf)) &&
                          z_code_transform(gz, buf2, sizeof(buf2)) &&
                          z_code_finish(gz), "Compression failed\n");
      compressed = static_cast<gchar *>(realloc(compressed, length + z_code_get_result_length(gz)));
      memcpy(compressed + length, z_code_peek_result(gz), z_code_get_result_length(gz));
      length += z_code_get_result_length(gz);
      z_code_flush_result(gz, 0);
    }

  gz = z_code_gzip_decode_new(4096);
  BOOST_CHECK_MESSAGE(z_code_transform(gz, compressed, length) &&
                      z_code_finish(gz), "Decompression failed\n");
  BOOST_CHECK_MESSAGE(z_code_get_result_length(gz) == 4096 * 2 * sizeof(buf),
      "Decompression resulted different length, than compressed length="<<length<<" result="<< z_code_get_result_length(gz));
  for (i = 0; i < 4096; i++)
    {
      for (j = 0; j < 2; j++)
        {
          guchar check;

          memset(buf, 'C', sizeof(buf));
          length = z_code_get_result(gz, buf, sizeof(buf));
          BOOST_CHECK_MESSAGE(length == sizeof(buf), "Expected some more data\n");
          if (j == 0)
            check = 'A';
          else 
            check = 'B';
          for (i = 0; i < sizeof(buf); i++)
            {
              BOOST_CHECK_MESSAGE(buf[i] == check, "Invalid data returned from decompression\n");
            }
        }
    }
}
