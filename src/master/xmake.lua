add_requires("nlohmann_json")

target("master")
    set_kind("binary")
    add_files("*.cpp")
    add_deps("common")
    add_packages("asio")
    add_packages("nlohmann_json")
