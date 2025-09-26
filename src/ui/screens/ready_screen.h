#pragma once
#include <lvgl.h>
#include "../../config/ui_theme.h"

class ReadyScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* tabview;
    lv_obj_t* profile_tabs[4];
    lv_obj_t* weight_labels[3];
    lv_obj_t* settings_tab;
    bool visible;

public:
    void create();
    void show();
    void hide();
    void update_profile_values(const float values[3], bool show_time);
    void set_active_tab(int tab);
    void set_profile_long_press_handler(lv_event_cb_t handler);
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_settings_tab() const { return settings_tab; }
    
private:
    void create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight);
    void create_settings_page(lv_obj_t* parent);
};
