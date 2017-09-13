#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/registry.h>

#include <stdio.h>

BOOST_AUTO_TEST_CASE(test_registry)
{
  int i;
  char buf[128];

  z_registry_init();

  for (i = 0; i < 10; i++)
    {
      snprintf(buf, sizeof(buf), "key%d", i);
      z_registry_add(buf, ZR_PROXY, GINT_TO_POINTER(i));
    }
  for (i = 0; i < 10; i++) 
    {
      gint res;
      gint type = 0;

      snprintf(buf, sizeof(buf), "key%d", i);
      res = GPOINTER_TO_UINT(z_registry_get(buf, &type));
      BOOST_CHECK(res == i);
      BOOST_CHECK(type == ZR_PROXY);
    }

  z_registry_destroy();
}
