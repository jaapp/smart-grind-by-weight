cmake_minimum_required(VERSION 3.24)
project(smart_grind_simulator LANGUAGES C CXX)

include(FetchContent)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.3.0
    GIT_SHALLOW TRUE
)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

target_compile_definitions(lvgl PUBLIC
    SMART_GRIND_SIM
)
target_include_directories(lvgl PUBLIC "${PROJECT_ROOT}/include")

add_executable(smart-grind-sim
    main.cpp
    grind_mode_format.cpp
    "${PROJECT_ROOT}/src/ui/ui_helpers.cpp"
    "${PROJECT_ROOT}/src/ui/screens/ready_screen.cpp"
    "${PROJECT_ROOT}/src/ui/screens/grinding_screen_arc.cpp"
    "${PROJECT_ROOT}/src/ui/screens/grinding_screen_chart.cpp"
    "${PROJECT_ROOT}/src/font/lv_font_montserrat_56.c"
    "${PROJECT_ROOT}/src/font/lv_font_montserrat_60.c"
)

target_compile_definitions(smart-grind-sim PRIVATE
    SMART_GRIND_SIM
    NOMINMAX
)
target_include_directories(smart-grind-sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/platform"
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/include"
    "${lvgl_SOURCE_DIR}/src"
)
target_link_libraries(smart-grind-sim PRIVATE lvgl user32 gdi32 shell32)

if(MSVC)
    target_compile_options(smart-grind-sim PRIVATE /W4 /permissive-)
endif()

if(BUILD_TESTING)
    add_test(NAME simulator_smoke COMMAND smart-grind-sim --smoke)
    set_tests_properties(simulator_smoke PROPERTIES TIMEOUT 15)
    add_test(NAME simulator_render_budget COMMAND smart-grind-sim --benchmark)
    set_tests_properties(simulator_render_budget PROPERTIES TIMEOUT 20)
    add_test(NAME simulator_swipe_budget COMMAND smart-grind-sim --swipe-benchmark)
    set_tests_properties(simulator_swipe_budget PROPERTIES TIMEOUT 10)
endif()
