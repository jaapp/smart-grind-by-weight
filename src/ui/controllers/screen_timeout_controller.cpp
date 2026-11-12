#include "screen_timeout_controller.h"
#include <Arduino.h>

#include "../../config/constants.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager), screen_dimmed_(false) {}

void ScreenTimeoutController::register_events() {}

bool ScreenTimeoutController::update() {
    if (!ui_manager_) {
        return false;
    }

    auto* hardware = ui_manager_->hardware_manager;
    if (!hardware) {
        return false;
    }

    auto* display = hardware->get_display();
    if (!display) {
        return false;
    }

    const bool auto_select = ui_manager_->profile_controller &&
        ui_manager_->profile_controller->get_auto_select_profile() >= 0;
    display->set_dimmed(screen_dimmed_, auto_select);

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return false;
    }

    if (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        if (screen_dimmed_) {
            float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
            if (ui_manager_->menu_controller_) {
                normal = ui_manager_->menu_controller_->get_normal_brightness();
            }
            display->set_brightness(normal);
            screen_dimmed_ = false;
            display->set_dimmed(false, auto_select);
        }
        return false;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();
    bool recent_weight_activity = sensor &&
                                  sensor->weight_range_exceeds(USER_SCREEN_AUTO_DIM_TIMEOUT_MS,
                                                               USER_WEIGHT_ACTIVITY_THRESHOLD_G);

    bool should_dim = (ms_since_touch >= USER_SCREEN_AUTO_DIM_TIMEOUT_MS) && !recent_weight_activity;

    if (should_dim && !screen_dimmed_) {
        float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
        if (ui_manager_->menu_controller_) {
            dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
        }
        display->set_brightness(dimmed);
        screen_dimmed_ = true;
        display->set_dimmed(true, auto_select);
    } else if (!should_dim && screen_dimmed_) {
        float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
        if (ui_manager_->menu_controller_) {
            normal = ui_manager_->menu_controller_->get_normal_brightness();
        }
        display->set_brightness(normal);
        screen_dimmed_ = false;
        display->set_dimmed(false, auto_select);
        return true;
    }
    return false;
}
