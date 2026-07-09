#pragma once

#include <cstdint>

#include "../../config/constants.h"

class UIManager;

// Implements automatic screen dimming based on touch/weight activity

class ScreenTimeoutController {
public:
    explicit ScreenTimeoutController(UIManager* manager);

    void register_events();
    void update();

    // Re-read the screensaver timeout/mode from NVS into the local cache. Called once at startup
    // and whenever the user changes these settings, so update() never touches NVS on the hot path.
    void refresh_settings();

private:
    UIManager* ui_manager_;
    bool screen_dimmed_;

    // Cached screensaver settings (see refresh_settings). Reading these every UI tick would
    // otherwise open/close the NVS namespace ~40 times per second in the render task.
    bool settings_loaded_ = false;
    uint32_t timeout_ms_ = USER_SCREEN_AUTO_DIM_TIMEOUT_MS;
    int mode_ = USER_SCREEN_SAVER_MODE_DEFAULT;

    float get_normal_brightness() const;
    float get_screensaver_brightness() const;
};
