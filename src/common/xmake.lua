target("common")
    set_kind("static")
    add_files("*.cpp")
    add_files("*.cppm", {public = true})
    add_packages("asio", "nlohmann_json", "spdlog")
    