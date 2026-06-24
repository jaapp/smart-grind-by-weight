/**
 * @file lv_mem_core_psram.cpp
 *
 * Custom LVGL memory allocator that routes to PSRAM (external SPI RAM) on ESP32-S3.
 *
 * Root cause for this allocator: LVGL's default stdlib malloc() on ESP32-Arduino
 * prefers internal DRAM for allocations. With the full UI (all screens) and the BLE
 * stack both initialized before FreeRTOS tasks are created, internal DRAM was exhausted
 * leaving insufficient contiguous blocks for task stack allocation, causing task
 * creation failures. By routing LVGL objects to the 8MB PSRAM, ~127KB of internal
 * DRAM is freed for task stacks and other DMA-constrained allocations.
 *
 * Falls back to internal DRAM if PSRAM is unavailable or the PSRAM allocation fails.
 */

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

// LVGL custom allocator interface (LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM)
// These C-linkage functions replace lv_mem_core_clib.c when LV_STDLIB_CUSTOM is set.

extern "C" {

void lv_mem_init(void)
{
    // Nothing to init - PSRAM is initialised by the bootloader via board_build.arduino.memory_type = qio_opi
}

void lv_mem_deinit(void)
{
    // Nothing to deinit
}

// Not supported in simple PSRAM allocator mode (no pools)
void * lv_mem_add_pool(void * mem, size_t bytes)
{
    (void)mem;
    (void)bytes;
    return nullptr;
}

void lv_mem_remove_pool(void * pool)
{
    (void)pool;
}

void * lv_malloc_core(size_t size)
{
    // Prefer PSRAM; fall back to internal DRAM if PSRAM is full or unavailable
    void * ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

void * lv_realloc_core(void * p, size_t new_size)
{
    // heap_caps_realloc handles pointers from both DRAM and PSRAM regions
    void * ptr = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

void lv_free_core(void * p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    // Not supported
    (void)mon_p;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

} // extern "C"
