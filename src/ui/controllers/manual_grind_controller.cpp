#include "manual_grind_controller.h"

#include <Arduino.h>
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../system/statistics_manager.h"
#include "../ui_manager.h"

namespace {
constexpr uint32_t kReadoutUpdateIntervalMs = 100;
}

ManualGrindUIController::ManualGrindUIController(UIManager* manager)
    : ui_manager_(manager) {}

void ManualGrindUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    lv_obj_t* time_label = ui_manager_->ready_screen.get_manual_time_label();
    if (time_label) {
        lv_obj_add_event_cb(time_label, [](lv_event_t* e) {
            auto* controller = static_cast<ManualGrindUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_time_tap();
            }
        }, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* weight_label = ui_manager_->ready_screen.get_manual_weight_label();
    if (weight_label) {
        lv_obj_add_event_cb(weight_label, [](lv_event_t* e) {
            auto* controller = static_cast<ManualGrindUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_weight_tap();
            }
        }, LV_EVENT_CLICKED, this);
    }
}

void ManualGrindUIController::update() {
    if (!ui_manager_) {
        return;
    }

    if (running_ &&
        millis() - run_start_ms_ + accumulated_ms_ >= USER_MANUAL_GRIND_MAX_RUNTIME_MS) {
        stop_run();
        if (ui_manager_->grinding_controller_) {
            ui_manager_->grinding_controller_->update_grind_button_icon();
        }
    }

    refresh_readouts(false);
}

void ManualGrindUIController::handle_grind_button() {
    if (running_) {
        stop_run();
    } else {
        start_run();
    }
}

void ManualGrindUIController::on_enter() {
    accumulated_ms_ = 0;
    run_start_ms_ = millis();

    auto* sensor = ui_manager_ ? ui_manager_->hardware_manager->get_weight_sensor() : nullptr;
    if (sensor) {
        sensor->tareNoDelay();
    }

    ui_manager_->ready_screen.reset_manual_readouts();
}

void ManualGrindUIController::on_state_changed(UIState new_state) {
    if (new_state != UIState::READY) {
        stop_and_reset();
    }
}

void ManualGrindUIController::stop_and_reset() {
    if (running_) {
        stop_run();
    }
    accumulated_ms_ = 0;
    if (ui_manager_) {
        ui_manager_->ready_screen.reset_manual_readouts();
    }
}

void ManualGrindUIController::start_run() {
    if (running_ || !ui_manager_) {
        return;
    }

    if (ui_manager_->grind_controller && ui_manager_->grind_controller->is_active()) {
        return;
    }

    auto* grinder = ui_manager_->hardware_manager->get_grinder();
    if (!grinder) {
        return;
    }

    grinder->start();
    ui_manager_->set_background_active(true);
    running_ = true;
    run_start_ms_ = millis();
    refresh_readouts(true);
}

void ManualGrindUIController::stop_run() {
    if (!running_ || !ui_manager_) {
        return;
    }

    auto* grinder = ui_manager_->hardware_manager->get_grinder();
    if (grinder) {
        grinder->stop();
    }
    ui_manager_->set_background_active(false);

    uint32_t run_duration_ms = millis() - run_start_ms_;
    accumulated_ms_ += run_duration_ms;
    running_ = false;

    statistics_manager.update_motor_test(run_duration_ms);
    refresh_readouts(true);
}

void ManualGrindUIController::handle_time_tap() {
    accumulated_ms_ = 0;
    run_start_ms_ = millis();
    refresh_readouts(true);
}

void ManualGrindUIController::handle_weight_tap() {
    auto* sensor = ui_manager_ ? ui_manager_->hardware_manager->get_weight_sensor() : nullptr;
    if (sensor) {
        sensor->tareNoDelay();
    }
    refresh_readouts(true);
}

void ManualGrindUIController::refresh_readouts(bool force) {
    if (!ui_manager_) {
        return;
    }

    uint32_t now = millis();
    if (!force && now - last_readout_update_ms_ < kReadoutUpdateIntervalMs) {
        return;
    }
    last_readout_update_ms_ = now;

    uint32_t elapsed_ms = accumulated_ms_ + (running_ ? now - run_start_ms_ : 0);
    ui_manager_->ready_screen.update_manual_time(elapsed_ms / 1000.0f);

    auto* sensor = ui_manager_->hardware_manager->get_weight_sensor();
    if (!sensor) {
        return;
    }
    if (sensor->is_tare_in_progress()) {
        ui_manager_->ready_screen.update_manual_weight_text("--");
    } else {
        ui_manager_->ready_screen.update_manual_weight(sensor->get_display_weight());
    }
}
