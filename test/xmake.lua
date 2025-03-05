target("all")
    set_kind("phony")
    on_run(function ()
        import("core.project.task")
        os.cd(os.scriptdir())
        for _, file in ipairs(os.files("*.cpp")) do
            local target_name = path.basename(file)
            task.run("build", {target = target_name})
        end
    end)
    add_cflags("-pg")
    add_cxxflags("-pg")
    

for _, file in ipairs(os.files("*.cpp")) do
    local target_name = path.basename(file)
    target(target_name)
        set_kind("binary")
        set_targetdir(os.scriptdir())  -- 设置构建路径为当前路径
        add_files(file)
        add_deps("common", "proto")
        add_packages("asio", "nlohmann_json", "protobuf-cpp", "spdlog", "boost")
end