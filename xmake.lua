add_rules("mode.debug", "mode.release")

set_languages("c++23")

target("storage")
    set_kind("binary")
    add_files("src/storage/*.cpp")

