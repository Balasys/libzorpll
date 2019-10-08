#include <zorpll/inetaddress.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <cstdlib>
#include <limits>

std::unique_ptr<ZInetAddress>
ZInetAddress::make_address(const std::string &address)
{
  ZInetData data;

  if (string_to_internal_ipv4(address, data))
    return std::unique_ptr<ZInetAddress>(new ZInetAddress(data, 32));
  else if (string_to_internal_ipv6(address, data))
    return std::unique_ptr<ZInetAddress>(new ZInetAddress(data, 128));

  return nullptr;
}

std::unique_ptr<ZInetAddress>
ZInetAddress::make_address(const std::string &address, const std::uint8_t netmask)
{
  ZInetData data;

  if ((string_to_internal_ipv4(address, data) && netmask <= 32) ||
      (string_to_internal_ipv6(address, data) && netmask <= 128))
    return std::unique_ptr<ZInetAddress>(new ZInetAddress(data, netmask));

  return nullptr;
}

std::unique_ptr<ZInetAddress>
ZInetAddress::make_address(const std::string &address, const std::string &netmask)
{
  std::uint8_t prefix = 0;
  if (netmask_number_string_to_int(netmask, prefix))
    return make_address(address, prefix);

  ZInetData data_address;
  ZInetData data_netmask;

  if ((string_to_internal_ipv4(address, data_address) && string_to_internal_ipv4(netmask, data_netmask)) ||
      (string_to_internal_ipv6(address, data_address) && string_to_internal_ipv6(netmask, data_netmask)))
    {
      if (is_valid_netmask(data_netmask))
        return make_address(address, internal_to_netmask(data_netmask));
    }

  return nullptr;
}

std::unique_ptr<ZInetAddress>
ZInetAddress::make_address(const ZInetFamily family, const std::uint8_t netmask)
{
  if (netmask <= 32 && family == ZInetFamily::IPv4)
    return std::unique_ptr<ZInetAddress>(new ZInetAddress(netmask_to_internal(netmask, family), 32));
  else if (netmask <= 128 && family == ZInetFamily::IPv6)
    return std::unique_ptr<ZInetAddress>(new ZInetAddress(netmask_to_internal(netmask, family), 128));

  return nullptr;
}

std::unique_ptr<ZInetAddress>
ZInetAddress::make_address(const ZInetFamily family, const std::string &netmask)
{
  std::uint8_t prefix = 0;
  if (netmask_number_string_to_int(netmask, prefix))
    return make_address(family, prefix);

  return nullptr;
}

ZInetFamily
ZInetAddress::get_family() const
{
  return inet_data.family;
}

std::uint8_t
ZInetAddress::get_netmask() const
{
  return netmask;
}

std::string
ZInetAddress::get_address_string() const
{
  return internal_to_string(inet_data);
}

std::string
ZInetAddress::get_network_string(const ZInetFormat format) const
{
  auto str = internal_to_string(get_network());
  if (format == ZInetFormat::Slashed)
    str += "/" + std::to_string(netmask);

  return str;
}

bool
ZInetAddress::is_on_same_network(const ZInetAddress *rhs) const
{
  return get_network() == rhs->get_network();
}

bool
ZInetAddress::is_network_intersect(const ZInetAddress *rhs) const
{
  if (get_family() != rhs->get_family())
    return false;

  const auto network_a = get_network();
  const auto network_b = rhs->get_network();
  const auto common_prefix = std::min(get_netmask(), rhs->get_netmask());
  for (std::uint8_t i = 0; i < common_prefix; ++i)
    {
      const auto mask = htonl(static_cast<std::uint32_t>(1 << (31 - (i % 32))));
      if ((network_a.address[i / 32] & mask) != (network_b.address[i / 32] & mask))
        return false;
    }

  return true;
}

bool
ZInetAddress::string_to_internal_ipv4(const std::string &address, ZInetData &inet)
{
  inet.family = ZInetFamily::IPv4;

  return inet_pton(AF_INET, address.c_str(), inet.address.data());
}

bool
ZInetAddress::string_to_internal_ipv6(const std::string &address, ZInetData &inet)
{
  inet.family = ZInetFamily::IPv6;

  return inet_pton(AF_INET6, address.c_str(), inet.address.data());
}

ZInetAddress::ZInetData
ZInetAddress::netmask_to_internal(const std::uint8_t netmask, const ZInetFamily family)
{
  ZInetData data;
  data.family = family;

  if (netmask == 0)
    return data;

  if (netmask <= 32)
    {
      data.address[0] = htonl(std::numeric_limits<std::uint32_t>::max() << (32 - netmask));
    }
  else if (netmask <= 128)
    {
      std::int8_t mask = netmask;
      for (auto &p : data.address)
        {
          mask -= 32;
          if (mask > 0)
            {
              p = std::numeric_limits<std::uint32_t>::max();
            }
          else if (mask == 0)
            {
              p = std::numeric_limits<std::uint32_t>::max();
              break;
            }
          else if (mask < 0)
            {
              p = htonl(std::numeric_limits<std::uint32_t>::max() << (mask * -1));
              break;
            }
        }
    }

  return data;
}

std::uint8_t
ZInetAddress::internal_to_netmask(const ZInetData &inet)
{
  const std::uint8_t size = (inet.family == ZInetFamily::IPv6) ? 4 : 1;
  std::uint8_t ones = 0;
  bool end = false;

  for (std::uint8_t i = 0; i < size && !end; ++i)
    {
      for (std::int8_t ii = 31; ii >= 0 && !end; --ii)
        {
          if (inet.address[i] & htonl(static_cast<std::uint32_t>(1 << ii)))
            ++ones;
          else
            end = true;
        }
    }

  return ones;
}

bool
ZInetAddress::is_valid_netmask(const ZInetData &inet)
{
  bool zero_found = false;
  for (std::uint8_t i = 0; i < inet.address.size(); ++i)
    {
      for (std::int8_t ii = 31; ii >= 0; --ii)
        {
          if (!(inet.address[i] & htonl(static_cast<std::uint32_t>(1 << ii))))
            zero_found = true;
          else if (zero_found)
            return false;
        }
    }

  return true;
}

bool
ZInetAddress::netmask_number_string_to_int(const std::string &netmask, std::uint8_t &netmask_number)
{
  if (netmask.length() > 3)
    return false;

  char *end = nullptr;
  const unsigned long prefix = std::strtoul(netmask.c_str(), &end, 10);

  if (end == (netmask.data() + netmask.size()) && prefix != std::numeric_limits<unsigned long>::max() && prefix <= 128)
    {
      netmask_number = static_cast<std::uint8_t>(prefix);
      return true;
    }

  return false;
}

std::string
ZInetAddress::internal_to_string(const ZInetData &inet)
{
  std::array<char, INET6_ADDRSTRLEN> buffer = {};

  const int family = (inet.family == ZInetFamily::IPv6) ? AF_INET6 : AF_INET;

#ifdef _WIN32
  ZInetData non_const_inet = inet;
  inet_ntop(family, non_const_inet.address.data(), buffer.data(), buffer.size());
#else
  inet_ntop(family, inet.address.data(), buffer.data(), buffer.size());
#endif

  return buffer.data();
}

ZInetAddress::ZInetData
ZInetAddress::get_network() const
{
  const auto mask = netmask_to_internal(netmask, get_family());

  auto network = inet_data;
  const std::uint8_t size = (get_family() == ZInetFamily::IPv6) ? 4 : 1;
  for (std::uint8_t i = 0; i < size; ++i)
    network.address[i] &= mask.address[i];

  return network;
}
