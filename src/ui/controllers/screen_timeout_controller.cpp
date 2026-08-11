#include "screen_timeout_controller.h"

#include "../../config/constants.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager), screen_dimmed_(false) {}

void ScreenTimeoutController::register_events() {
    screensaver_ = std::make_unique<ScreensaverOverlay>();
    screensaver_->create();
}

void ScreenTimeoutController::start_screensaver_preview() {
    if (!screensaver_ || !ui_manager_ || !ui_manager_->hardware_manager) {
        return;
    }
    auto* display = ui_manager_->hardware_manager->get_display();
    if (!display) {
        return;
    }

    float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
    ScreensaverStyle style = ScreensaverStyle::WAVE;
    if (ui_manager_->menu_controller_) {
        dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
        style = ui_manager_->menu_controller_->get_screensaver_style();
    }

    display->set_brightness(dimmed);
    screen_dimmed_ = true;
    preview_active_ = true;
    screensaver_->show(style);
}

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

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return;
    }

    if (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        if (screensaver_) {
            screensaver_->hide();
        }
        if (screen_dimmed_) {
            float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
            if (ui_manager_->menu_controller_) {
                normal = ui_manager_->menu_controller_->get_normal_brightness();
            }
            display->set_brightness(normal);
            screen_dimmed_ = false;
        }
        return;
    }

    auto* grinder = hardware->get_grinder();
    bool motor_running = grinder && grinder->is_grinding();

    bool ota_active = false;
    if (ui_manager_->state_machine) {
        UIState state = ui_manager_->state_machine->get_current_state();
        ota_active = (state == UIState::OTA_UPDATE) || (state == UIState::OTA_UPDATE_FAILED);
    }

    if (screensaver_ && screensaver_->is_visible() && (motor_running || ota_active)) {
        screensaver_->hide();
    }

    // A preview holds the overlay regardless of idle state until a touch dismisses it
    if (preview_active_) {
        if (screensaver_ && screensaver_->is_visible()) {
            return;
        }
        preview_active_ = false;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();
    bool recent_weight_activity = sensor &&
                                  sensor->weight_range_exceeds(USER_SCREEN_AUTO_DIM_TIMEOUT_MS,
                                                               USER_WEIGHT_ACTIVITY_THRESHOLD_G);

    bool should_dim = (ms_since_touch >= USER_SCREEN_AUTO_DIM_TIMEOUT_MS) &&
                      !recent_weight_activity && !motor_running;

    if (should_dim && !screen_dimmed_) {
        float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
        if (ui_manager_->menu_controller_) {
            dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
        }
        display->set_brightness(dimmed);
        screen_dimmed_ = true;

        if (screensaver_ && !ota_active && ui_manager_->menu_controller_ &&
            ui_manager_->menu_controller_->get_screensaver_enabled()) {
            screensaver_->show(ui_manager_->menu_controller_->get_screensaver_style());
        }
    } else if (!should_dim && screen_dimmed_) {
        float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
        if (ui_manager_->menu_controller_) {
            normal = ui_manager_->menu_controller_->get_normal_brightness();
        }
        display->set_brightness(normal);
        screen_dimmed_ = false;

        if (screensaver_) {
            screensaver_->hide();
        }
    }
}
