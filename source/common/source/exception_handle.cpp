#include <common/exception.h>
#include <common/log.h>

namespace common
{

  auto exception_handle(std::exception_ptr ex) -> void
  {
    try
    {
      if (ex)
      {
        std::rethrow_exception(ex);
      }
    }
    catch (const std::exception &ec)
    {
      LOG_CRITICAL("recv exception {}", ec.what());
      exit(-1);
    }
    catch (...)
    {
      LOG_CRITICAL("unknown exception");
      exit(-1);
    }
  }

} // namespace common