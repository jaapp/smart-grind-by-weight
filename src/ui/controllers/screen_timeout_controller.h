#pragma once

#include <cstdint>
#include <lvgl.h>

#include "../../config/constants.h"

class UIManager;
class DisplayManager;

// Two-stage screensaver driven by touch/weight inactivity:
//   stage 1 (dim timeout): dim the backlight — plain dim, or the boot logo on an
//                          opaque black overlay (mode), at the dimmed brightness
//   stage 2 (off timeout): backlight fully off
// Any activity restores normal brightness and removes the overlay. Never engages
// while grinding.
class ScreenTimeoutController {
public:
    explicit ScreenTimeoutController(UIManager* manager);

    void register_events();
    void update();

    // Re-read the screensaver timeouts/mode from NVS into the local cache. Called once
    // at startup and whenever the user changes these settings, so update() never
    // touches NVS on the hot path.
    void refresh_settings();

private:
    enum class Stage { ACTIVE, DIMMED, OFF };

    void apply_stage(Stage stage, DisplayManager* display);
    void show_logo_overlay();
    void hide_logo_overlay();

    float get_normal_brightness() const;
    float get_screensaver_brightness() const;

    UIManager* ui_manager_;
    Stage stage_ = Stage::ACTIVE;

    // Cached screensaver settings (see refresh_settings). Reading these every UI tick
    // would otherwise open/close the NVS namespace ~40 times per second.
    bool settings_loaded_ = false;
    uint32_t dim_timeout_ms_ = USER_SCREEN_DIM_TIMEOUT_MS;
    uint32_t off_timeout_ms_ = USER_SCREEN_OFF_TIMEOUT_MS;
    int mode_ = USER_SCREEN_SAVER_MODE_DEFAULT;

    // Logo screensaver overlay (created on engage, deleted on wake)
    lv_obj_t* saver_screen_ = nullptr;
};
