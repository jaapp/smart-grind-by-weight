idf_component_register(SRCS "detools.c" "heatshrink/heatshrink_decoder.c"
                       INCLUDE_DIRS "include" "heatshrink")

# Vendored third-party delta/heatshrink sources: silence format warnings-as-
# errors under the stricter standalone IDF toolchain (parity with PlatformIO).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)
