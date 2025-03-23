add_includedirs("$(projectdir)/src")

target("master")
    set_kind("binary")
    add_files("*.cpp")
    add_files("*.cppm")
    add_deps("common", "proto")
