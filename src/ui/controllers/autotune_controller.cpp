#include "autotune_controller.h"
#include "../ui_manager.h"
#include "../../config/constants.h"
#include <Arduino.h>

AutoTuneUIController::AutoTuneUIController(UIManager* manager)
    : ui_manager_(manager)
    , autotune_started_(false)
{}

void AutoTuneUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    auto cancel_btn = ui_manager_->autotune_screen.get_cancel_button();
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            static_cast<AutoTuneUIController*>(lv_event_get_user_data(e))->handle_cancel();
        }, LV_EVENT_CLICKED, this);
    }

    auto ok_btn = ui_manager_->autotune_screen.get_ok_button();
    if (ok_btn) {
        lv_obj_add_event_cb(ok_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            static_cast<AutoTuneUIController*>(lv_event_get_user_data(e))->handle_ok();
        }, LV_EVENT_CLICKED, this);
    }
}

void AutoTuneUIController::update() {
    if (!ui_manager_) {
        return;
    }

    // Get autotune controller from hardware manager
    auto hw_manager = ui_manager_->get_hardware_manager();
    if (!hw_manager) {
        return;
    }

    auto autotune_controller = hw_manager->get_autotune_controller();
    if (!autotune_controller) {
        return;
    }

    // Advance backend state machine first
    autotune_controller->update();

    if (!autotune_started_) {
        return;
    }

    const AutoTuneProgress& progress = autotune_controller->get_progress();

    if (progress.phase == AutoTunePhase::COMPLETE_SUCCESS) {
        const AutoTuneResult& result = autotune_controller->get_result();
        ui_manager_->autotune_screen.show_success_screen(result.latency_ms, progress.previous_latency_ms);
        ui_manager_->menu_screen.update_diagnostics(hw_manager->get_weight_sensor());
        autotune_started_ = false;
        return;
    }

    if (progress.phase == AutoTunePhase::COMPLETE_FAILURE) {
        const AutoTuneResult& result = autotune_controller->get_result();
        ui_manager_->autotune_screen.show_failure_screen(result.error_message);
        autotune_started_ = false;
        return;
    }

    if (autotune_controller->is_active()) {
        ui_manager_->autotune_screen.update_progress(progress);
        // Clear message flag after UI has read it
        if (progress.has_new_message) {
            autotune_controller->clear_message_flag();
        }
    }
}

void AutoTuneUIController::start_autotune() {
    // Deprecated - use confirm_and_begin() instead
    // This is kept for backwards compatibility
    confirm_and_begin();
}

void AutoTuneUIController::confirm_and_begin() {
    // This is called AFTER user has already confirmed in menu screen
    // So we just start the autotune process directly
    if (!ui_manager_) {
        LOG_BLE("ERROR: Cannot start autotune - no UI manager\n");
        return;
    }

    auto hw_manager = ui_manager_->get_hardware_manager();
    if (!hw_manager) {
        LOG_BLE("ERROR: Cannot start autotune - no hardware manager\n");
        return;
    }

    auto autotune_controller = hw_manager->get_autotune_controller();
    if (!autotune_controller) {
        LOG_BLE("ERROR: Cannot start autotune - no autotune controller\n");
        return;
    }

    LOG_BLE("AutoTune UI: Starting autotune process\n");

    // Switch to autotune screen and show console
    ui_manager_->switch_to_state(UIState::AUTOTUNING);
    ui_manager_->autotune_screen.show_console_screen();

    // Start the autotune process
    if (autotune_controller->start()) {
        autotune_started_ = true;
        LOG_BLE("AutoTune UI: Started successfully\n");
    } else {
        LOG_BLE("ERROR: AutoTune failed to start\n");
        ui_manager_->switch_to_state(UIState::MENU);
    }
}

void AutoTuneUIController::handle_cancel() {
    LOG_BLE("AutoTune UI: Cancel button pressed\n");

    if (!ui_manager_) {
        return;
    }

    auto hw_manager = ui_manager_->get_hardware_manager();
    if (!hw_manager) {
        return;
    }

    auto autotune_controller = hw_manager->get_autotune_controller();
    if (autotune_controller && autotune_controller->is_active()) {
        autotune_controller->cancel();
    }

    autotune_started_ = false;

    // Return to menu screen
    ui_manager_->switch_to_state(UIState::MENU);
}

void AutoTuneUIController::handle_ok() {
    // Completion acknowledged - return to menu
    LOG_BLE("AutoTune UI: OK button pressed (completion acknowledged)\n");
    autotune_started_ = false;

    if (!ui_manager_) {
        return;
    }

    ui_manager_->switch_to_state(UIState::MENU);
}
