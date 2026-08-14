#pragma once
#include <lvgl.h>
#include "../event_bridge_lvgl.h"

class UIManager;

// Handles mode pane navigation and long-press target editing

class ReadyUIController {
public:
    explicit ReadyUIController(UIManager* manager);

    void register_events();
    void update();
    void refresh_targets();
    void handle_tab_change(int tab);
    void handle_target_long_press();
    void handle_swipe_down();

private:
    static void gesture_cb(lv_event_t* e);

    UIManager* ui_manager_;
};
