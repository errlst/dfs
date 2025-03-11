add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_requires("asio")
add_requires("nlohmann_json")
add_requires("protobuf-cpp 29.2")
add_requires("spdlog")
add_requires("boost")

add_cxxflags("-Wall")


set_toolchains("gcc-14")
-- set_toolchains("clang-19")


includes("src/common/xmake.lua")
includes("src/storage/xmake.lua")
includes("src/master/xmake.lua")
includes("src/client/xmake.lua")
includes("src/proto/xmake.lua")
includes("test/xmake.lua")
