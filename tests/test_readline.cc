#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/streamfd.h>
#include <zorpll/streamline.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

const gchar *expected_outputs[] =
{
  "1",
  "2",
  "3",
  "01234567",
  "012345678", //truncates "9abc"
  "abc",
  "abcdef",
  "012345678", //truncates 90123456789012345678"
  "abcdef",
  "012345678",
  "abcdef"
};

BOOST_AUTO_TEST_CASE(test_readline)
{
  ZStream *input;
  int pair[2], status = 0;
  pid_t pid;
  gchar *line;
  gsize length;

  BOOST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) >= 0);

  pid = fork();
  BOOST_CHECK(pid >= 0);

  if (pid != 0)
    {
      close(pair[1]);
      BOOST_CHECK(write(pair[0], "1\n2\n3\n", 6) == 6);
      BOOST_CHECK(write(pair[0], "0123", 4) == 4);
      BOOST_CHECK(write(pair[0], "4567\n", 5) == 5);
      BOOST_CHECK(write(pair[0], "0123456789", 10) == 10);
      BOOST_CHECK(write(pair[0], "abc\nAabc\nabcdef\n", 16) == 16); /* Because of truncate A will be eliminated */
      BOOST_CHECK(write(pair[0], "0123456789", 10) == 10);
      BOOST_CHECK(write(pair[0], "0123456789", 10) == 10);
      BOOST_CHECK(write(pair[0], "012345678\nAabcdef\n", 18) == 18); /* Because of truncate A will be eliminated */
      BOOST_CHECK(write(pair[0], "012345678\nAabcdef", 17) == 17);
      close(pair[0]);
      waitpid(pid, &status, 0);
    }
  else
    {
      gint rc;
      guint i;

      printf("%d\n", getpid());
      sleep(1);
      close(pair[0]);
      input = z_stream_fd_new(pair[1], "sockpair input");
      input = z_stream_line_new(input, 10, ZRL_EOL_NL | ZRL_TRUNCATE);

      i = 0;

      if (!z_stream_unget(input, "A", 1, NULL))
        _exit(1);
      rc = z_stream_line_get(input, &line, &length, NULL);
      while (rc == G_IO_STATUS_NORMAL)
        {
          if (!(i < (sizeof(expected_outputs) / sizeof(gchar *)) &&
                              line[0] == 'A' &&
                              strlen(expected_outputs[i]) == length - 1 &&
                              memcmp(expected_outputs[i], line + 1, length - 1) == 0))
            {
              _exit(1);
            }
          printf("line ok: %.*s\n", (int) length, line);
          if (!z_stream_unget(input, "A", 1, NULL))
            _exit(1);
          rc = z_stream_line_get(input, &line, &length, NULL);
          i++;
        }
      close(pair[1]);
      _exit(!(i >= (sizeof(expected_outputs) / sizeof(gchar *))));
    }
  BOOST_CHECK((status >> 8) == 0);
}
