#pragma once
#include "../config/hardware.h"
#if HW_DISPLAY_VARIANT_V2
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#else
#include <Arduino_GFX_Library.h>
#endif
#include <lvgl.h>
#include "touch_driver.h"
#include "../config/constants.h"

class DisplayManager {
private:
#if HW_DISPLAY_VARIANT_V2
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_panel_handle_t panel_handle;
    lv_display_t* pending_flush_display;
#else
    Arduino_DataBus* bus;
    Arduino_GFX* gfx_device;
#endif
    lv_display_t* lvgl_display;
    lv_indev_t* lvgl_input;
    lv_color_t* draw_buffer;
#if !HW_DISPLAY_VARIANT_V2
    uint16_t* dma_staging_buffer;
#endif
    TouchDriver touch_driver;
#if !HW_DISPLAY_VARIANT_V2
    uint16_t dma_staging_rows;
#endif
    
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_size;
    bool initialized;

public:
    void init();
    void update();
    void set_brightness(float brightness);
    
    uint32_t get_width() const { return screen_width; }
    uint32_t get_height() const { return screen_height; }
    bool is_initialized() const { return initialized; }
    TouchDriver* get_touch_driver() { return &touch_driver; }
    
private:
    static void display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
#if HW_DISPLAY_VARIANT_V2
    static bool color_transfer_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_io_event_data_t* event_data,
                                       void* user_context);
#endif
    static void display_rounder_cb(lv_event_t* e);
    static void touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static uint32_t millis_cb();
};

extern DisplayManager* g_display_manager;
