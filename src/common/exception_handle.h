#pragma once
#include <stdexcept> // IWYU pragma: keep

namespace common {
auto exception_handle(std::exception_ptr ex) -> void;
}
