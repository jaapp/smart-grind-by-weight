# Application "main" component.
#
# The source tree physically lives in ../src (preserved from the PlatformIO
# layout). We register it here as the IDF "main" component so it is always
# force-linked and automatically depends on every other component (IDF special-
# cases the component named "main"). Managed dependencies are declared in the
# adjacent idf_component.yml.
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/src"
    WHOLE_ARCHIVE
)
# WHOLE_ARCHIVE: the app provides LVGL's custom allocator (lv_malloc_core etc.
# in src/hardware/lv_mem_core_psram.cpp). Since main is linked before the lvgl
# component it depends on, those objects would otherwise be dropped before lvgl
# references them. Forcing the whole archive keeps every app object linked.
# NOTE: deliberately no REQUIRES — the "main" component auto-depends on every
# other component (IDF built-ins, managed deps, and local delta/detools).
# Listing REQUIRES here would override that and drop built-ins like esp_timer.

# Preserve the old platformio.ini build_flags for the application sources only.
# PlatformIO compiled the app with -Wall but NOT -Werror; native IDF adds bare
# -Werror. GCC only lets you carve out exceptions per-category (a blanket
# -Wno-error does not undo it), so keep these pre-existing, benign warnings
# non-fatal for the app sources — matching the prior build without churning
# working code. IDF framework components keep -Werror.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Ofast
    -Wall
    -Wno-error=format-truncation
    -Wno-error=format-overflow
    -Wno-error=stringop-truncation
    -Wno-error=stringop-overflow
    -Wno-error=maybe-uninitialized
    -Wno-error=switch
    -Wno-error=format
)
