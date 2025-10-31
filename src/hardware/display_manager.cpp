#include "display_manager.h"
#include "../config/constants.h"
#include "../config/logging.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

DisplayManager* g_display_manager = nullptr;

void DisplayManager::init() {
    g_display_manager = this;
    
    // Initialize display hardware
    bus = new Arduino_ESP32QSPI(
        HW_DISPLAY_CS_PIN, HW_DISPLAY_SCK_PIN, HW_DISPLAY_D0_PIN, 
        HW_DISPLAY_D1_PIN, HW_DISPLAY_D2_PIN, HW_DISPLAY_D3_PIN);
    
    gfx_device = new Arduino_CO5300(
        bus, HW_DISPLAY_RESET_PIN, HW_DISPLAY_ROTATION_DEG, HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX,
        HW_DISPLAY_COLOR_ORDER, HW_DISPLAY_OFFSET_X_PX, HW_DISPLAY_IPS_INVERT_X, HW_DISPLAY_IPS_INVERT_Y);

    
    if (!gfx_device->begin()) {
        return;
    }
    
    gfx_device->fillScreen(RGB565_BLACK);
    
    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);
    
    screen_width = gfx_device->width();
    screen_height = gfx_device->height();

    // Full screen buffer, but only partial updates used
    // RGB565 format (16bit per pixel)
    draw_buffer = nullptr;
    dma_staging_buffer = nullptr;
    dma_staging_rows = 16;

    const size_t draw_rows = 40; // 280 * 40 * 2 = 22,400 bytes
    buffer_size = screen_width * draw_rows * sizeof(uint16_t);

    draw_buffer = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                buffer_size,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!draw_buffer) {
        draw_buffer = static_cast<lv_color_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    buffer_size,
                                    MALLOC_CAP_8BIT));
    }

    if (!draw_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate LVGL draw buffer\n");
        return;
    }

    while (dma_staging_rows >= 4 && dma_staging_buffer == nullptr) {
        dma_staging_buffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    screen_width * dma_staging_rows * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!dma_staging_buffer) {
            dma_staging_rows /= 2;
        }
    }

    if (!dma_staging_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate DMA staging buffer\n");
        heap_caps_free(draw_buffer);
        draw_buffer = nullptr;
        return;
    }

    lvgl_display = lv_display_create(screen_width, screen_height);
    lv_display_set_flush_cb(lvgl_display, display_flush_cb);
    lv_display_set_buffers(lvgl_display, draw_buffer, NULL,
                          buffer_size , LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_add_event_cb(lvgl_display, display_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    
    // Initialize touch
    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);
    
    initialized = true;
}

void DisplayManager::update() {
    if (!initialized) return;
    
    touch_driver.update();
    lv_timer_handler();
}

// Update the refresh area to be full width
// This avoids weird artifacts when partial row updates are used
void DisplayManager::display_rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    
    area->x1 = 0;
    area->x2 = g_display_manager->screen_width - 1;
}

void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!g_display_manager || !g_display_manager->gfx_device) return;
    
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    uint32_t remaining_rows = h;
    uint32_t current_y = area->y1;
    uint32_t src_row_offset = 0;
    uint16_t* staging = g_display_manager->dma_staging_buffer;
    uint32_t staging_rows = g_display_manager->dma_staging_rows ? g_display_manager->dma_staging_rows : h;
    const uint16_t* src_pixels = reinterpret_cast<const uint16_t*>(px_map);

    while (remaining_rows > 0) {
        uint32_t rows = std::min<uint32_t>(remaining_rows, staging_rows);
        size_t copy_pixels = static_cast<size_t>(w) * rows;
        const uint16_t* chunk_src = src_pixels + (static_cast<size_t>(src_row_offset) * w);

        if (staging) {
            memcpy(staging, chunk_src, copy_pixels * sizeof(uint16_t));

            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, staging, w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, staging, w, rows);
            }
        } else {
            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            }
        }

        remaining_rows -= rows;
        src_row_offset += rows;
        current_y += rows;
    }
    
    lv_display_flush_ready(disp);
}

void DisplayManager::touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (!g_display_manager) return;
    
    TouchData touch = g_display_manager->touch_driver.get_touch_data();
    
    if (touch.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch.x;
        data->point.y = touch.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint32_t DisplayManager::millis_cb() {
    return millis();
}

void DisplayManager::set_brightness(float brightness) {
    if (!initialized || !gfx_device) return;
    
    // Clamp brightness to valid hardware range [0.0, 1.0]
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
    // Cast to CO5300 and call setBrightness with 8-bit value
    Arduino_CO5300* display = static_cast<Arduino_CO5300*>(gfx_device);
    uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
    display->setBrightness(brightness_value);
}
