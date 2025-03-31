target("proto")
    set_kind("static")
    
    add_rules("protobuf.cpp")
    add_files("*.proto", { proto_autogendir = "$(scriptdir)" })

    add_files("*.cc")
    add_includedirs("./", {public = true})