#include "ready_controller.h"

#include <Preferences.h>
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/bean_controller.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
#include "../ui_manager.h"

namespace {

void add_gesture_handler_recursive(lv_obj_t* obj, lv_event_cb_t handler, void* user_data) {
    if (!obj || !handler) {
        return;
    }

    lv_obj_add_event_cb(obj, handler, LV_EVENT_GESTURE, user_data);
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; ++i) {
        add_gesture_handler_recursive(lv_obj_get_child(obj, i), handler, user_data);
    }
}

} // namespace

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

void ReadyUIController::refresh_bean_summary() {
    if (!ui_manager_ || !ui_manager_->bean_controller) {
        return;
    }

    const BeanRecord* bean = ui_manager_->bean_controller->get_active_bean();
    if (!bean) {
        ui_manager_->ready_screen.update_bean_summary(false, nullptr, nullptr,
                                                      BeanController::kDefaultMahlgradX2,
                                                      0, 0, 0);
        return;
    }

    ui_manager_->ready_screen.update_bean_summary(
        true,
        bean->name,
        bean->roaster,
        bean->mahlgrad_x2[BeanController::kDoubleProfileIndex],
        bean->dose_used_x10,
        bean->purge_used_x10,
        bean->bag_size_g);
}

void ReadyUIController::refresh_bean_list() {
    if (!ui_manager_) {
        return;
    }
    ui_manager_->bean_list_screen.update(ui_manager_->bean_controller);
}

void ReadyUIController::refresh_feedback_screen() {
    if (!ui_manager_ || !ui_manager_->bean_controller) {
        return;
    }
    const BeanRecord* bean = ui_manager_->bean_controller->get_active_bean();
    if (!bean) {
        ui_manager_->bean_feedback_screen.update("", BeanController::kDefaultMahlgradX2);
        return;
    }
    ui_manager_->bean_feedback_screen.update(bean->name, bean->mahlgrad_x2[BeanController::kDoubleProfileIndex]);
}

void ReadyUIController::reload_ready_ui_mode() {
    if (!ui_manager_) {
        return;
    }

    bool advanced = USER_READY_UI_ADVANCED_DEFAULT;
    Preferences prefs;
    if (prefs.begin(USER_READY_UI_PREF_NAMESPACE, true)) {
        advanced = prefs.getBool(USER_READY_UI_PREF_KEY_ADVANCED, USER_READY_UI_ADVANCED_DEFAULT);
        prefs.end();
    }
    ui_manager_->ready_screen.set_advanced_ui_enabled(advanced);
    ui_manager_->ready_screen.set_active_tab(ui_manager_->current_tab);
}

void ReadyUIController::handle_tab_change(int tab) {
    if (!ui_manager_) {
        return;
    }

    if (ui_manager_->ready_screen.is_advanced_ui_enabled() && tab >= USER_PROFILE_COUNT) {
        tab = ui_manager_->profile_controller
                  ? ui_manager_->profile_controller->get_current_profile()
                  : BeanController::kDoubleProfileIndex;
    }
    if (tab < 0 || tab >= USER_PROFILE_COUNT + 1) {
        tab = BeanController::kDoubleProfileIndex;
    }

    ui_manager_->current_tab = tab;
    if (ui_manager_->profile_controller && tab < USER_PROFILE_COUNT) {
        ui_manager_->profile_controller->set_current_profile(tab);
        ui_manager_->edit_target = get_current_profile_target(*ui_manager_->profile_controller, ui_manager_->current_mode);
        refresh_profiles();
    }
    ui_manager_->ready_screen.set_active_tab(tab);

    if (ui_manager_->grinding_controller_) {
        ui_manager_->grinding_controller_->update_grind_button_icon();
    }
}

