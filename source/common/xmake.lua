target("common")
    set_kind("static")
    add_files("source/*.cpp")
    add_includedirs("include", {public = true})
    