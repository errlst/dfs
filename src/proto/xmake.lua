target("compile_proto")
    set_kind("phony")
    on_build(function () 
        os.exec("echo 'protoc compiling'")
        os.exec('protoc ' .. os.scriptdir() .. '/proto.proto -I ' .. os.scriptdir() .. ' --cpp_out=' .. os.scriptdir() )
    end)


target("proto")
    set_kind("static")
    add_files("*.cc")
    add_packages("protobuf-cpp")--cpp_out=