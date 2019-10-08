#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/listen.h>
#include <zorpll/connect.h>
#include <zorpll/sockaddr.h>

#include <stdio.h>

GMainLoop *loop;
gint counter = 0;

static void
test_done(void)
{
  counter++;
  if (counter == 2)
    g_main_quit(loop);
}

static gboolean
test_accepted(ZStream * /* stream */, ZSockAddr *client, ZSockAddr *dest, gpointer /* user_data */)
{
  printf("Connection accepted\n");
  z_sockaddr_unref(client);
  z_sockaddr_unref(dest);
  test_done();
  return TRUE;
}

static void
test_connected(ZStream * /* fdstream */, GError * /* error */, gpointer /* user_data */)
{
  printf("Connected\n");
  test_done();
}

class TempFile
{
public:
   TempFile() { name = tmpnam(NULL); };
   ~TempFile() { unlink(name); }
   char *get() { return name; }
private:
   char *name;
};

BOOST_AUTO_TEST_CASE(test_conns)
{
  ZSockAddr *a;
  ZListener *l;
  ZConnector *c;
  ZSockAddr *dest;
  TempFile socket_name;

  loop = g_main_loop_new(NULL, TRUE);

  a = z_sockaddr_unix_new(socket_name.get());
  l = z_stream_listener_new("sessionid", a, 0, 255, test_accepted, NULL);
  BOOST_REQUIRE(z_listener_start(l));

  c = z_stream_connector_new("sessionid", NULL, a, 0, test_connected, NULL, NULL);
  z_connector_start(c, &dest);
  while (g_main_loop_is_running(loop))
    {
      g_main_context_iteration(NULL, TRUE);
    }
  z_listener_unref(l);
  z_connector_unref(c);
}
