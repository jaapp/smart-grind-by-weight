idf_component_register(SRCS "delta.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update detools freertos log spi_flash)

# Vendored OTA helper: its printf format specifiers (e.g. %x for uint32_t) are
# benign on Xtensa (same width) but the standalone IDF toolchain is stricter
# than PlatformIO's bundled one. Keep parity by not erroring on format warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)