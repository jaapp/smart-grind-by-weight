#include "ready_controller.h"

#include "preferences_idf.h"
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../../logging/grind_logging.h"
#include "../components/ui_operations.h"
#include "../event_bridge_lvgl.h"
#include "../ui_manager.h"

// Hard safety cap: stop a held manual grind after this long even if the
// button is still pressed (or a release event was missed).
static constexpr uint32_t kManualGrindMaxMs = 10000;

ReadyUIController::ReadyUIController(UIManager* manager)
    : ui_manager_(manager) {}

void ReadyUIController::update() {
    if (!ui_manager_) {
        return;
    }
    // Drive the live weight readout while the Scale tab is showing
    if (ui_manager_->current_tab == UIManager::kScaleTabIndex &&
        ui_manager_->state_machine->is_state(UIState::READY)) {
        auto* hw = ui_manager_->get_hardware_manager();
        auto* sensor = hw ? hw->get_weight_sensor() : nullptr;
        ui_manager_->ready_screen.update_scale_weight(sensor ? sensor->get_display_weight() : 0.0f);
    }
}

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

    // Leaving the Scale tab must always stop a held manual grind
    if (tab != UIManager::kScaleTabIndex) {
        stop_manual_grind();
    }

    ui_manager_->ready_screen.update_page_dots(tab);

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

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::SCALE_TARE,
                                      [this](lv_event_t*) { handle_scale_tare(); });

    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::SCALE_GRIND,
                                      [this](lv_event_t* e) { handle_scale_grind(e); });

    ui_manager_->ready_screen.set_profile_long_press_handler(EventBridgeLVGL::profile_long_press_handler);

}

void ReadyUIController::handle_scale_tare() {
    if (!ui_manager_) {
        return;
    }
    auto* hardware = ui_manager_->get_hardware_manager();
    if (!hardware) {
        return;
    }

    // Never tare while the motor is running
    stop_manual_grind();

    UIOperations::execute_tare(hardware, [this]() {
        if (!ui_manager_) {
            return;
        }
        auto* sensor = ui_manager_->get_hardware_manager()->get_weight_sensor();
        ui_manager_->ready_screen.update_scale_weight(sensor ? sensor->get_display_weight() : 0.0f);
    });
}

void ReadyUIController::handle_scale_grind(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        start_manual_grind();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        stop_manual_grind();
    }
}

void ReadyUIController::start_manual_grind() {
    if (!ui_manager_ || manual_grind_active_) {
        return;
    }
    // Only grind from the Scale tab in the READY state
    if (!ui_manager_->state_machine->is_state(UIState::READY) ||
        ui_manager_->current_tab != UIManager::kScaleTabIndex) {
        return;
    }

    auto* hardware = ui_manager_->get_hardware_manager();
    auto* grinder = hardware ? hardware->get_grinder() : nullptr;
    if (!grinder) {
        return;
    }

    manual_grind_active_ = true;
    grinder->start();
    ui_manager_->set_background_active(true);

    if (manual_grind_timeout_timer_) {
        lv_timer_del(manual_grind_timeout_timer_);
    }
    manual_grind_timeout_timer_ = lv_timer_create(manual_grind_timeout_cb, kManualGrindMaxMs, this);
    if (manual_grind_timeout_timer_) {
        lv_timer_set_repeat_count(manual_grind_timeout_timer_, 1);
    }

    LOG_BLE("[%lums MANUAL_GRIND] Started (hold-to-grind)\n", millis());
}

void ReadyUIController::stop_manual_grind() {
    if (manual_grind_timeout_timer_) {
        lv_timer_del(manual_grind_timeout_timer_);
        manual_grind_timeout_timer_ = nullptr;
    }

    if (!manual_grind_active_) {
        return;
    }
    manual_grind_active_ = false;

    auto* hardware = ui_manager_ ? ui_manager_->get_hardware_manager() : nullptr;
    auto* grinder = hardware ? hardware->get_grinder() : nullptr;
    if (grinder) {
        grinder->stop();
    }
    if (ui_manager_) {
        ui_manager_->set_background_active(false);
    }

    LOG_BLE("[%lums MANUAL_GRIND] Stopped\n", millis());
}

void ReadyUIController::manual_grind_timeout_cb(lv_timer_t* timer) {
    auto* self = static_cast<ReadyUIController*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }
    // One-shot timer: LVGL deletes it after this callback, so drop our handle first
    self->manual_grind_timeout_timer_ = nullptr;
    LOG_BLE("[%lums MANUAL_GRIND] Safety auto-stop (%lums cap reached)\n", millis(), (unsigned long)kManualGrindMaxMs);
    self->stop_manual_grind();
}
