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
    void refresh_bean_summary();
    void refresh_bean_list();
    void refresh_feedback_screen();
    void handle_tab_change(int tab);
    void handle_profile_select(lv_event_t* event);
    void handle_profile_swipe(lv_dir_t dir);
    void handle_profile_long_press();
    void handle_bean_card();
    void handle_bean_back();
    void handle_bean_select(lv_event_t* event);
    void handle_feedback(EventBridgeLVGL::EventType event_type);
    void toggle_mode();
    void reload_ready_ui_mode();

private:
    UIManager* ui_manager_;
};
