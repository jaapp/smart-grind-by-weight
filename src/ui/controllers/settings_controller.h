#pragma once
#include <lvgl.h>

class UIManager;

// Manages BLE/logging toggles, brightness sliders, motor test, factory reset, and statistics

class SettingsUIController {
public:
    explicit SettingsUIController(UIManager* manager);

    void register_events();
    void update();
    void handle_calibrate();
    void handle_reset();
    void handle_purge();
    void handle_motor_test();
    void handle_tare();
    void handle_autotune();
    void handle_back();
    void handle_refresh_stats();
    void handle_diagnostics_reset();
    void handle_ble_toggle();
    void handle_ble_startup_toggle();
    void handle_logging_toggle();
    void handle_grind_mode_swipe_toggle();
    void handle_grind_mode_radio_button();
    void handle_brightness_normal_slider();
    void handle_brightness_normal_slider_released();
    void handle_brightness_screensaver_slider();
    void handle_brightness_screensaver_slider_released();

    float get_normal_brightness() const;
    float get_screensaver_brightness() const;

private:
    UIManager* ui_manager_;
    lv_timer_t* motor_timer_{};

    void perform_factory_reset();
    void execute_purge_operation();
    void run_motor_test();
    void stop_motor_timer();
    void motor_timer_cb(lv_timer_t* timer);
    static void static_motor_timer_cb(lv_timer_t* timer);
    void return_to_settings();
    void perform_diagnostics_reset();
};
