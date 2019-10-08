#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/inetaddress.h>

BOOST_AUTO_TEST_CASE(simple_factory)
{
  constexpr std::tuple<const char *, std::uint8_t> test_data_invalid[] = {
    std::make_tuple("255.255.255.256",           32),
    std::make_tuple("apple",                     32),
    std::make_tuple("2.3.4.5",                   33),
    std::make_tuple("2001:db8::",               129),
    std::make_tuple("::z",                      128),
    std::make_tuple("",                          32),
    std::make_tuple("",                         128)
  };
  for (const auto &data : test_data_invalid)
    BOOST_CHECK(ZInetAddress::make_address(std::get<0>(data), std::get<1>(data)).get() == nullptr);

  constexpr std::tuple<const char *, std::uint8_t, std::uint8_t> test_data_get_netmask[] = {
    std::make_tuple("1.2.3.4",     32, 32),
    std::make_tuple("1.2.3.4",     24, 24),
    std::make_tuple("1.2.3.4",      0, 0),
    std::make_tuple("fc00::aaaa", 128, 128),
    std::make_tuple("fc00::aaaa", 127, 127),
    std::make_tuple("fc00::aaaa",   7, 7),
    std::make_tuple("fc00::aaaa",   0, 0),
  };
  for (const auto &data : test_data_get_netmask)
    {
      const auto address = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_netmask(), std::get<2>(data));
    }

  constexpr std::tuple<const char *, std::uint8_t, const char *> test_data_get_address_string[] = {
    std::make_tuple("1.2.3.4",                                  32, "1.2.3.4"),
    std::make_tuple("0.0.0.0",                                  32, "0.0.0.0"),
    std::make_tuple("255.255.255.255",                          32, "255.255.255.255"),
    std::make_tuple("255.255.255.255",                           0, "255.255.255.255"),
    std::make_tuple("2001:db8::",                              128, "2001:db8::"),
    std::make_tuple("::",                                      128, "::"),
    std::make_tuple("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 127, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
    std::make_tuple("fc00::aaaa",                              128, "fc00::aaaa"),
    std::make_tuple("fc00::aaaa",                                0, "fc00::aaaa")
  };
  for (const auto &data : test_data_get_address_string)
    {
      const auto address = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_address_string(), std::get<2>(data));
    }

  constexpr std::tuple<const char *, std::uint8_t, ZInetFormat, const char *> test_data_get_network_string[] = {
    std::make_tuple("2.3.4.5",                                  32, ZInetFormat::Normal, "2.3.4.5"),
    std::make_tuple("2.3.4.5",                                  24,  ZInetFormat::Slashed, "2.3.4.0/24"),
    std::make_tuple("2.3.4.5",                                  16,  ZInetFormat::Slashed, "2.3.0.0/16"),
    std::make_tuple("254.254.254.0",                             9,  ZInetFormat::Slashed, "254.128.0.0/9"),
    std::make_tuple("0.0.0.0",                                   0,  ZInetFormat::Slashed, "0.0.0.0/0"),
    std::make_tuple("::1",                                     128, ZInetFormat::Normal, "::1"),
    std::make_tuple("fe80::ffff:aabb:cccc:dddd",                64,  ZInetFormat::Slashed, "fe80::/64"),
    std::make_tuple("fe80::ffff:aabb:cccc:dddd",                 8,  ZInetFormat::Slashed, "fe00::/8"),
    std::make_tuple("aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb",  12,  ZInetFormat::Slashed, "aaa0::/12"),
    std::make_tuple("aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb", 120,  ZInetFormat::Slashed, "aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bb00/120"),
    std::make_tuple("aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb", 128, ZInetFormat::Normal, "aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb"),
    std::make_tuple("::",                                        0,  ZInetFormat::Slashed, "::/0")
  };
  for (const auto &data : test_data_get_network_string)
    {
      const auto address = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_network_string(std::get<2>(data)), std::get<3>(data));
    }
}

