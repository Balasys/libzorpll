#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/stream.h>
#include <zorpll/streamssl.h>
#include <zorpll/streamfd.h>
#include <zorpll/log.h>
#include <zorpll/misc.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

gchar testcert[512];
gchar testkey[512];

const std::string msg_from_client_to_server("msg_from_client_to_server");
const std::string msg_from_server_to_client("msg_from_server_to_client");

void
test_server(gint fd)
{
  ZStream *stream;
  ZSSLSession *ssl_session;
  gsize bytes_written, bytes_read;
  gchar text_sent_by_client[512];

  ssl_session = z_ssl_session_new("server/ssl", Z_SSL_MODE_SERVER, testkey, testcert, NULL, NULL, 9, Z_SSL_VERIFY_NONE);
  BOOST_CHECK(ssl_session);

  stream = z_stream_fd_new(fd, "server");
  BOOST_CHECK(stream);
  stream = z_stream_push(stream, z_stream_ssl_new(NULL, ssl_session));
  BOOST_CHECK(stream);

  BOOST_CHECK_EQUAL(SSL_accept(ssl_session->ssl), 1);

  BOOST_CHECK_EQUAL(z_stream_write(stream, msg_from_server_to_client.c_str(), msg_from_server_to_client.length(), &bytes_written, NULL), G_IO_STATUS_NORMAL);
  BOOST_CHECK_EQUAL(msg_from_server_to_client.length(), bytes_written);

  z_stream_read(stream, text_sent_by_client, sizeof(text_sent_by_client), &bytes_read, NULL);
  BOOST_CHECK_EQUAL(std::string(text_sent_by_client, bytes_read), msg_from_client_to_server);
}

void
test_client(gint fd)
{
  ZStream *stream;
  ZSSLSession *ssl_session;
  gchar text_sent_by_server[512];
  gsize bytes_written, bytes_read;

  ssl_session = z_ssl_session_new("client/ssl", Z_SSL_MODE_CLIENT, NULL, NULL, NULL, NULL, 9, Z_SSL_VERIFY_NONE);
  BOOST_CHECK(ssl_session);

  stream = z_stream_fd_new(fd, "client");
  stream = z_stream_push(stream, z_stream_ssl_new(NULL, ssl_session));
  BOOST_CHECK(stream);
  BOOST_CHECK_EQUAL(SSL_connect(ssl_session->ssl), 1);

  BOOST_CHECK_EQUAL(z_stream_write(stream, msg_from_client_to_server.c_str(), msg_from_client_to_server.length(), &bytes_written, NULL), G_IO_STATUS_NORMAL);
  BOOST_CHECK_EQUAL(msg_from_client_to_server.length(), bytes_written);

  z_stream_read(stream, text_sent_by_server, sizeof(text_sent_by_server), &bytes_read, NULL);
  BOOST_CHECK_EQUAL(std::string(text_sent_by_server, bytes_read), msg_from_server_to_client);
}

BOOST_AUTO_TEST_CASE(test_ssl)
{
  gint fds[2], status;

  z_ssl_init();
  g_snprintf(testcert, sizeof(testcert), "%s/testx509.crt", TEST_SRC_DIR);
  g_snprintf(testkey, sizeof(testkey), "%s/testx509.key", TEST_SRC_DIR);

  BOOST_CHECK(socketpair(PF_UNIX, SOCK_STREAM, 0, fds) >= 0);

  if (fork() != 0)
    {
      close(fds[0]);
      test_client(fds[1]);

      wait(&status);
      BOOST_CHECK(status == 0);
    }
  else
    {
      close(fds[1]);
      test_server(fds[0]);
    }
}
