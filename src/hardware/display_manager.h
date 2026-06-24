#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "touch_driver.h"
#include "../config/constants.h"

class DisplayManager {
public:
    void init();
    void update();
    void set_brightness(float brightness);

    uint32_t get_width() const  { return screen_width; }
    uint32_t get_height() const { return screen_height; }
    bool is_initialized() const { return initialized; }
    TouchDriver* get_touch_driver() { return &touch_driver; }

    // Lock/unlock LVGL for thread-safe access from multiple tasks.
    // lock() blocks up to timeout_ms; returns false on timeout.
    bool lock(uint32_t timeout_ms = portMAX_DELAY);
    void unlock();

    // Accessed by DMA ISR callback (on_color_trans_done)
    lv_display_t*             lvgl_display  = nullptr;

private:
    esp_lcd_panel_handle_t    panel_handle  = nullptr;
    esp_lcd_panel_io_handle_t io_handle     = nullptr;
    lv_indev_t*               lvgl_input    = nullptr;
    lv_color_t*               draw_buf1     = nullptr;
    lv_color_t*               draw_buf2     = nullptr;

    SemaphoreHandle_t         lvgl_mutex    = nullptr;

    TouchDriver touch_driver;

    uint32_t screen_width  = HW_DISPLAY_WIDTH_PX;
    uint32_t screen_height = HW_DISPLAY_HEIGHT_PX;
    bool     initialized   = false;

    // LVGL callbacks
    static void     lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area,
                                  uint8_t* px_map);
    static void     lvgl_rounder_cb(lv_event_t* e);
    static void     touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static uint32_t lvgl_tick_cb();
};

extern DisplayManager* g_display_manager;
