#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/packetbuf.h>
#include <glib.h>
#include <string.h>

BOOST_AUTO_TEST_CASE(test_pktbuf_split)
{
  const char data[] = "first.last";
  const gsize at = 5;
  ZPktBuf *buf, *newbuf;

  buf = z_pktbuf_new();
  z_pktbuf_copy(buf, data, sizeof(data));
  newbuf =  z_pktbuf_split(buf, at);

  BOOST_CHECK(z_pktbuf_length(buf) == 5); /* "first", without terminating 0 */
  BOOST_CHECK(memcmp(buf->data, "first", z_pktbuf_length(buf)) == 0);

  BOOST_CHECK(z_pktbuf_length(newbuf) == 6); /* ".last", with terminating 0 */
  BOOST_CHECK(memcmp(newbuf->data, ".last", z_pktbuf_length(newbuf)) == 0);

  z_pktbuf_unref(buf);
  z_pktbuf_unref(newbuf);
}

BOOST_AUTO_TEST_CASE(test_pktbuf_append_pktbuf)
{
  ZPktBuf *buf, *other;

  buf = z_pktbuf_new();

  other = z_pktbuf_new();
  z_pktbuf_put_string(other, "qwerty");

  z_pktbuf_append_pktbuf(buf, other);

  BOOST_CHECK(z_pktbuf_length(buf) == 6);
  BOOST_CHECK(memcmp(buf->data, "qwerty", z_pktbuf_length(buf)) == 0);

  other = z_pktbuf_new();
  z_pktbuf_append_pktbuf(buf, other);

  BOOST_CHECK(z_pktbuf_length(buf) == 6);
  BOOST_CHECK(memcmp(buf->data, "qwerty", z_pktbuf_length(buf)) == 0);

  other = z_pktbuf_new();
  z_pktbuf_put_string(other, "qwerty");

  z_pktbuf_append_pktbuf(buf, other);

  BOOST_CHECK(z_pktbuf_length(buf) == 12);
  BOOST_CHECK(memcmp(buf->data, "qwertyqwerty", z_pktbuf_length(buf)) == 0);
}
