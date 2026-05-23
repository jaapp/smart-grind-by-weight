#include "screen_timeout_controller.h"
#include "screensaver_controller.h"

#include <Arduino.h>

#include "../../config/constants.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager)
    , timing_settings_(ScreensaverSettings::load_timing())
    , screen_dimmed_(false) {}

void ScreenTimeoutController::register_events() {}

void ScreenTimeoutController::update() {
    if (!ui_manager_) {
        return;
    }

    auto* hardware = ui_manager_->hardware_manager;
    if (!hardware) {
        return;
    }

    auto* display = hardware->get_display();
    if (!display) {
        return;
    }

    if (is_protected_state()) {
        restore_normal_display(display);
        return;
    }

    uint32_t now = millis();
    refresh_settings_if_needed(now);

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();
    uint32_t idle_timeout_ms = ScreensaverSettings::idle_timeout_ms(timing_settings_);
    uint32_t weight_activity_window_ms =
        ScreensaverSettings::weight_activity_window_ms(timing_settings_);
    bool recent_weight_activity = sensor &&
                                  sensor->weight_range_exceeds(weight_activity_window_ms,
                                                               USER_WEIGHT_ACTIVITY_THRESHOLD_G);

    bool should_dim = (ms_since_touch >= idle_timeout_ms) && !recent_weight_activity;

    if (should_dim && !screen_dimmed_) {
        float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
        if (ui_manager_->menu_controller_) {
            dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
        }
        display->set_brightness(dimmed);
        screen_dimmed_ = true;

        // Show screensaver image if enabled
        if (screensaver_controller_ &&
            screensaver_controller_->is_sleep_enabled() &&
            screensaver_controller_->has_image()) {
            screensaver_controller_->show();
        }
    } else if (!should_dim && screen_dimmed_) {
        restore_normal_display(display);
    }
}

void ScreenTimeoutController::refresh_settings_if_needed(uint32_t now_ms) {
    constexpr uint32_t kRefreshIntervalMs = 5000;

    if (last_settings_refresh_ms_ != 0 &&
        now_ms - last_settings_refresh_ms_ < kRefreshIntervalMs) {
        return;
    }

    timing_settings_ = ScreensaverSettings::load_timing();
    last_settings_refresh_ms_ = now_ms;
}

void ScreenTimeoutController::restore_normal_display(DisplayManager* display) {
    bool screensaver_visible = screensaver_controller_ && screensaver_controller_->is_visible();
    if (screensaver_visible) {
        screensaver_controller_->hide();
    }

    if (screen_dimmed_ || screensaver_visible) {
        float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
        if (ui_manager_ && ui_manager_->menu_controller_) {
            normal = ui_manager_->menu_controller_->get_normal_brightness();
        }
        display->set_brightness(normal);
    }

    screen_dimmed_ = false;
}

bool ScreenTimeoutController::is_protected_state() const {
    if (!ui_manager_) {
        return false;
    }

    bool ota_active = ui_manager_->bluetooth_manager &&
                      ui_manager_->bluetooth_manager->is_updating();
    if (ota_active) {
        return true;
    }

    if (!ui_manager_->state_machine) {
        return false;
    }

    UIState state = ui_manager_->state_machine->get_current_state();
    return state == UIState::GRINDING ||
           state == UIState::OTA_UPDATE ||
           state == UIState::OTA_UPDATE_FAILED;
}
