idf_component_register(SRCS "detools.c" "heatshrink/heatshrink_decoder.c"
                       INCLUDE_DIRS "include" "heatshrink")

# Vendored third-party delta/heatshrink sources: silence format warnings-as-
# errors so the stricter IDF toolchain doesn't fail on untouched upstream code.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)
