add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

local levilamina_version = "26.10.*"

if is_config("target_type", "server") then
    add_requires("levilamina " .. levilamina_version, {configs = {target_type = "server"}})
else
    add_requires("levilamina " .. levilamina_version, {configs = {target_type = "client"}})
end

add_requires("levibuildscript")

option("build_tests")
    set_default(false)
    set_showmenu(true)
option_end()

if has_config("build_tests") then
    add_requires("gtest")
    add_requires("nlohmann_json")
end

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

target("serverinfo-rest") -- 插件名称
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_defines("NOMINMAX", "UNICODE")
    add_packages("levilamina")
    set_exceptions("none") -- To avoid conflicts with /EHa.
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    else
        add_defines("LL_PLAT_C")
    end

target("serverinfo-rest-tests")
    set_enabled(has_config("build_tests"))
    set_kind("binary")
    set_languages("c++20")
    add_defines("NOMINMAX", "UNICODE")
    add_packages("gtest", "nlohmann_json")
    add_files("test/config_migration_test.cpp", "test/player_data_store_test.cpp", "test/token_auth_test.cpp")
    add_files("src/mod/ConfigMigration.cpp", "src/mod/PlayerDataStore.cpp", "src/mod/TokenAuth.cpp")
    add_includedirs("src")
