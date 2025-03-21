add_includedirs("$(projectdir)/src")

target("master")
    set_kind("binary")
    add_files("*.cpp")
    add_deps("common", "proto")
    add_packages("asio", "nlohmann_json", "protobuf-cpp", "spdlog")