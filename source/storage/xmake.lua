target("storage")
    set_kind("binary")
    add_files("*.cpp")
    add_deps("common", "proto")
