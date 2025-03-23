add_packages("protobuf-cpp")

add_rules("protobuf.cpp")
add_files("*.proto", { proto_autogendir = "$(scriptdir)" })

target("proto")
    set_kind("static")
    add_files("*.cc")