void ReadyUIController::handle_profile_select(lv_event_t* event) {
    if (!ui_manager_ || !event) {
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const int tab = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
    if (tab < 0 || tab >= 3) {
        return;
    }

    handle_tab_change(tab);
}

void ReadyUIController::handle_profile_swipe(lv_dir_t dir) {
    if (!ui_manager_ || !ui_manager_->state_machine ||
        !ui_manager_->state_machine->is_state(UIState::READY) ||
        !ui_manager_->ready_screen.is_advanced_ui_enabled()) {
        return;
    }

    int next_tab = ui_manager_->current_tab;
    if (dir == LV_DIR_LEFT) {
        next_tab = next_tab < USER_PROFILE_COUNT - 1 ? next_tab + 1 : USER_PROFILE_COUNT - 1;
    } else if (dir == LV_DIR_RIGHT) {
        next_tab = next_tab > 0 ? next_tab - 1 : 0;
    } else {
        return;
    }

    handle_tab_change(next_tab);
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

void ReadyUIController::handle_bean_card() {
    if (!ui_manager_ || !ui_manager_->state_machine || !ui_manager_->state_machine->is_state(UIState::READY)) {
        return;
    }
    refresh_bean_list();
    ui_manager_->switch_to_state(UIState::BEAN_LIST);
}

void ReadyUIController::handle_bean_back() {
    if (!ui_manager_) {
        return;
    }
    ui_manager_->switch_to_state(UIState::READY);
}

void ReadyUIController::handle_bean_select(lv_event_t* event) {
    if (!ui_manager_ || !ui_manager_->bean_controller || !event) {
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const uint8_t id = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
    if (id != 0 && ui_manager_->bean_controller->set_active_bean(id)) {
        refresh_bean_summary();
    }
    ui_manager_->switch_to_state(UIState::READY);
}

void ReadyUIController::handle_feedback(EventBridgeLVGL::EventType event_type) {
    if (!ui_manager_ || !ui_manager_->bean_controller) {
        return;
    }

    bool stay_on_feedback = false;
    switch (event_type) {
        case EventBridgeLVGL::EventType::BEAN_FEEDBACK_FINER:
            ui_manager_->bean_controller->apply_feedback_to_active(BeanController::Feedback::FINER);
            stay_on_feedback = true;
            break;
        case EventBridgeLVGL::EventType::BEAN_FEEDBACK_OK:
            ui_manager_->bean_controller->apply_feedback_to_active(BeanController::Feedback::OK);
            break;
        case EventBridgeLVGL::EventType::BEAN_FEEDBACK_COARSER:
            ui_manager_->bean_controller->apply_feedback_to_active(BeanController::Feedback::COARSER);
            stay_on_feedback = true;
            break;
        case EventBridgeLVGL::EventType::BEAN_FEEDBACK_SKIP:
        default:
            break;
    }

    refresh_bean_summary();
    if (stay_on_feedback) {
        refresh_feedback_screen();
        return;
    }

    if (ui_manager_->grind_controller) {
        ui_manager_->grind_controller->return_to_idle();
    } else {
        ui_manager_->switch_to_state(UIState::READY);
    }
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

    ui_manager_->current_mode = (ui_manager_->current_mode == GrindMode::WEIGHT)
                                    ? GrindMode::TIME
                                    : GrindMode::WEIGHT;

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
    if (lv_obj_t* bean_card = ui_manager_->ready_screen.get_bean_card()) {
        lv_obj_add_event_cb(bean_card, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::BEAN_CARD)));
    }

    auto gesture_handler = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
            return;
        }
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        UIManager* ui = static_cast<UIManager*>(lv_event_get_user_data(e));
        if (!ui || !ui->state_machine->is_state(UIState::READY) || !ui->ready_controller_) {
            return;
        }

        bool handled = false;
        if (ui->ready_screen.is_advanced_ui_enabled()) {
            if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
                ui->ready_controller_->handle_profile_swipe(dir);
                handled = true;
            } else if (dir == LV_DIR_TOP) {
                ui->switch_to_state(UIState::MENU);
                handled = true;
            } else if (dir == LV_DIR_BOTTOM) {
                ui->ready_controller_->toggle_mode();
                handled = true;
            }

            if (handled) {
                lv_event_stop_bubbling(e);
                return;
            }
        }

        if (dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM) {
            ui->ready_controller_->toggle_mode();
            lv_event_stop_bubbling(e);
        }
    };

    if (ready_screen_obj) {
        add_gesture_handler_recursive(ready_screen_obj, gesture_handler, ui_manager_);
    }
    if (ui_manager_->grinding_controller_) {
        add_gesture_handler_recursive(ui_manager_->grinding_controller_->get_grind_button(), gesture_handler, ui_manager_);
        add_gesture_handler_recursive(ui_manager_->grinding_controller_->get_pulse_button(), gesture_handler, ui_manager_);
    }
    if (tabview && !ready_screen_obj) {
        lv_obj_add_event_cb(tabview, gesture_handler, LV_EVENT_GESTURE, ui_manager_);
    }
    lv_obj_add_event_cb(lv_scr_act(), gesture_handler, LV_EVENT_GESTURE, ui_manager_);

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::PROFILE_SELECT,
                                      [this](lv_event_t* event) { handle_profile_select(event); });

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::TAB_CHANGE,
                                      [this](lv_event_t* event) {
                                          lv_obj_t* tabview_obj = static_cast<lv_obj_t*>(lv_event_get_target(event));
                                          uint32_t tab_id = lv_tabview_get_tab_act(tabview_obj);
                                          handle_tab_change(static_cast<int>(tab_id));
                                      });

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::PROFILE_LONG_PRESS,
                                      [this](lv_event_t*) { handle_profile_long_press(); });

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_CARD,
                                      [this](lv_event_t*) { handle_bean_card(); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_BACK,
                                      [this](lv_event_t*) { handle_bean_back(); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_SELECT,
                                      [this](lv_event_t* e) { handle_bean_select(e); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_FINER,
                                      [this](lv_event_t*) { handle_feedback(EventBridgeLVGL::EventType::BEAN_FEEDBACK_FINER); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_OK,
                                      [this](lv_event_t*) { handle_feedback(EventBridgeLVGL::EventType::BEAN_FEEDBACK_OK); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_COARSER,
                                      [this](lv_event_t*) { handle_feedback(EventBridgeLVGL::EventType::BEAN_FEEDBACK_COARSER); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::BEAN_FEEDBACK_SKIP,
                                      [this](lv_event_t*) { handle_feedback(EventBridgeLVGL::EventType::BEAN_FEEDBACK_SKIP); });
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::READY_SETTINGS,
                                      [this](lv_event_t*) {
                                          if (ui_manager_ && ui_manager_->state_machine &&
                                              ui_manager_->state_machine->is_state(UIState::READY)) {
                                              ui_manager_->switch_to_state(UIState::MENU);
                                          }
                                      });

    ui_manager_->ready_screen.set_profile_long_press_handler(EventBridgeLVGL::profile_long_press_handler);
    reload_ready_ui_mode();

}
