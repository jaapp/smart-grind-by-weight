#pragma once
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "touch_driver.h"
#include "../config/pins.h"

class DisplayManager {
private:
    Arduino_DataBus* bus;
    Arduino_GFX* gfx_device;
    lv_display_t* lvgl_display;
    lv_indev_t* lvgl_input;
    lv_color_t* draw_buffer;
    TouchDriver touch_driver;
    
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_size;
    bool initialized;

public:
    void init();
    void update();
    void flush();
    void set_brightness(float brightness);
    
    uint32_t get_width() const { return screen_width; }
    uint32_t get_height() const { return screen_height; }
    bool is_initialized() const { return initialized; }
    TouchDriver* get_touch_driver() { return &touch_driver; }
    
private:
    static void display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void display_rounder_cb(lv_event_t* e);
    static void touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static uint32_t millis_cb();
};

extern DisplayManager* g_display_manager;
