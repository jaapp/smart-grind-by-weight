idf_component_register(SRCS "delta.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update detools freertos log spi_flash)

# Vendored OTA helper: its printf format specifiers (e.g. %x for uint32_t) are
# benign on Xtensa (same width), so don't let the strict IDF toolchain error on
# these untouched upstream format warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)