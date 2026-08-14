#include "ready_controller.h"

#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
#include "../ui_manager.h"

ReadyUIController::ReadyUIController(UIManager* manager)
    : ui_manager_(manager) {}

void ReadyUIController::update() {}

void ReadyUIController::refresh_targets() {
    if (!ui_manager_ || !ui_manager_->profile_controller) {
        return;
    }

    ui_manager_->ready_screen.update_target_values(ui_manager_->profile_controller->get_current_weight(),
                                                   ui_manager_->profile_controller->get_current_time());
}

void ReadyUIController::handle_tab_change(int tab) {
    if (!ui_manager_) {
        return;
    }

    int previous_tab = ui_manager_->current_tab;
    ui_manager_->current_tab = tab;

    if (ui_manager_->manual_grind_controller_ && previous_tab != tab) {
        if (previous_tab == ReadyScreen::TAB_MANUAL) {
            ui_manager_->manual_grind_controller_->stop_and_reset();
        } else if (tab == ReadyScreen::TAB_MANUAL) {
            ui_manager_->manual_grind_controller_->on_enter();
        }
    }

    if (tab == ReadyScreen::TAB_WEIGHT || tab == ReadyScreen::TAB_TIME) {
        ui_manager_->current_mode = (tab == ReadyScreen::TAB_TIME) ? GrindMode::TIME : GrindMode::WEIGHT;
        if (ui_manager_->profile_controller) {
            ui_manager_->profile_controller->set_grind_mode(ui_manager_->current_mode);
        }
        ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
        refresh_targets();
    }

    if (ui_manager_->profile_controller) {
        ui_manager_->profile_controller->set_active_tab(tab);
    }

    if (ui_manager_->grinding_controller_) {
        ui_manager_->grinding_controller_->update_grind_button_icon();
    }
}

void ReadyUIController::gesture_cb(lv_event_t* e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_BOTTOM) {
        return;
    }
    auto* self = static_cast<ReadyUIController*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_swipe_down();
    }
}

void ReadyUIController::handle_swipe_down() {
    if (!ui_manager_ || !ui_manager_->state_machine ||
        !ui_manager_->state_machine->is_state(UIState::READY)) {
        return;
    }
    if (ui_manager_->screen_timeout_controller_) {
        ui_manager_->screen_timeout_controller_->start_screensaver_now();
    }
}

void ReadyUIController::handle_target_long_press() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (!ui_manager_->state_machine->is_state(UIState::READY) ||
        (ui_manager_->current_tab != ReadyScreen::TAB_WEIGHT &&
         ui_manager_->current_tab != ReadyScreen::TAB_TIME)) {
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

void ReadyUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    lv_obj_t* tabview = ui_manager_->ready_screen.get_tabview();

    if (tabview) {
        lv_obj_add_event_cb(tabview, EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::TAB_CHANGE)));
    }

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::TAB_CHANGE,
                                      [this](lv_event_t* event) {
                                          lv_obj_t* tabview_obj = static_cast<lv_obj_t*>(lv_event_get_target(event));
                                          uint32_t tab_id = lv_tabview_get_tab_act(tabview_obj);
                                          handle_tab_change(static_cast<int>(tab_id));
                                      });

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::TARGET_LONG_PRESS,
                                      [this](lv_event_t*) { handle_target_long_press(); });

    // Swipe down anywhere on the ready screen starts the screensaver.
    // Gestures bubble up to the active screen, so this also covers the grind
    // button strip below the tabview; the READY state gate keeps it inert on
    // every other screen.
    lv_obj_add_event_cb(lv_scr_act(), gesture_cb, LV_EVENT_GESTURE, this);

    ui_manager_->ready_screen.set_target_long_press_handler(EventBridgeLVGL::target_long_press_handler);
}
