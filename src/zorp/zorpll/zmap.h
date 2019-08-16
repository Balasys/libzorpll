#ifndef ZORP_ZMAP_H_INCLUDED
#define ZORP_ZMAP_H_INCLUDED

#include <array>
#include <stdexcept>
#include <utility>

/**
 * ZMap is a generic associative container which can be used at
 * compile time.
 *
 * Usage example:
 *
 * int main()
 * {
 *   enum class MyEnum : std::size_t
 *   {
 *     Apple,
 *     Banana,
 *     Coconut,
 *     Strawberry
 *   };
 *
 *   constexpr auto enum_map = make_z_map(
 *     std::make_pair(MyEnum::Apple, std::string_view("Hype")),
 *     std::make_pair(MyEnum::Banana, std::string_view("Milkshake")),
 *     std::make_pair(MyEnum::Coconut, std::string_view("Hawaii"))
 *   );
 *
 *   constexpr auto key = z_map_get_key(enum_map, std::string_view("Hawaii"));
 *   constexpr auto value = z_map_get_value(enum_map, MyEnum::Coconut);
 *   std::cout << static_cast<std::size_t>(key) << " " << value << std::endl;
 *
 *   // Compilation will fail, because there is no associated std::string_view value to this MyEnum key:
 *   constexpr auto value_not_found = z_map_get_value(enum_map, MyEnum::Strawberry);
 *
 *   // Compilation will fail, because there is no associated MyEnum key to this std::string_view value:
 *   constexpr auto key_not_found = z_map_get_key(enum_map, std::string_view("Ice"));
 * }
 **/

template<typename E, typename T, std::size_t N>
using ZMap = std::array<std::pair<E, T>, N>;

template<typename E, typename T, std::size_t N>
constexpr T
z_map_get_value(const ZMap<E, T, N> &map, const E key, std::size_t i = 0)
{
  return (i == map.size()) ? throw std::invalid_argument("Key is not present in map!") :
    (map[i].first == key) ? map[i].second :
    z_map_get_value(map, key, i + 1);
}

template<typename E, typename T, std::size_t N>
constexpr E
z_map_get_key(const ZMap<E, T, N> &map, const T &value, std::size_t i = 0)
{
  return (i == map.size()) ? throw std::invalid_argument("Value is not present in map!") :
    (map[i].second == value) ? map[i].first :
    z_map_get_key(map, value, i + 1);
}

template<typename... E, typename... T>
constexpr auto make_z_map(std::pair<E, T>&&... args)
  -> ZMap<std::common_type_t<E...>, std::common_type_t<T...>, sizeof...(args)>
{
  return {std::forward<std::pair<E, T>>(args)...};
}

#endif // ZORP_ZMAP_H_INCLUDED
