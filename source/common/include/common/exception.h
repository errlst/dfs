#pragma once

#include <exception>

namespace common
{

  auto exception_handle(std::exception_ptr ex) -> void;

} // namespace common
