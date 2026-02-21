set_project("tei_mt_lcui_gui")
set_version("0.1.0")
set_languages("c++20")
add_rules("mode.debug", "mode.release")

add_requires("lcui")

if is_plat("linux") then
    add_syslinks("pthread")
end

target("tei_mt_lcui_gui")
    set_kind("binary")
    add_packages("lcui")
    add_files("src/*.cpp")
    add_includedirs("src")
    add_installfiles("app/*")
    set_rundir("app")
