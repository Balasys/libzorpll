#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/stream.h>
#include <zorpll/streamfd.h>
#include <zorpll/streambuf.h>
#include <zorpll/streamline.h>
#include <zorpll/streamgzip.h>
#include <zorpll/log.h>
#include <zorpll/poll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

int
test_stream_unget(void)
{
  ZStream *stream;
  gchar *buf = "12345678901234567890";
  gchar testbuf[20];
  gint fds[2], i;
  gsize br;
  gint res = 0;

  if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      perror("socketpair");
      return 1;
    }

  if (write(fds[1], "hoppala", 7) < 0)
    {
      perror("write");
      res = 1;
    }

  close(fds[1]);
  stream = z_stream_fd_new(fds[0], "stdin");
  z_stream_unget(stream, buf, strlen(buf), NULL);

  for (i = 0; i < (int)sizeof(testbuf); i++)
    {
      if (z_stream_read(stream, &testbuf[i], 1, &br, NULL) != G_IO_STATUS_NORMAL)
        {
          perror("z_stream_read");
          res = 1;
        }
      else if (testbuf[i] != buf[i])
        {
          fprintf(stderr, "Invalid data read\n");
          res = 1;
        }
    }
  if (z_stream_read(stream, testbuf, sizeof(testbuf), &br, NULL) != G_IO_STATUS_NORMAL)
    {
      perror("z_stream_read2");
    }
  if (memcmp(testbuf, "hoppala", 7) != 0)
    {
      fprintf(stderr, "Invalid data read\n");
      res = 1;
    }
  z_stream_close(stream, NULL);
  z_stream_unref(stream);
  return res;
}

int
test_streambuf(void)
{
  ZStream *stream;
  gint fds[2];
  ZPoll *poll;
  gsize bw;
  gint res = 1, i;

  if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      perror("socketpair");
      return 1;
    }

  poll = z_poll_new();
  stream = z_stream_buf_new(z_stream_fd_new(fds[0], "fdstream"), 4096, 0);
  z_poll_add_stream(poll, stream);

  /* generate lots of messages */
  for (i = 0; i < 1000; i++)
    {
      if (z_stream_write(stream, "ABCDEF", 6, &bw, NULL) != G_IO_STATUS_NORMAL)
        {
          fprintf(stderr, "z_stream_buf_write returned non-normal status\n");
          goto exit;
        }
    }

  i = 0;
  while (z_poll_iter_timeout(poll, 100) && i < 1000)
    {
      i++;
    }

  z_stream_shutdown(stream, SHUT_RDWR, NULL);
  for (i = 0; i < 1000; i++)
    {
      gchar buf[16];
      if (read(fds[1], buf, 6) < 0)
        {
          perror("read");
          goto exit;
        }
      if (memcmp(buf, "ABCDEF", 6) != 0)
        {
          fprintf(stderr, "comparison mismatch; buf=[%.*s]\n", 6, buf);
          goto exit;
        }
    }
  res = 0;
 exit:
  z_stream_close(stream, NULL);
  z_stream_unref(stream);
  close(fds[1]);
  return res;
}

int
test_streamline(void)
{
  ZStream *stream;
  gint fds[2];
  gint res = 1;
  gchar *line;
  gsize length;

  if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      perror("socketpair");
      return 1;
    }
  stream = z_stream_line_new(z_stream_fd_new(fds[0], "fdstream"), 4096, ZRL_RETURN_EOL);

  g_return_val_if_fail(write(fds[1], "abcdef\r\n", 8) == 8, 1);

  if (z_stream_line_get(stream, &line, &length, NULL) != G_IO_STATUS_NORMAL)
    {
      fprintf(stderr, "z_stream_line_get returned non-normal status\n");
      goto exit;
    }
  if (strncmp(line, "abcdef\r\n", 8) != 0)
    {
      fprintf(stderr, "comparison mismatch, line='%*.s'", (int)length, line);
      goto exit;
    }
  res = 0;

 exit:
  z_stream_close(stream, NULL);
  z_stream_unref(stream);
  close(fds[1]);
  return res;
}

