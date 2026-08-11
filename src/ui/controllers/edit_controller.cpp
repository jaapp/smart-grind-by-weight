#include "edit_controller.h"

#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../ui_manager.h"
#include "jog_adjust_controller.h"

EditUIController::EditUIController(UIManager* manager)
    : ui_manager_(manager) {}

void EditUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    auto save_btn = ui_manager_->edit_screen.get_save_btn();
    if (save_btn) {
        lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* controller = static_cast<EditUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_save();
            }
        }, LV_EVENT_CLICKED, this);
    }

    auto cancel_btn = ui_manager_->edit_screen.get_cancel_btn();
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* controller = static_cast<EditUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_cancel();
            }
        }, LV_EVENT_CLICKED, this);
    }

    auto plus_btn = ui_manager_->edit_screen.get_plus_btn();
    if (plus_btn) {
        lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
            auto* controller = static_cast<EditUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_plus(lv_event_get_code(e));
            }
        }, LV_EVENT_ALL, this);
    }

    auto minus_btn = ui_manager_->edit_screen.get_minus_btn();
    if (minus_btn) {
        lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
            auto* controller = static_cast<EditUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_minus(lv_event_get_code(e));
            }
        }, LV_EVENT_ALL, this);
    }
}

void EditUIController::update() {}

void EditUIController::handle_save() {
    if (!ui_manager_ || !ui_manager_->profile_controller) {
        return;
    }

    update_current_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode, ui_manager_->edit_target);
    ui_manager_->profile_controller->save_targets();
    if (ui_manager_->ready_controller_) {
        ui_manager_->ready_controller_->refresh_targets();
    }
    ui_manager_->switch_to_state(UIState::READY);
}

void EditUIController::handle_cancel() {
    if (!ui_manager_) {
        return;
    }

    ui_manager_->edit_target = ui_manager_->original_target;
    ui_manager_->edit_screen.set_mode(ui_manager_->current_mode);
    update_display();
    ui_manager_->switch_to_state(UIState::READY);
}

void EditUIController::handle_plus(lv_event_code_t code) {
    if (!ui_manager_ || !ui_manager_->profile_controller) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        const auto& traits = get_grind_mode_traits(ui_manager_->current_mode);
        ui_manager_->edit_target = clamp_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode,
                                                        ui_manager_->edit_target + traits.fine_increment);
        update_display();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->start(1);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->stop();
        }
    }
}

void EditUIController::handle_minus(lv_event_code_t code) {
    if (!ui_manager_ || !ui_manager_->profile_controller) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        const auto& traits = get_grind_mode_traits(ui_manager_->current_mode);
        ui_manager_->edit_target = clamp_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode,
                                                        ui_manager_->edit_target - traits.fine_increment);
        update_display();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->start(-1);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->stop();
        }
    }
}

void EditUIController::update_display() {
    if (!ui_manager_) {
        return;
    }

    ui_manager_->edit_screen.set_mode(ui_manager_->current_mode);
    ui_manager_->edit_screen.update_target(ui_manager_->edit_target);
}
