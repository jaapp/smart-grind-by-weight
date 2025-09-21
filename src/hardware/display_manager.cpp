#include "display_manager.h"
#include "../config/constants.h"
#include "../config/system_config.h"
#include "../config/logging.h"
#include <Arduino.h>


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
    
    // canvas = new Arduino_Canvas(HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX, gfx_device, HW_DISPLAY_OFFSET_X_PX, HW_DISPLAY_OFFSET_Y_PX, HW_DISPLAY_ROTATION_DEG);
    
    if (!gfx_device->begin()) {
        return;
    }
    
    gfx_device->fillScreen(RGB565_BLUE);
    
    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);
    
    screen_width = gfx_device->width();
    screen_height = gfx_device->height();
    // Use smaller buffer for reliable partial rendering
    buffer_size = screen_width * screen_height;  // 64 rows should be enough for most UI elements

    draw_buffer = (lv_color_t*)heap_caps_aligned_alloc(
        8, buffer_size * 2, MALLOC_CAP_8BIT);

    lvgl_display = lv_display_create(screen_width, screen_height);
    lv_display_set_flush_cb(lvgl_display, display_flush_cb);
    lv_display_set_buffers(lvgl_display, draw_buffer, NULL,
                          buffer_size * 2 , LV_DISPLAY_RENDER_MODE_PARTIAL);
    
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

void DisplayManager::flush() {
    if (!initialized) return;
    // gfx_device->flush();
}


void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!g_display_manager || !g_display_manager->gfx_device) return;

    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    // Calculate stride based on LVGL's 4-byte alignment (LV_DRAW_BUF_STRIDE_ALIGN = 4)
    // LVGL aligns stride in bytes, not pixels
    uint32_t stride_bytes = (w * 2 + 3) & ~3;  // Round width*2 up to next 4-byte boundary
    uint32_t stride_pixels = stride_bytes / 2;  // Convert back to pixels

    uint16_t* src = (uint16_t*)px_map;
    Arduino_CO5300* display = static_cast<Arduino_CO5300*>(g_display_manager->gfx_device);

    display->startWrite();

    if (stride_pixels == w || h == 1) {
        // No padding or single row - use fast path
        display->setAddrWindow(area->x1, area->y1, w, h);
        display->writePixels(src, w * h);
    } else {
        // Handle stride padding row by row
        for (uint32_t y = 0; y < h; y++) {
            display->setAddrWindow(area->x1, area->y1 + y, w, 1);
            display->writePixels(src + (y * stride_pixels), w);
        }
    }

    display->endWrite();
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
