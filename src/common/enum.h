#pragma once
#include <boost/preprocessor.hpp>

#define PROCESS_ONE_ELEMENT(r, unused, idx, elem) \
  BOOST_PP_COMMA_IF(idx)                          \
  BOOST_PP_STRINGIZE(elem)

#define ENUM_MACRO(name, ...)                                                                                                       \
  enum name { __VA_ARGS__ };                                                                                                        \
  inline const char *name##_strings[] = {BOOST_PP_SEQ_FOR_EACH_I(PROCESS_ONE_ELEMENT, % %, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))}; \
  template <typename T>                                                                                                             \
  constexpr const char *name##_to_string(T value) {                                                                                 \
    int v = static_cast<int>(value);                                                                                                \
    if (v < sizeof(name##_strings) / sizeof(char *)) {                                                                              \
      return name##_strings[v];                                                                                                     \
    }                                                                                                                               \
    return "unknown";                                                                                                               \
  }

/* 这行代码是因为阻止 gcc 报错  warning: backslash-newline at end of file  */
extern int __________________________________________________________________________________;