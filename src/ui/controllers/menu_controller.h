#pragma once
#include <lvgl.h>

class UIManager;

// Handles the interactive menu: BLE/logging toggles, brightness sliders, maintenance actions, and stats

class MenuUIController {
public:
    explicit MenuUIController(UIManager* manager);

    void register_events();
    void update();
    void handle_calibrate();
    void handle_reset();
    void handle_purge();
    void handle_motor_test();
    void handle_autotune();
    void handle_back();
    void handle_refresh_stats();
    void handle_diagnostics_reset();
    void handle_ble_toggle();
    void handle_ble_startup_toggle();
    void handle_logging_toggle();
    void handle_grind_mode_swipe_toggle();
    void handle_grind_mode_radio_button();
    void handle_auto_start_toggle();
    void handle_auto_return_toggle();
    void handle_grinder_purge_mode_radio_button();
    void handle_grinder_purge_amount_slider();
    void handle_grinder_purge_amount_slider_released();
    void handle_grind_freshness_hours_slider();
    void handle_grind_freshness_hours_slider_released();
    void handle_brightness_normal_slider();
    void handle_brightness_normal_slider_released();
    void handle_brightness_screensaver_slider();
    void handle_brightness_screensaver_slider_released();
    void handle_screensaver_timeout_slider();
    void handle_screensaver_timeout_slider_released();
    void handle_screensaver_off_timeout_slider();
    void handle_screensaver_off_timeout_slider_released();
    void handle_screensaver_mode_radio_button();

    float get_normal_brightness() const;
    float get_screensaver_brightness() const;
    uint32_t get_screensaver_timeout_ms() const;      // stage 1: dim timeout
    uint32_t get_screensaver_off_timeout_ms() const;  // stage 2: display-off timeout
    int get_screensaver_mode() const;                 // stage-1 style: DIM or LOGO

private:
    UIManager* ui_manager_;
    lv_timer_t* motor_timer_{};

    void perform_factory_reset();
    void execute_purge_operation();
    void run_motor_test();
    void stop_motor_timer();
    void motor_timer_cb(lv_timer_t* timer);
    static void static_motor_timer_cb(lv_timer_t* timer);
    void return_to_menu();
    void perform_diagnostics_reset();
};
