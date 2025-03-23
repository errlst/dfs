add_includedirs("$(projectdir)/src")

target("storage")
    set_kind("binary")
    add_deps("common", "proto")
    add_packages("asio", "nlohmann_json", "protobuf-cpp", "spdlog")
    add_files("*.cpp")
