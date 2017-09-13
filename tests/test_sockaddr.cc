#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/sockaddr.h>
#include <stdio.h>
#include <stdlib.h>

BOOST_AUTO_TEST_CASE(test_sockaddr)
{
  ZSockAddr *a, *b;
  struct sockaddr_in s_sin;
  struct sockaddr_un s_sun;

  /* AF_INET */

  a = z_sockaddr_inet_new("1.2.3.4", 5555);
  memset(&s_sin, 0xaa, sizeof(s_sin));
  s_sin.sin_family = AF_INET;
  z_inet_aton("1.2.3.4", &s_sin.sin_addr);
  s_sin.sin_port = htons(5555);
  b = z_sockaddr_new((struct sockaddr *) &s_sin, sizeof(s_sin));

  BOOST_CHECK(z_sockaddr_equal(a, b) == TRUE);

  z_sockaddr_unref(a);
  z_sockaddr_unref(b);

  /* AF_UNIX */

  a = z_sockaddr_unix_new("abcdef");
  memset(&s_sun, 0xaa, sizeof(s_sun));
  s_sun.sun_family = AF_UNIX;
  strncpy(s_sun.sun_path, "abcdef", sizeof(s_sun.sun_path));
  b = z_sockaddr_new((struct sockaddr *) &s_sun, sizeof(s_sun));

  BOOST_CHECK(z_sockaddr_equal(a, b) == TRUE);

  z_sockaddr_unref(a);
  z_sockaddr_unref(b);
}
