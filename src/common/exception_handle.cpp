#include "exception_handle.h"
#include "log.h"

auto common::exception_handle(std::exception_ptr ex) -> void {
  try {
    if (ex) {
      std::rethrow_exception(ex);
    }
  } catch (const std::exception &ec) {
    LOG_CRITICAL("recv exception {}", ec.what());
    exit(-1);
  } catch (...) {
    LOG_CRITICAL("unknown exception");
    exit(-1);
  }
}