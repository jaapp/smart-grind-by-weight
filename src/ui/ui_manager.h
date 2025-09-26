#pragma once
#include <lvgl.h>
#include "components/blocking_overlay.h"
#include "components/ui_operations.h"
#include "screens/ready_screen.h"
#include "screens/edit_screen.h"
#include "screens/grinding_screen.h"
#include "screens/settings_screen.h"
#include "screens/calibration_screen.h"
#include "screens/confirm_screen.h"
#include "screens/ota_screen.h"
#include "screens/ota_update_failed_screen.h"
#include "event_bridge_lvgl.h"
#include "../system/state_machine.h"
#include "../controllers/profile_controller.h"
#include "../controllers/grind_controller.h"
#include "../controllers/grind_events.h"
#include "../controllers/grind_mode.h"
#include "../hardware/hardware_manager.h"
#include "../bluetooth/manager.h"

/*
 * AVAILABLE FONTS AND THEIR USAGE:
 * - lv_font_montserrat_24: Standard text and button labels
 * - lv_font_montserrat_32: Button symbols (OK, CLOSE, PLUS, MINUS)
 * - lv_font_montserrat_36: Screen titles
 * - lv_font_montserrat_56: Large weight displays
 * 
 * JOG ACCELERATION STAGES:
 * - Stage 1 (0-2s): 1.0g/s (100ms intervals, 1x multiplier)
 * - Stage 2 (2-4s): 4.7g/s (64ms intervals, 3x multiplier) 
 * - Stage 3 (4-6s): 9.4g/s (64ms intervals, 6x multiplier)
 * - Stage 4 (6s+): 20.3g/s (64ms intervals, 13x multiplier)
 */

// Forward declarations for event handlers
class ProfileEventHandler;
class GrindEventHandler;
class SettingsEventHandler;
class CalibrationEventHandler;

class UIManager {
    friend class ProfileEventHandler;
    friend class GrindEventHandler;
    friend class SettingsEventHandler;
    friend class CalibrationEventHandler;
    
private:
    HardwareManager* hardware_manager;
    StateMachine* state_machine;
    ProfileController* profile_controller;
    GrindController* grind_controller;
    BluetoothManager* bluetooth_manager;
    
    lv_obj_t* grind_button;
    lv_obj_t* grind_icon;
    lv_obj_t* ble_status_icon;
    lv_timer_t* jog_timer;
    lv_timer_t* motor_timer;
    lv_timer_t* grind_complete_timer;
    lv_timer_t* grind_timeout_timer;
    
    float edit_weight;
    float original_weight;
    float calibration_weight;
    float final_grind_weight;
    int final_grind_progress;
    float error_grind_weight;
    int error_grind_progress;
    char error_message[32];
    int current_tab;
    GrindMode current_mode;
    bool chart_updates_enabled;
    bool initialized;
    unsigned long jog_start_time;
    int jog_stage;
    int jog_direction;
    char ota_failed_expected_build[16];
    bool data_export_active;
    
    // Screen timeout state
    bool screen_dimmed;
    
#if HW_LOADCELL_USE_MOCK_DRIVER
    // Mock grinder background state
    lv_style_t style_screen;
    bool mock_grinder_background_active;
#endif
    
    // Function pointer for pending confirmation action
    void (*pending_confirm_callback)();
    
    // Event handler instances
    ProfileEventHandler* profile_handler;
    GrindEventHandler* grind_handler;
    SettingsEventHandler* settings_handler;
    CalibrationEventHandler* calibration_handler;
    
    // Static instance pointer for grind event callback
    static UIManager* instance;

public:
    ReadyScreen ready_screen;
    EditScreen edit_screen;
    GrindingScreen grinding_screen;
    SettingsScreen settings_screen;
    CalibrationScreen calibration_screen;
    ConfirmScreen confirm_screen;
    OTAScreen ota_screen;
    OtaUpdateFailedScreen ota_update_failed_screen;

    void init(HardwareManager* hw_mgr, StateMachine* sm, 
              ProfileController* pc, GrindController* gc, BluetoothManager* bluetooth);
    void update();
    void switch_to_state(UIState new_state);
    
    // Event handlers
    void handle_tab_change(int tab);
    void handle_profile_long_press();
    void handle_grind_button();
    void handle_edit_save();
    void handle_edit_cancel();
    void handle_edit_plus(lv_event_code_t code);
    void handle_edit_minus(lv_event_code_t code);
    void handle_settings_calibrate();
    void handle_settings_reset();
    void handle_settings_purge();
    void handle_settings_motor_test();
    void handle_settings_tare();
    void handle_settings_back();
    void handle_settings_refresh_stats();
    void handle_cal_ok();
    void handle_cal_cancel();
    void handle_cal_plus(lv_event_code_t code);
    void handle_cal_minus(lv_event_code_t code);
    void handle_ble_toggle();
    void handle_ble_startup_toggle();
    void handle_logging_toggle();
    void handle_brightness_normal_slider();
    void handle_brightness_normal_slider_released();
    void handle_brightness_screensaver_slider();
    void handle_brightness_screensaver_slider_released();
    void handle_confirm();
    void handle_confirm_cancel();
    void handle_grind_layout_toggle();
    
    // Helper method to show confirmation dialog
    void show_confirmation(const char* title, const char* message, 
                          const char* confirm_text, lv_color_t confirm_color,
                          void (*on_confirm)(), const char* cancel_text = "CANCEL");
    
    // Helper methods for brightness settings
    float get_normal_brightness() const;
    float get_screensaver_brightness() const;
    
    // Static instance getter
    static UIManager* get_instance() { return instance; }
    
    // Public accessors for static callbacks
    ProfileController* get_profile_controller() { return profile_controller; }
    HardwareManager* get_hardware_manager() { return hardware_manager; }
    GrindController* get_grind_controller() { return grind_controller; }
    void set_current_tab(int tab) { current_tab = tab; }
    
    // OTA update methods
    void update_ota_progress(int percent);
    void update_ota_status(const char* status);
    void show_ota_update_failed_warning(const char* expected_build);
    void set_ota_failure_info(const char* expected_build);
    
    // Data export methods
    void start_data_export_ui();
    void update_data_export_progress();
    void stop_data_export_ui();
    
    // Event-driven grind updates
    static void grind_event_handler(const GrindEventData& event_data);
    
#if HW_LOADCELL_USE_MOCK_DRIVER
    void update_mock_grinder_background(bool grinder_active);
#endif
    

private:
    void create_ui();
    void create_grind_button();
    void create_ble_status_icon();
    void update_ble_status_icon();
    void refresh_ready_profiles();
    void setup_event_handlers();
    void update_grind_button_icon();
    void update_edit_weight_display();
    void start_jog_timer(int direction);
    void stop_jog_timer();
    
    // State-specific update methods
    void update_settings_state();
    void update_calibration_state();
    void update_grind_complete_state();
    void update_grind_timeout_state();
    void update_screen_timeout_state();
    
    void jog_timer_cb(lv_timer_t* timer);
    void motor_timer_cb(lv_timer_t* timer);
    void grind_complete_timeout_cb(lv_timer_t* timer);
    void grind_timeout_timer_cb(lv_timer_t* timer);

    void toggle_mode();
    
    
    // Static wrappers for LVGL callbacks
    static void static_jog_timer_cb(lv_timer_t* timer);
    static void static_motor_timer_cb(lv_timer_t* timer);
    static void static_grind_complete_timeout_cb(lv_timer_t* timer);
    static void static_grind_timeout_timer_cb(lv_timer_t* timer);
};
