add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_requires("asio")
add_requires("nlohmann_json")
add_requires("protobuf-cpp")


if is_mode("debug") then
    -- add_cflags("-pg")
    -- add_cxxflags("-pg")
    -- add_ldflags("-pg")
end

set_toolchains("gcc-14")


includes("src/common/xmake.lua")
includes("src/storage/xmake.lua")
includes("src/master/xmake.lua")
includes("src/client/xmake.lua")
includes("src/proto/xmake.lua")
includes("test/xmake.lua")

