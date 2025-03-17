#include <iostream>
#include <print>
#include <utility>

template <auto value>
constexpr auto enum_name() {
  auto name = std::string_view{__PRETTY_FUNCTION__};
  auto start = name.find('=') + 2;
  auto end = name.size() - 1;
  name = std::string_view{name.data() + start, end - start};
  start = name.rfind("::");
  return start == std::string_view::npos ? name : std::string_view{name.data() + start + 2, name.size() - start - 2};
}

template <typename Enum>
  requires std::is_enum_v<Enum>
constexpr auto enum_name(Enum value) -> std::string_view {
  constexpr static auto names = []<std::size_t... Is>(std::index_sequence<Is...>) {
    return std::array<std::string_view, std::to_underlying(Enum::sentinel)>{
        enum_name<static_cast<Enum>(Is)>()...};
  }(std::make_index_sequence<std::to_underlying(Enum::sentinel)>{});

  if (value < Enum::sentinel)
    return names[static_cast<std::size_t>(value)];
  else
    return "unknown";
}

enum class color {
  red,
  blue,
  green,
  sentinel,
};

auto main() -> int {
  auto c = color::red;
  std::println("{}", enum_name(c));

  c = color::sentinel;
  std::println("{}", enum_name(c));

  c = static_cast<color>(3);
  std::println("{}", enum_name(c));

  return 0;
}