target("compile_proto")
    set_kind("phony")
    on_build(function () 
        os.exec("sh " .. os.scriptdir() .. "/compile_proto.sh")
    end)


target("proto")
    set_kind("static")
    add_files("*.cc")
    add_packages("protobuf-cpp")