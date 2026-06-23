#include "jog_adjust_controller.h"

#include "arduino_compat.h"
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../ui_manager.h"

JogAdjustController::JogAdjustController(UIManager* manager)
    : ui_manager_(manager) {}

void JogAdjustController::register_events() {}

void JogAdjustController::update() {}

void JogAdjustController::start(int direction) {
    if (!ui_manager_) {
        return;
    }

    ui_manager_->jog_start_time = millis();
    ui_manager_->jog_stage = 1;
    ui_manager_->jog_direction = direction;

    if (ui_manager_->jog_timer == nullptr) {
        ui_manager_->jog_timer = lv_timer_create(timer_callback, USER_JOG_STAGE_1_INTERVAL_MS, this);
    }

    lv_timer_set_user_data(ui_manager_->jog_timer, this);
    lv_timer_set_period(ui_manager_->jog_timer, USER_JOG_STAGE_1_INTERVAL_MS);
    lv_timer_resume(ui_manager_->jog_timer);
}

void JogAdjustController::stop() {
    if (!ui_manager_ || !ui_manager_->jog_timer) {
        return;
    }

    lv_timer_pause(ui_manager_->jog_timer);
}

void JogAdjustController::handle_timer(lv_timer_t* timer) {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    unsigned long elapsed = millis() - ui_manager_->jog_start_time;
    int multiplier = SYS_JOG_STAGE_1_MULTIPLIER;

    if (elapsed >= USER_JOG_STAGE_4_THRESHOLD_MS && ui_manager_->jog_stage < 4) {
        ui_manager_->jog_stage = 4;
        lv_timer_set_period(timer, SYS_JOG_STAGE_4_INTERVAL_MS);
        multiplier = SYS_JOG_STAGE_4_MULTIPLIER;
    } else if (elapsed >= USER_JOG_STAGE_3_THRESHOLD_MS && ui_manager_->jog_stage < 3) {
        ui_manager_->jog_stage = 3;
        lv_timer_set_period(timer, SYS_JOG_STAGE_3_INTERVAL_MS);
        multiplier = SYS_JOG_STAGE_3_MULTIPLIER;
    } else if (elapsed >= USER_JOG_STAGE_2_THRESHOLD_MS && ui_manager_->jog_stage < 2) {
        ui_manager_->jog_stage = 2;
        lv_timer_set_period(timer, SYS_JOG_STAGE_2_INTERVAL_MS);
        multiplier = SYS_JOG_STAGE_2_MULTIPLIER;
    } else if (ui_manager_->jog_stage == 2) {
        multiplier = SYS_JOG_STAGE_2_MULTIPLIER;
    } else if (ui_manager_->jog_stage == 3) {
        multiplier = SYS_JOG_STAGE_3_MULTIPLIER;
    } else if (ui_manager_->jog_stage == 4) {
        multiplier = SYS_JOG_STAGE_4_MULTIPLIER;
    }

    for (int i = 0; i < multiplier; ++i) {
        if (ui_manager_->state_machine->is_state(UIState::EDIT)) {
            const auto& traits = get_grind_mode_traits(ui_manager_->current_mode);
            ui_manager_->edit_target = clamp_profile_target(*ui_manager_->profile_controller,
                                                            ui_manager_->current_mode,
                                                            ui_manager_->edit_target + ui_manager_->jog_direction * traits.fine_increment);
        } else if (ui_manager_->state_machine->is_state(UIState::CALIBRATION)) {
            float cal_weight = ui_manager_->calibration_screen.get_calibration_weight();
            cal_weight = ui_manager_->profile_controller->clamp_weight(
                cal_weight + ui_manager_->jog_direction * USER_FINE_WEIGHT_ADJUSTMENT_G);
            ui_manager_->calibration_screen.update_calibration_weight(cal_weight);
        }
    }

    if (ui_manager_->state_machine->is_state(UIState::EDIT) && ui_manager_->edit_controller_) {
        ui_manager_->edit_controller_->update_display();
    }
}

void JogAdjustController::timer_callback(lv_timer_t* timer) {
    auto* controller = static_cast<JogAdjustController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->handle_timer(timer);
    }
}
