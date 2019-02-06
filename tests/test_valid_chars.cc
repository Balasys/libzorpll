#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/misc.h>

#include <stdio.h>

typedef struct _Tests
{
  const gchar *valid_chars;
  const gchar *teststring;
  gboolean expected_result;
} Tests;

static struct _Tests testcases[] = {
	{"a-z", "appletree", TRUE},
	{"a-z", "AppleTree", FALSE},
	{"a-zA-Z0-9._@\\\\", "appletree", TRUE},
	{"a-zA-Z0-9._@\\\\", "AppleTree", TRUE},
	{"a-zA-Z0-9._@\\\\", "Apple\\Tree", TRUE},
	{NULL, NULL, FALSE}};

gboolean z_charset_test(const gchar *valid, const gchar *test, gboolean *result)
{
  ZCharSet charset;

  z_charset_init(&charset);

  if (!z_charset_parse(&charset, valid))
    {
      printf("problem in parsing valid chars: %s.\n", valid);
      return FALSE;
    }

  *result = z_charset_is_string_valid(&charset, test, -1);

  return TRUE;
}

BOOST_AUTO_TEST_CASE(test_valid_chars)
{
  guint i;
  int ret = 0;

  printf("Test valid chars\n");

  for (i = 0; testcases[i].valid_chars != NULL; i++)
    {
      gboolean result;
      if (z_charset_test(testcases[i].valid_chars, testcases[i].teststring, &result))
        {
          if (result != testcases[i].expected_result)
            {
              printf("Failed\n");
              ret = 1;
            }
          else
            {
              printf("PASS\n");
            }
        }
      else
        {
          printf("Failed\n");
          ret = 1;
        }
    }

  BOOST_CHECK(ret == 0);
}
