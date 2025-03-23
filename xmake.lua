add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_requires("asio", "nlohmann_json", "protobuf-cpp 29.2", "spdlog")
add_packages("asio", "nlohmann_json", "protobuf-cpp", "spdlog")

add_cxxflags("-Wall")
add_ldflags("-lstdc++exp")

set_toolchains("clang-19")

includes("source/common/xmake.lua")
includes("source/proto/xmake.lua")
includes("source/client/xmake.lua")
includes("source/master/xmake.lua")


-- includes("source/storage/xmake.lua")
-- includes("test/xmake.lua")
