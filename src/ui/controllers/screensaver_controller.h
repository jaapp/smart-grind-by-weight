#pragma once

#include <lvgl.h>
#include <cstdint>

/**
 * ScreensaverController - Loads and displays a custom screensaver image
 * from LittleFS as a full-screen LVGL overlay.
 *
 * Used for startup splash and power-save display modes.
 */
class ScreensaverController {
public:
    ScreensaverController();
    ~ScreensaverController();

    /// Check if a screensaver image file exists on LittleFS
    bool has_image() const;

    /// Read NVS preference: show image on startup
    bool is_startup_enabled() const;

    /// Read NVS preference: show image during sleep/dim
    bool is_sleep_enabled() const;

    /// Load image from LittleFS and display as full-screen overlay
    void show();

    /// Hide overlay and free image buffer
    void hide();

    bool is_visible() const { return visible_; }

private:
    lv_obj_t* overlay_screen_;
    lv_obj_t* previous_screen_;
    lv_obj_t* image_widget_;
    uint8_t* image_buffer_;
    lv_image_dsc_t image_dsc_;
    bool visible_;

    bool load_image();
    void free_image();
};
