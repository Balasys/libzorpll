#ifndef ZORP_INETADDRESS_H_INCLUDED
#define ZORP_INETADDRESS_H_INCLUDED

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

enum class ZInetFamily
{
  IPv4,
  IPv6
};

enum class ZInetFormat
{
  Normal,
  Slashed
};

class ZInetAddress final
{
public:
  ~ZInetAddress() = default;

  ZInetAddress(const ZInetAddress &) = default;
  ZInetAddress &operator=(const ZInetAddress &) = default;

  static std::unique_ptr<ZInetAddress> make_address(const std::string &address);
  static std::unique_ptr<ZInetAddress> make_address(const std::string &address, const std::uint8_t netmask);
  static std::unique_ptr<ZInetAddress> make_address(const std::string &address, const std::string &netmask);
  static std::unique_ptr<ZInetAddress> make_address(const ZInetFamily family, const std::uint8_t netmask);
  static std::unique_ptr<ZInetAddress> make_address(const ZInetFamily family, const std::string &netmask);

  ZInetFamily get_family() const;
  std::uint8_t get_netmask() const;
  std::string get_address_string() const;
  std::string get_network_string(const ZInetFormat format = ZInetFormat::Normal) const;
  bool is_on_same_network(const ZInetAddress *rhs) const;
  bool is_network_intersect(const ZInetAddress *rhs) const;

private:
  class ZInetData {
  public:
    ZInetData() = default;
    ZInetData(const ZInetData &) = default;
    ZInetData &operator=(const ZInetData &) = default;

    bool operator==(const ZInetData &rhs) {
      return (family == rhs.family) && (address == rhs.address);
    }

    ZInetFamily family = ZInetFamily::IPv4;
    std::array<std::uint32_t, 4> address = std::array<std::uint32_t, 4>();
  };

  ZInetAddress(const ZInetData &data, const std::uint8_t mask) :
    inet_data(data), netmask(mask) {}

  static bool string_to_internal_ipv4(const std::string &address, ZInetData &inet);
  static bool string_to_internal_ipv6(const std::string &address, ZInetData &inet);

  static ZInetData netmask_to_internal(const std::uint8_t netmask, const ZInetFamily family);
  static std::uint8_t internal_to_netmask(const ZInetData &inet);

  static bool is_valid_netmask(const ZInetData &inet);

  static bool netmask_number_string_to_int(const std::string &netmask, std::uint8_t &netmask_number);

  static std::string internal_to_string(const ZInetData &inet);

  ZInetData get_network() const;

  ZInetData inet_data;
  std::uint8_t netmask;
};

#endif
