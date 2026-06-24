cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Expose project-wide headers (include/git_info.h, include/lv_conf.h) to every
# component, and define the LVGL "simple include" shims globally so the managed
# lvgl component picks up our include/lv_conf.h. This replaces the global
# `-Iinclude` / `-DLV_*_INCLUDE_SIMPLE` build_flags from the old platformio.ini.
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)

project(smart-grind-by-weight)
