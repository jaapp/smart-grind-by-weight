#pragma once
#include <lvgl.h>
#include "screens/calibration_screen.h"

// Forward declarations
class UIManager;

// Base class for event handler groups
class UIEventHandler {
protected:
    UIManager* ui_manager;
    
public:
    UIEventHandler(UIManager* manager) : ui_manager(manager) {}
    virtual ~UIEventHandler() = default;
};

// Handler for profile-related events (tab switching, editing)
class ProfileEventHandler : public UIEventHandler {
public:
    ProfileEventHandler(UIManager* manager) : UIEventHandler(manager) {}
    
    void handle_tab_change(int tab);
    void handle_profile_long_press();
    void handle_edit_save();
    void handle_edit_cancel();
    void handle_edit_plus(lv_event_code_t code);
    void handle_edit_minus(lv_event_code_t code);
};

// Handler for grind control events
class GrindEventHandler : public UIEventHandler {
public:
    GrindEventHandler(UIManager* manager) : UIEventHandler(manager) {}
    
    void handle_grind_button();
};

// Handler for settings mode events
class SettingsEventHandler : public UIEventHandler {
public:
    SettingsEventHandler(UIManager* manager) : UIEventHandler(manager) {}
    
    void handle_settings_calibrate();
    void handle_settings_reset();
    void handle_settings_purge();
    void handle_settings_motor_test();
    void handle_settings_measurements_data();
    void handle_settings_ble_export();
    void handle_settings_tare();
    void handle_settings_back();
    void handle_ble_toggle();
    void handle_ble_startup_toggle();
    void handle_brightness_normal_slider();
    void handle_brightness_normal_slider_released();
    void handle_brightness_screensaver_slider();
    void handle_brightness_screensaver_slider_released();
    void handle_confirm();
    void handle_confirm_cancel();
};

// Handler for calibration events
class CalibrationEventHandler : public UIEventHandler {
public:
    CalibrationEventHandler(UIManager* manager) : UIEventHandler(manager) {}
    
    void handle_cal_ok();
    void handle_cal_cancel();
    void handle_cal_plus(lv_event_code_t code);
    void handle_cal_minus(lv_event_code_t code);
};
