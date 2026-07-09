#include "screen_timeout_controller.h"

#include "../../config/constants.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager), screen_dimmed_(false) {}

void ScreenTimeoutController::register_events() {}

void ScreenTimeoutController::refresh_settings() {
    if (ui_manager_ && ui_manager_->menu_controller_) {
        timeout_ms_ = ui_manager_->menu_controller_->get_screensaver_timeout_ms();
        mode_ = ui_manager_->menu_controller_->get_screensaver_mode();
    } else {
        timeout_ms_ = USER_SCREEN_AUTO_DIM_TIMEOUT_MS;
        mode_ = USER_SCREEN_SAVER_MODE_DEFAULT;
    }
    settings_loaded_ = true;
}

void ScreenTimeoutController::update() {
    if (!ui_manager_) {
        return;
    }

    // Load the cached settings once; thereafter update() stays off NVS. The menu handlers call
    // refresh_settings() when the user changes the timeout or mode.
    if (!settings_loaded_) {
        refresh_settings();
    }

    auto* hardware = ui_manager_->hardware_manager;
    if (!hardware) {
        return;
    }

    auto* display = hardware->get_display();
    if (!display) {
        return;
    }

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return;
    }

    if (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        if (screen_dimmed_) {
            display->set_brightness(get_normal_brightness());
            screen_dimmed_ = false;
        }
        return;
    }

    // Resolve the user-configured screensaver timeout and behaviour (Dim vs Off) from cache.
    uint32_t timeout_ms = timeout_ms_;
    int mode = mode_;

    // "Never" disables the screensaver entirely; make sure the panel is restored.
    if (timeout_ms == USER_SCREEN_SAVER_TIMEOUT_NEVER_MS) {
        if (screen_dimmed_) {
            display->set_brightness(get_normal_brightness());
            screen_dimmed_ = false;
        }
        return;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();
    bool recent_weight_activity = sensor &&
                                  sensor->weight_range_exceeds(timeout_ms,
                                                               USER_WEIGHT_ACTIVITY_THRESHOLD_G);

    bool should_dim = (ms_since_touch >= timeout_ms) && !recent_weight_activity;

    if (should_dim && !screen_dimmed_) {
        // Off mode fully powers down the panel; Dim mode drops to the configured brightness.
        float target = (mode == USER_SCREEN_SAVER_MODE_OFF)
                           ? 0.0f
                           : get_screensaver_brightness();
        display->set_brightness(target);
        screen_dimmed_ = true;
    } else if (!should_dim && screen_dimmed_) {
        display->set_brightness(get_normal_brightness());
        screen_dimmed_ = false;
    }
}

float ScreenTimeoutController::get_normal_brightness() const {
    if (ui_manager_ && ui_manager_->menu_controller_) {
        return ui_manager_->menu_controller_->get_normal_brightness();
    }
    return USER_SCREEN_BRIGHTNESS_NORMAL;
}

float ScreenTimeoutController::get_screensaver_brightness() const {
    if (ui_manager_ && ui_manager_->menu_controller_) {
        return ui_manager_->menu_controller_->get_screensaver_brightness();
    }
    return USER_SCREEN_BRIGHTNESS_DIMMED;
}
