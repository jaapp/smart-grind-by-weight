#pragma once
#include <lvgl.h>
#include "../event_bridge_lvgl.h"

class UIManager;

// Handles profile tab navigation, long-press editing, and swipe mode switching

class ReadyUIController {
public:
    explicit ReadyUIController(UIManager* manager);

    void register_events();
    void update();
    void refresh_profiles();
    void handle_tab_change(int tab);
    void handle_profile_long_press();
    void toggle_mode();

    // Manual hold-to-grind (Scale tab). Safe to call when idle.
    void stop_manual_grind();

private:
    void handle_scale_tare();
    void handle_scale_grind(lv_event_t* e);
    void start_manual_grind();
    static void manual_grind_timeout_cb(lv_timer_t* timer);

    UIManager* ui_manager_;
    bool manual_grind_active_ = false;
    lv_timer_t* manual_grind_timeout_timer_ = nullptr;
};
