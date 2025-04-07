#include "store_util.h"
#include "config.h"

namespace storage
{

  using namespace storage_detail;

  auto init_store_group() -> void
  {
    hot_store_group_ = std::make_shared<store_ctx_group>("hot_store_group", storage_config.server.hot_paths);
    cold_store_group_ = std::make_shared<store_ctx_group>("cold_store_group", storage_config.server.cold_paths);
  }

  auto hot_store_group() -> std::shared_ptr<store_ctx_group>
  {
    return hot_store_group_;
  }

  auto is_hot_store_group(std::shared_ptr<store_ctx_group> g) -> bool
  {
    return g == hot_store_group_;
  }

  auto cold_store_group() -> std::shared_ptr<store_ctx_group>
  {
    return cold_store_group_;
  }

  auto store_groups() -> std::vector<std::shared_ptr<store_ctx_group>>
  {
    return {hot_store_group_, cold_store_group_};
  }

} // namespace storage