add_rules("mode.debug", "mode.release")

set_languages("c++23")
add_cxxflags("-Wall")
set_toolchains("gcc-14")

add_requires("asio")
add_requires("nlohmann_json")

includes("src/common/xmake.lua")
includes("src/storage/xmake.lua")
includes("src/master/xmake.lua")
includes("test/xmake.lua")
