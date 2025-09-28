#include "event_bridge_lvgl.h"
#include "ui_manager.h"
#include "../config/constants.h"

UIManager* EventBridgeLVGL::ui_manager = nullptr;

void EventBridgeLVGL::set_ui_manager(UIManager* mgr) {
    ui_manager = mgr;
}

void EventBridgeLVGL::dispatch_event(lv_event_t* e) {
    if (!ui_manager) {
        LOG_BLE("[ERROR] EventBridgeLVGL: UI manager not initialized\n");
        return;
    }
    
    // Get event type from user data
    EventType event_type = static_cast<EventType>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    handle_event(event_type, e);
}

void EventBridgeLVGL::profile_long_press_handler(lv_event_t* e) {
    handle_event(EventType::PROFILE_LONG_PRESS, e);
}

void EventBridgeLVGL::handle_event(EventType event_type, lv_event_t* e) {
    switch (event_type) {
        case EventType::TAB_CHANGE: {
            lv_obj_t* tabview = static_cast<lv_obj_t*>(lv_event_get_target(e));
            uint32_t tab_id = lv_tabview_get_tab_act(tabview);
            if (tab_id < 4) {
                ui_manager->handle_tab_change(tab_id);
            }
            break;
        }
        
        case EventType::PROFILE_LONG_PRESS:
            ui_manager->handle_profile_long_press();
            break;
            
        case EventType::GRIND_BUTTON:
            LOG_BLE("[%lums LVGL_EVENT] grind_button triggered\n", millis());
            ui_manager->handle_grind_button();
            break;
            
        case EventType::PULSE_BUTTON:
            LOG_BLE("[%lums LVGL_EVENT] pulse_button triggered\n", millis());
            ui_manager->handle_pulse_button();
            break;
            
        case EventType::EDIT_SAVE:
            ui_manager->handle_edit_save();
            break;
            
        case EventType::EDIT_CANCEL:
            ui_manager->handle_edit_cancel();
            break;
            
        case EventType::EDIT_PLUS: {
            lv_event_code_t code = lv_event_get_code(e);
            ui_manager->handle_edit_plus(code);
            break;
        }
        
        case EventType::EDIT_MINUS: {
            lv_event_code_t code = lv_event_get_code(e);
            ui_manager->handle_edit_minus(code);
            break;
        }
        
        case EventType::SETTINGS_CALIBRATE:
            ui_manager->handle_settings_calibrate();
            break;
            
        case EventType::SETTINGS_RESET:
            ui_manager->handle_settings_reset();
            break;
            
        case EventType::SETTINGS_PURGE:
            ui_manager->handle_settings_purge();
            break;
            
        case EventType::SETTINGS_MOTOR_TEST:
            ui_manager->handle_settings_motor_test();
            break;
            
        case EventType::SETTINGS_TARE:
            ui_manager->handle_settings_tare();
            break;
            
        case EventType::SETTINGS_BACK:
            ui_manager->handle_settings_back();
            break;
            
        case EventType::SETTINGS_REFRESH_STATS:
            ui_manager->handle_settings_refresh_stats();
            break;
            
        case EventType::BLE_TOGGLE:
            ui_manager->handle_ble_toggle();
            break;

        case EventType::BLE_STARTUP_TOGGLE:
            ui_manager->handle_ble_startup_toggle();
            break;

        case EventType::LOGGING_TOGGLE:
            ui_manager->handle_logging_toggle();
            break;

        case EventType::BRIGHTNESS_NORMAL_SLIDER:
            ui_manager->handle_brightness_normal_slider();
            break;
        
        case EventType::BRIGHTNESS_NORMAL_SLIDER_RELEASED:
            ui_manager->handle_brightness_normal_slider_released();
            break;
            
        case EventType::BRIGHTNESS_SCREENSAVER_SLIDER:
            ui_manager->handle_brightness_screensaver_slider();
            break;
            
        case EventType::BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED:
            ui_manager->handle_brightness_screensaver_slider_released();
            break;
            
        case EventType::CAL_OK:
            ui_manager->handle_cal_ok();
            break;
            
        case EventType::CAL_CANCEL:
            ui_manager->handle_cal_cancel();
            break;
            
        case EventType::CAL_PLUS: {
            lv_event_code_t code = lv_event_get_code(e);
            ui_manager->handle_cal_plus(code);
            break;
        }
        
        case EventType::CAL_MINUS: {
            lv_event_code_t code = lv_event_get_code(e);
            ui_manager->handle_cal_minus(code);
            break;
        }
        
        case EventType::CONFIRM:
            ui_manager->handle_confirm();
            break;
            
        case EventType::CONFIRM_CANCEL:
            ui_manager->handle_confirm_cancel();
            break;

        default:
            LOG_BLE("[WARNING] EventBridgeLVGL: Unknown event type: %d\n", static_cast<int>(event_type));
            break;
    }
}
