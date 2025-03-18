add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_requires("asio", "nlohmann_json", "protobuf-cpp 29.2", "spdlog")

add_cxxflags("-Wall")
add_ldflags("-lstdc++exp")

set_toolchains("gcc-14")

includes("src/common/xmake.lua")
includes("src/storage/xmake.lua")
includes("src/master/xmake.lua")
includes("src/client/xmake.lua")
includes("src/proto/xmake.lua")
includes("test/xmake.lua")
