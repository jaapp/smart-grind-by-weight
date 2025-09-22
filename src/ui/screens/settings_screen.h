#pragma once
#include <lvgl.h>
#include "../../config/ui_theme.h"
#include "../../bluetooth/manager.h"
#include "../../controllers/grind_controller.h"

class GrindingScreen;  // Forward declaration

class SettingsScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* tabview;
    lv_obj_t* info_tab;
    lv_obj_t* settings_tab;
    lv_obj_t* tools_tab;
    lv_obj_t* reset_tab;
    
    // Info tab elements
    lv_obj_t* info_label;
    lv_obj_t* uptime_label;
    lv_obj_t* memory_label;
    lv_obj_t* version_label;
    lv_obj_t* build_label;
    lv_obj_t* sessions_label;
    lv_obj_t* events_label;
    lv_obj_t* measurements_label;
    lv_obj_t* refresh_stats_button;
    
    // Settings tab elements
    lv_obj_t* ble_toggle;
    lv_obj_t* ble_startup_toggle;
    lv_obj_t* ble_status_label;
    lv_obj_t* ble_timer_label;
    lv_obj_t* logging_toggle;
    lv_obj_t* brightness_normal_slider;
    lv_obj_t* brightness_screensaver_slider;
    lv_obj_t* brightness_normal_label;
    lv_obj_t* brightness_screensaver_label;
    lv_obj_t* purge_button;
    lv_obj_t* reset_button;
    
    // Tools tab elements
    lv_obj_t* cal_button;
    lv_obj_t* motor_test_button;
    lv_obj_t* tare_button;
    lv_obj_t* taring_overlay;
    lv_obj_t* taring_label;
    
    // Common elements
    lv_obj_t* back_button;
    bool visible;
    
    BluetoothManager* bluetooth_manager;
    GrindController* grind_controller;
    GrindingScreen* grinding_screen;
    class HardwareManager* hardware_manager; // Forward declaration to access preferences

public:
    void create(BluetoothManager* bluetooth, GrindController* grind_ctrl, GrindingScreen* grind_screen, class HardwareManager* hw_mgr);
    void show();
    void hide();
    void update_info(const char* load_cell_info, unsigned long uptime_ms, size_t free_heap);
    void update_ble_status();
    void set_session_count(uint32_t count);
    void set_event_count(uint32_t count);
    void set_measurement_count(uint32_t count);
    void refresh_statistics();
    void show_taring_overlay();
    void hide_taring_overlay();
    void update_brightness_sliders();
    void update_brightness_labels();
    void update_bluetooth_startup_toggle();
    void update_logging_toggle();
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_cal_button() const { return cal_button; }
    lv_obj_t* get_purge_button() const { return purge_button; }
    lv_obj_t* get_reset_button() const { return reset_button; }
    lv_obj_t* get_motor_test_button() const { return motor_test_button; }
    lv_obj_t* get_tare_button() const { return tare_button; }
    lv_obj_t* get_back_button() const { return back_button; }
    lv_obj_t* get_ble_toggle() const { return ble_toggle; }
    lv_obj_t* get_ble_startup_toggle() const { return ble_startup_toggle; }
    lv_obj_t* get_logging_toggle() const { return logging_toggle; }
    lv_obj_t* get_refresh_stats_button() const { return refresh_stats_button; }
    lv_obj_t* get_brightness_normal_slider() const { return brightness_normal_slider; }
    lv_obj_t* get_brightness_screensaver_slider() const { return brightness_screensaver_slider; }
    
private:
    void create_info_page(lv_obj_t* parent);
    void create_settings_page(lv_obj_t* parent);
    void create_tools_page(lv_obj_t* parent);
    void create_reset_page(lv_obj_t* parent);
    void create_back_button();
    lv_obj_t* create_separator(lv_obj_t* parent, const char* text);
};
