add_rules("mode.debug", "mode.release")

set_languages("c++23")


add_requires("asio")
add_packages("asio")

add_requires("nlohmann_json")
add_packages("nlohmann_json")

add_requires("protobuf-cpp 29.2")
add_packages("protobuf-cpp")

add_requires("spdlog")
add_packages("spdlog")


add_cxxflags("-Wall")
add_ldflags("-lstdc++exp")

set_toolchains("gcc-14")

includes("source/common/xmake.lua")
includes("source/proto/xmake.lua")
includes("source/client/xmake.lua")
includes("source/master/xmake.lua")
includes("source/storage/xmake.lua")

includes("test/xmake.lua")
