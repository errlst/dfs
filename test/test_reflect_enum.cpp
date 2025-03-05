#include <boost/preprocessor.hpp>
#include <iostream>

#define PROCESS_ONE_ELEMENT(r, unused, idx, elem) \
  BOOST_PP_COMMA_IF(idx)                          \
  BOOST_PP_STRINGIZE(elem)

#define ENUM_MACRO(name, ...)                                                                                               \
  enum class name { __VA_ARGS__ };                                                                                          \
  const char *name##Strings[] = {BOOST_PP_SEQ_FOR_EACH_I(PROCESS_ONE_ELEMENT, % %, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))}; \
  template <typename T>                                                                                                     \
  constexpr const char *name##ToString(T value) { return name##Strings[static_cast<int>(value)]; }

ENUM_MACRO(Esper, Unu, Du, Tri, Kvar, Kvin, Ses, Sep, Ok, Naux, Dek);

int main() {
  std::cout << EsperToString(Esper::Kvin) << std::endl;
}