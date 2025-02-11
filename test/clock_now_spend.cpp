#include <chrono>
#include <print>

/* 测试 steady_clock::now() 耗时 */
auto main() -> int {
  auto begin = std::chrono::steady_clock::now();
  auto times = 10000000;
  for (auto i = 0; i < times; ++i) {
    auto now = std::chrono::steady_clock::now();
  }
  auto end = std::chrono::steady_clock::now();
  std::println("{}", std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count() / times);
  return 0;
}