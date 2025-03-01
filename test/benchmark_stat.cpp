#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <sys/stat.h>

auto test() {
  auto start = std::chrono::high_resolution_clock::now();
  struct stat st;
  for (auto i = 0; i < 10000000; ++i) {
    stat("./benchmark_stat", &st);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::println("elapsed time: {}", elapsed.count());
}

auto main() -> int {
  auto ofs = std::ofstream{"./benchmark_stat"};
  test();
  std::filesystem::remove("./benchmark_stat");
  return 0;
}