int
test_streamgzip_with_headers(void)
{
  ZStream *stream, *fdstream;
  gint fds[2];
  gint res = 1;
  gchar contents[32];
  gsize length;
  gchar compressed_file[] = 
    "\x1f\x8b\x08\x1c\x0c\x4f\xc9\x43\x00\x03" 
    "\x05\x00" "extra" /* extra field, 2 byte length field in little-endian */
    "abcdef\x00"       /* original filename, NUL terminated */ 
    "comment\x00"      /* comment, NUL terminated */
    "\x4b\x4c\x4a\x4e\x49\x4d\x03\x00\xef\x39\x8e\x4b\x06\x00\x00"
    "\x00";
  time_t ts;
  gchar *origname;
  gchar *comment;
  gchar *extra;
  gint extra_len;

  if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      perror("socketpair");
      return 1;
    }
  fdstream = z_stream_fd_new(fds[0], "fdstream");

  g_return_val_if_fail(write(fds[1], compressed_file, sizeof(compressed_file)) == sizeof(compressed_file), 1);

  stream = z_stream_gzip_new(fdstream, Z_SGZ_GZIP_HEADER, 6, 32768);

  if (!z_stream_gzip_fetch_header(stream, NULL))
    {
      fprintf(stderr, "z_stream_gzip_fetch_header returned error\n");
      goto exit;
    }

  z_stream_gzip_get_header_fields(stream, &ts, &origname, &comment, &extra_len, &extra);

  if (extra_len != 5 || strncmp(extra, "extra", strlen("extra")) != 0)
    {
      fprintf(stderr, "extra mismatch %.*s\n", extra_len, extra);
      goto exit;
    }
  if (strcmp(origname, "abcdef") != 0)
    {
      fprintf(stderr, "Original filename mismatch %s\n", origname);
      goto exit;
    }

  if (strcmp(comment, "comment") != 0)
    {
      fprintf(stderr, "Comment mismatch\n");
      goto exit;
    }

  if (z_stream_read(stream, contents, sizeof(contents), &length, NULL) != G_IO_STATUS_NORMAL)
    {
      fprintf(stderr, "z_stream_read returned non-normal status\n");
      goto exit;
    }
  if (length != 6 || strncmp(contents, "abcdef", 6) != 0)
    {
      fprintf(stderr, "comparison mismatch, content='%.*s'", (int)length, contents);
      goto exit;
    }
  res = 0;

 exit:
  z_stream_close(stream, NULL);
  z_stream_unref(stream);
  close(fds[1]);
  return res;
}

int
test_streamgzip_no_headers(void)
{
  ZStream *stream, *fdstream;
  gint fds[2];
  gint res = 1;
  gchar contents[32];
  gsize length;
  gchar compressed_file[] = 
    "x\x9cKLJNIM\x03\x00\x08\x1e\x02V";

  if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      perror("socketpair");
      return 1;
    }
  fdstream = z_stream_fd_new(fds[0], "fdstream");

  g_return_val_if_fail(write(fds[1], compressed_file, sizeof(compressed_file)) == sizeof(compressed_file), 1);

  stream = z_stream_gzip_new(fdstream, 0, 6, 32768);

  if (!z_stream_gzip_fetch_header(stream, NULL))
    {
      fprintf(stderr, "z_stream_gzip_fetch_header returned error\n");
      goto exit;
    }

  if (z_stream_read(stream, contents, sizeof(contents), &length, NULL) != G_IO_STATUS_NORMAL)
    {
      fprintf(stderr, "z_stream_read returned non-normal status\n");
      goto exit;
    }
  if (length != 6 || strncmp(contents, "abcdef", 6) != 0)
    {
      fprintf(stderr, "comparison mismatch, content='%.*s'", (int)length, contents);
      goto exit;
    }
  res = 0;

 exit:
  z_stream_close(stream, NULL);
  z_stream_unref(stream);
  close(fds[1]);
  return res;
}

BOOST_AUTO_TEST_CASE(test_streams)
{
  gint res;

  res = test_stream_unget();
  if (res == 0)
    res = test_streambuf();
  if (res == 0)
    res = test_streamline();
  if (res == 0)
    res = test_streamgzip_with_headers();
  if (res == 0)
    res = test_streamgzip_no_headers();

  BOOST_CHECK(res == 0);
}
