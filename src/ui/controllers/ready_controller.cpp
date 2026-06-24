#include "ready_controller.h"

#include "preferences_idf.h"
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
#include "../ui_manager.h"

ReadyUIController::ReadyUIController(UIManager* manager)
    : ui_manager_(manager) {}

void ReadyUIController::update() {}

void ReadyUIController::refresh_profiles() {
    if (!ui_manager_ || !ui_manager_->profile_controller) {
        return;
    }

    float values[USER_PROFILE_COUNT];
    for (int i = 0; i < USER_PROFILE_COUNT; ++i) {
        values[i] = get_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode, i);
    }
    ui_manager_->ready_screen.update_profile_values(values, ui_manager_->current_mode);
}

void ReadyUIController::handle_tab_change(int tab) {
    if (!ui_manager_) {
        return;
    }

    ui_manager_->current_tab = tab;
    if (ui_manager_->profile_controller && tab < 3) {
        ui_manager_->profile_controller->set_current_profile(tab);
        refresh_profiles();
    }

    if (ui_manager_->grinding_controller_) {
        ui_manager_->grinding_controller_->update_grind_button_icon();
    }
}

void ReadyUIController::handle_profile_long_press() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (!ui_manager_->state_machine->is_state(UIState::READY) || ui_manager_->current_tab >= 3) {
        return;
    }

    ui_manager_->original_target = get_current_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode);
    ui_manager_->edit_target = ui_manager_->original_target;
    ui_manager_->edit_screen.set_mode(ui_manager_->current_mode);
    if (ui_manager_->edit_controller_) {
        ui_manager_->edit_controller_->update_display();
    }
    ui_manager_->switch_to_state(UIState::EDIT);
}

void ReadyUIController::toggle_mode() {
    if (!ui_manager_ || ui_manager_->current_tab >= 3) {
        return;
    }

    Preferences prefs;
    prefs.begin("swipe", true); // read-only
    bool swipe_enabled = prefs.getBool("enabled", false);
    prefs.end();

    if (!swipe_enabled) {
        return;
    }

    // Cycle: WEIGHT → TIME → CALIBRATED_TIME → WEIGHT
    switch (ui_manager_->current_mode) {
        case GrindMode::WEIGHT:          ui_manager_->current_mode = GrindMode::TIME; break;
        case GrindMode::TIME:            ui_manager_->current_mode = GrindMode::CALIBRATED_TIME; break;
        case GrindMode::CALIBRATED_TIME: ui_manager_->current_mode = GrindMode::WEIGHT; break;
    }

    if (ui_manager_->profile_controller) {
        ui_manager_->profile_controller->set_grind_mode(ui_manager_->current_mode);
    }

    refresh_profiles();
    ui_manager_->edit_target = get_current_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode);
    if (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::EDIT)) {
        if (ui_manager_->edit_controller_) {
            ui_manager_->edit_controller_->update_display();
        }
    }

    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    if (ui_manager_->state_machine &&
        (ui_manager_->state_machine->is_state(UIState::GRINDING) ||
         ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE))) {
        if (ui_manager_->grinding_controller_) {
            ui_manager_->grinding_controller_->update_grinding_targets();
        }
    }

    if (ui_manager_->grinding_controller_) {
        ui_manager_->grinding_controller_->update_grind_button_icon();
    }
}

void ReadyUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    lv_obj_t* ready_screen_obj = ui_manager_->ready_screen.get_screen();
    lv_obj_t* tabview = ui_manager_->ready_screen.get_tabview();

    if (tabview) {
        lv_obj_add_event_cb(tabview, EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::TAB_CHANGE)));
    }

    auto gesture_handler = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
            return;
        }
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM) {
            return;
        }
        UIManager* ui = static_cast<UIManager*>(lv_event_get_user_data(e));
        if (ui && ui->state_machine->is_state(UIState::READY) && ui->ready_controller_) {
            ui->ready_controller_->toggle_mode();
        }
    };

    if (tabview) {
        lv_obj_add_event_cb(tabview, gesture_handler, LV_EVENT_GESTURE, ui_manager_);
    }
    if (ready_screen_obj) {
        lv_obj_add_event_cb(ready_screen_obj, gesture_handler, LV_EVENT_GESTURE, ui_manager_);
    }
    lv_obj_add_event_cb(lv_scr_act(), gesture_handler, LV_EVENT_GESTURE, ui_manager_);

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::TAB_CHANGE,
                                      [this](lv_event_t* event) {
                                          lv_obj_t* tabview_obj = static_cast<lv_obj_t*>(lv_event_get_target(event));
                                          uint32_t tab_id = lv_tabview_get_tab_act(tabview_obj);
                                          handle_tab_change(static_cast<int>(tab_id));
                                      });

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::PROFILE_LONG_PRESS,
                                      [this](lv_event_t*) { handle_profile_long_press(); });

    ui_manager_->ready_screen.set_profile_long_press_handler(EventBridgeLVGL::profile_long_press_handler);

}