BOOST_AUTO_TEST_CASE(factory_with_string_netmask)
{
  constexpr std::tuple<const char *, const char *> test_data_invalid[] = {
    std::make_tuple("255.255.255.256",          "32"),
    std::make_tuple("apple",                    "32"),
    std::make_tuple("2.3.4.5",                  "33"),
    std::make_tuple("2001:db8::",               "129"),
    std::make_tuple("::z",                      "128"),
    std::make_tuple("",                         "255.255.255.255"),
    std::make_tuple("",                         "128")
  };
  for (const auto &data : test_data_invalid)
    BOOST_CHECK(ZInetAddress::make_address(std::get<0>(data), std::get<1>(data)).get() == nullptr);

  constexpr std::tuple<const char *, const char *, std::uint8_t> test_data_get_netmask[] = {
    std::make_tuple("1.2.3.4",                            "255.255.255.255", 32),
    std::make_tuple("1.2.3.4",                              "255.255.255.0", 24),
    std::make_tuple("1.2.3.4",                                    "0.0.0.0", 0),
    std::make_tuple("fc00::aaaa", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 128),
    std::make_tuple("fc00::aaaa", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe", 127),
    std::make_tuple("fc00::aaaa",                                  "fc00::", 6),
    std::make_tuple("fc00::aaaa",                                      "::", 0),
  };
  for (const auto &data : test_data_get_netmask)
    {
      const auto address = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_netmask(), std::get<2>(data));
    }

  constexpr std::tuple<const char *, const char *, const char *> test_data_get_address_string[] = {
    std::make_tuple("1.2.3.4",                                 "32",              "1.2.3.4"),
    std::make_tuple("0.0.0.0",                                 "255.255.255.255", "0.0.0.0"),
    std::make_tuple("255.255.255.255",                         "255.255.255.255", "255.255.255.255"),
    std::make_tuple("255.255.255.255",                         "0.0.0.0",         "255.255.255.255"),
    std::make_tuple("2001:db8::",                              "128", "2001:db8::"),
    std::make_tuple("::",                                      "128", "::"),
    std::make_tuple("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "127", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
  };
  for (const auto &data : test_data_get_address_string)
    {
      const auto address = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_address_string(), std::get<2>(data));
    }

  constexpr std::tuple<const char *, const char *, ZInetFormat, const char *> test_data_get_network_string[] = {
    std::make_tuple("2.3.4.5",                                 "255.255.255.255", ZInetFormat::Normal, "2.3.4.5"),
    std::make_tuple("2.3.4.5",                                   "255.255.255.0",  ZInetFormat::Slashed, "2.3.4.0/24"),
    std::make_tuple("2.3.4.5",                                     "255.255.0.0",  ZInetFormat::Slashed, "2.3.0.0/16"),
    std::make_tuple("254.254.254.0",                               "255.128.0.0",  ZInetFormat::Slashed, "254.128.0.0/9"),
    std::make_tuple("0.0.0.0",                                         "0.0.0.0",  ZInetFormat::Slashed, "0.0.0.0/0"),
    std::make_tuple("::1",                                     "128", ZInetFormat::Normal, "::1"),
    std::make_tuple("fe80::ffff:aabb:cccc:dddd",               "ffff:ffff:ffff:ffff::", ZInetFormat::Slashed, "fe80::/64"),
    std::make_tuple("fe80::ffff:aabb:cccc:dddd",               "ff00::", ZInetFormat::Slashed, "fe00::/8"),
    std::make_tuple("aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb", "fff0::", ZInetFormat::Slashed, "aaa0::/12"),
    std::make_tuple("aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb", "120",  ZInetFormat::Slashed, "aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bb00/120"),
    std::make_tuple("aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb", "128", ZInetFormat::Normal, "aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb"),
    std::make_tuple("::",                                      "::",  ZInetFormat::Slashed, "::/0")
  };
  for (const auto &data : test_data_get_network_string)
    {
      const auto address = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_network_string(std::get<2>(data)), std::get<3>(data));
    }
}

