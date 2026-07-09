# Application "main" component.
#
# The source tree lives in ../src. We register it here as the IDF "main"
# component so it is always force-linked and automatically depends on every
# other component (IDF special-cases the component named "main"). Managed
# dependencies are declared in the adjacent idf_component.yml.
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

# The app sources are built with -Wall but keep a few pre-existing, benign
# warnings non-fatal (native IDF adds a bare -Werror). GCC only lets you carve
# out exceptions per-category (a blanket -Wno-error does not undo it), so list
# them explicitly here rather than churning working code. IDF framework
# components keep -Werror.
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

# ---------------------------------------------------------------------------
# Boot splash asset: auto-regenerate from the source PNG.
#
# assets/boot_logo.png is the source of truth. Whenever it (or the converter)
# changes, regenerate src/ui/assets/boot_logo.c before compiling so the flashed
# splash always matches the PNG - drop in a new logo and rebuild, no manual step.
#
# Only wired up when tools/venv exists (created by tools/grinder.py, and where
# Pillow/pypng live). A fresh checkout without the venv simply compiles the
# committed boot_logo.c as-is, so the build never depends on Python tooling.
# ---------------------------------------------------------------------------
set(BOOT_LOGO_PNG    ${CMAKE_SOURCE_DIR}/assets/boot_logo.png)
set(BOOT_LOGO_C      ${CMAKE_SOURCE_DIR}/src/ui/assets/boot_logo.c)
set(BOOT_LOGO_SCRIPT ${CMAKE_SOURCE_DIR}/tools/convert_logo.py)
set(BOOT_LOGO_VENV_PY ${CMAKE_SOURCE_DIR}/tools/venv/bin/python)

if(EXISTS ${BOOT_LOGO_VENV_PY} AND EXISTS ${BOOT_LOGO_PNG})
    add_custom_command(
        OUTPUT ${BOOT_LOGO_C}
        COMMAND ${BOOT_LOGO_VENV_PY} ${BOOT_LOGO_SCRIPT}
                --input ${BOOT_LOGO_PNG}
                --output-dir ${CMAKE_SOURCE_DIR}/src/ui/assets
                --name boot_logo
        DEPENDS ${BOOT_LOGO_PNG} ${BOOT_LOGO_SCRIPT}
        COMMENT "Regenerating boot splash asset from boot_logo.png"
        VERBATIM
    )
    add_custom_target(gen_boot_logo DEPENDS ${BOOT_LOGO_C})
    add_dependencies(${COMPONENT_LIB} gen_boot_logo)
endif()
