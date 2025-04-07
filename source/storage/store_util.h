#pragma once

#include "store.h"

namespace storage_detail
{
  using namespace storage;

  inline auto hot_store_group_ = std::shared_ptr<store_ctx_group>{};

  inline auto cold_store_group_ = std::shared_ptr<store_ctx_group>{};

} // namespace storage_detail

namespace storage
{
  /**
   * @brief 初始化 store group
   *
   */
  auto init_store_group() -> void;

  /**
   * @brief 获取 hot store group
   *
   */
  auto hot_store_group() -> std::shared_ptr<store_ctx_group>;

  /**
   * @brief 判断是 hot group 还是 cold group
   *
   */
  auto is_hot_store_group(std::shared_ptr<store_ctx_group>) -> bool;

  /**
   * @brief 获取 cold store group
   *
   */
  auto cold_store_group() -> std::shared_ptr<store_ctx_group>;

  /**
   * @brief hot + cold
   *
   */
  auto store_groups() -> std::vector<std::shared_ptr<store_ctx_group>>;

} // namespace storage