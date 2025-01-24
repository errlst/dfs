target("common")
    set_kind("static")
    add_files("*.cpp")
    add_packages("asio", "nlohmann_json")

    