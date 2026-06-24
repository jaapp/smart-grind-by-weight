set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../test")

idf_component_register(
    SRCS
        "test_main.c"
        "${TEST_DIR}/test_littlefs.c"
        "${TEST_DIR}/test_littlefs_common.c"
        "${TEST_DIR}/test_dir.c"
        "${TEST_DIR}/test_benchmark.c"
        "${TEST_DIR}/test_littlefs_static_partition.c"
    INCLUDE_DIRS "${TEST_DIR}"
    PRIV_REQUIRES
        esp_littlefs
        unity
        test_utils
        vfs
        spi_flash
        spiffs
        fatfs
        wear_levelling
        esp_timer
        esp_partition
    WHOLE_ARCHIVE
)

# Embed the test filesystem binary
target_add_binary_data(${COMPONENT_LIB} "${TEST_DIR}/testfs.bin" BINARY)