BOOST_AUTO_TEST_CASE(factory_from_netmask)
{
  constexpr std::tuple<std::uint8_t, ZInetFamily> test_data_invalid[] = {
    std::make_tuple(128,   ZInetFamily::IPv4)
  };
  for (const auto &data : test_data_invalid)
    BOOST_CHECK(ZInetAddress::make_address(std::get<1>(data), std::get<0>(data)).get() == nullptr);

  constexpr std::tuple<std::uint8_t, ZInetFamily, std::uint8_t> test_data_get_netmask[] = {
    std::make_tuple(0,   ZInetFamily::IPv4, 32),
    std::make_tuple(32,  ZInetFamily::IPv4, 32),
    std::make_tuple(0,   ZInetFamily::IPv6, 128),
    std::make_tuple(32,  ZInetFamily::IPv6, 128),
    std::make_tuple(121, ZInetFamily::IPv6, 128),
    std::make_tuple(128, ZInetFamily::IPv6, 128)
  };
  for (const auto &data : test_data_get_netmask)
    {
      const auto address = ZInetAddress::make_address(std::get<1>(data), std::get<0>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_netmask(), std::get<2>(data));
    }

  constexpr std::tuple<std::uint8_t, ZInetFamily, const char *> test_data_get_address_string[] = {
    std::make_tuple(0,   ZInetFamily::IPv4, "0.0.0.0"),
    std::make_tuple(25,  ZInetFamily::IPv4, "255.255.255.128"),
    std::make_tuple(32,  ZInetFamily::IPv4, "255.255.255.255"),
    std::make_tuple(0,   ZInetFamily::IPv6, "::"),
    std::make_tuple(66,  ZInetFamily::IPv6, "ffff:ffff:ffff:ffff:c000::"),
    std::make_tuple(128, ZInetFamily::IPv6, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
  };
  for (const auto &data : test_data_get_address_string)
    {
      const auto address = ZInetAddress::make_address(std::get<1>(data), std::get<0>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_address_string(), std::get<2>(data));
    }

  constexpr std::tuple<std::uint8_t, ZInetFamily, ZInetFormat, const char *> test_data_get_network_string[] = {
    std::make_tuple(0,   ZInetFamily::IPv4, ZInetFormat::Normal, "0.0.0.0"),
    std::make_tuple(25,  ZInetFamily::IPv4, ZInetFormat::Normal, "255.255.255.128"),
    std::make_tuple(32,  ZInetFamily::IPv4, ZInetFormat::Normal, "255.255.255.255"),
    std::make_tuple(0,   ZInetFamily::IPv6, ZInetFormat::Normal, "::"),
    std::make_tuple(66,  ZInetFamily::IPv6, ZInetFormat::Normal, "ffff:ffff:ffff:ffff:c000::"),
    std::make_tuple(128, ZInetFamily::IPv6, ZInetFormat::Normal, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
  };
  for (const auto &data : test_data_get_network_string)
    {
      const auto address = ZInetAddress::make_address(std::get<1>(data), std::get<0>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_network_string(std::get<2>(data)), std::get<3>(data));
    }
}

BOOST_AUTO_TEST_CASE(factory_from_string_netmask_number)
{
  constexpr std::tuple<const char *, ZInetFamily> test_data_invalid[] = {
    std::make_tuple("128",   ZInetFamily::IPv4)
  };
  for (const auto &data : test_data_invalid)
    BOOST_CHECK(ZInetAddress::make_address(std::get<1>(data), std::get<0>(data)).get() == nullptr);

  constexpr std::tuple<const char *, ZInetFamily, std::uint8_t> test_data_get_netmask[] = {
    std::make_tuple("0",   ZInetFamily::IPv4, 32),
    std::make_tuple("32",  ZInetFamily::IPv4, 32),
    std::make_tuple("0",   ZInetFamily::IPv6, 128),
    std::make_tuple("32",  ZInetFamily::IPv6, 128),
    std::make_tuple("121", ZInetFamily::IPv6, 128),
    std::make_tuple("128", ZInetFamily::IPv6, 128)
  };
  for (const auto &data : test_data_get_netmask)
    {
      const auto address = ZInetAddress::make_address(std::get<1>(data), std::get<0>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_netmask(), std::get<2>(data));
    }

  constexpr std::tuple<const char *, ZInetFamily, const char *> test_data_get_address_string[] = {
    std::make_tuple("0",   ZInetFamily::IPv4, "0.0.0.0"),
    std::make_tuple("25",  ZInetFamily::IPv4, "255.255.255.128"),
    std::make_tuple("32",  ZInetFamily::IPv4, "255.255.255.255"),
    std::make_tuple("0",   ZInetFamily::IPv6, "::"),
    std::make_tuple("66",  ZInetFamily::IPv6, "ffff:ffff:ffff:ffff:c000::"),
    std::make_tuple("128", ZInetFamily::IPv6, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
  };
  for (const auto &data : test_data_get_address_string)
    {
      const auto address = ZInetAddress::make_address(std::get<1>(data), std::get<0>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_address_string(), std::get<2>(data));
    }

  constexpr std::tuple<const char *, ZInetFamily, ZInetFormat, const char *> test_data_get_network_string[] = {
    std::make_tuple("0",   ZInetFamily::IPv4, ZInetFormat::Normal, "0.0.0.0"),
    std::make_tuple("25",  ZInetFamily::IPv4, ZInetFormat::Normal, "255.255.255.128"),
    std::make_tuple("32",  ZInetFamily::IPv4, ZInetFormat::Normal, "255.255.255.255"),
    std::make_tuple("0",   ZInetFamily::IPv6, ZInetFormat::Normal, "::"),
    std::make_tuple("66",  ZInetFamily::IPv6, ZInetFormat::Normal, "ffff:ffff:ffff:ffff:c000::"),
    std::make_tuple("128", ZInetFamily::IPv6, ZInetFormat::Normal, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
  };
  for (const auto &data : test_data_get_network_string)
    {
      const auto address = ZInetAddress::make_address(std::get<1>(data), std::get<0>(data));
      BOOST_CHECK(address.get() != nullptr);
      if (address.get())
        BOOST_CHECK_EQUAL(address->get_network_string(std::get<2>(data)), std::get<3>(data));
    }
}

BOOST_AUTO_TEST_CASE(is_on_same_network)
{
  constexpr std::tuple<const char *, std::uint8_t, const char *, std::uint8_t, bool> test_data[] = {
    std::make_tuple("192.168.0.1",  32, "192.168.0.1",  32, true),
    std::make_tuple("192.168.0.1",  24, "192.168.0.5",  24, true),
    std::make_tuple("192.168.0.1",   8, "192.168.0.5",  24, false),
    std::make_tuple("192.168.0.1",  16, "192.169.0.1",  16, false),
    std::make_tuple("4.3.2.1",       0,     "1.2.3.4",   0, true),
    std::make_tuple("::1",         128,         "::1", 128, true),
    std::make_tuple("::1",          59,         "::1",  55, true),
    std::make_tuple("1.2.3.4",      32,  "2001:db8::",  32, false),
    std::make_tuple("2101:db8::",    8,  "2001:db8::",   8, false),
    std::make_tuple("::1",           0,         "::1",   0, true)
  };
  for (const auto &data : test_data)
    {
      const auto address1 = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      const auto address2 = ZInetAddress::make_address(std::get<2>(data), std::get<3>(data));
      BOOST_CHECK(address1.get() != nullptr);
      BOOST_CHECK(address2.get() != nullptr);
      BOOST_CHECK_EQUAL(address1->is_on_same_network(address2.get()), std::get<4>(data));
    }
}

BOOST_AUTO_TEST_CASE(is_network_intersect)
{
  constexpr std::tuple<const char *, std::uint8_t, const char *, std::uint8_t, bool> test_data[] = {
    std::make_tuple("192.168.0.1",            32,           "192.168.0.1",  32, true),
    std::make_tuple("192.168.0.1",            32,           "192.168.0.1",  24, true),
    std::make_tuple("192.168.0.1",            32,           "192.168.0.1",   0, true),
    std::make_tuple("192.168.0.1",            32,           "192.168.1.2",  16, true),
    std::make_tuple("192.168.0.1",            16,           "192.169.0.1",  16, false),
    std::make_tuple("4.3.2.1",                 0,               "1.2.3.4",   0, true),
    std::make_tuple("4.3.2.1",                32,               "1.2.3.4",  32, false),
    std::make_tuple("aaaa:bbbb:cccc:dddd::", 128, "aaaa:bbbb:cccc:dddd::", 128, true),
    std::make_tuple("aaaa:bbbb:cccc:dddd::", 128, "aaaa:bbbb:cccc:dddd::",  24, true),
    std::make_tuple("aaaa:bbbb:cccc:dddd::", 128, "aaaa:bbbb:cccc:dddd::",   0, true),
    std::make_tuple("aaaa:bbbb:cccc:dddd::", 128, "aaff:bbbb:cccc:dddd::",   8, true),
    std::make_tuple("aaaa:bbbb:cccc:dddd::", 128, "aaff:bbbb:cccc:dddd::",  16, false),
    std::make_tuple("aaaa:bbbb:cccc:dddd::",   0, "bbbb:aaaa:dddd:cccc::",   0, true),
    std::make_tuple("aaaa:bbbb:cccc:dddd::", 128, "bbbb:aaaa:dddd:cccc::", 128, false)
  };
  for (const auto &data : test_data)
    {
      const auto address1 = ZInetAddress::make_address(std::get<0>(data), std::get<1>(data));
      const auto address2 = ZInetAddress::make_address(std::get<2>(data), std::get<3>(data));
      BOOST_CHECK(address1.get() != nullptr);
      BOOST_CHECK(address2.get() != nullptr);
      BOOST_CHECK_EQUAL(address1->is_network_intersect(address2.get()), std::get<4>(data));
    }
}
