#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/misc.h>
#include <string.h>
#include <stdlib.h>


class ZCryptConfig
{
public:
  ZCryptConfig()
    {
    }
  ~ZCryptConfig()
    {
    }

  bool testcase(char *pw, char *salt, char *expected)
    {
      char result[128];

      z_crypt(pw, salt, result, sizeof(result));
      return strcmp(result, expected) == 0;
    }

};

BOOST_FIXTURE_TEST_SUITE(testsuite, ZCryptConfig)

BOOST_AUTO_TEST_CASE(test_zcrypt)
{
  BOOST_CHECK(testcase("titkos", "$1$abcdef$", "$1$abcdef$tViuCKijOibTb1mxJ.nuL1"));
  BOOST_CHECK(testcase("titkos", "$1$abc$", "$1$abc$.CtgYDt9Kysbluq2wuHVL0"));
  BOOST_CHECK(testcase("titkos", "$1$abc$", "$1$abc$.CtgYDt9Kysbluq2wuHVL0"));
  BOOST_CHECK(testcase("titkos", "$1$01234567$", "$1$01234567$8.GchdyyhO1de8.vYREOZ1"));
  BOOST_CHECK(testcase("titkos", "$1$0123456789$", "$1$01234567$8.GchdyyhO1de8.vYREOZ1"));
}

BOOST_AUTO_TEST_SUITE_END()
