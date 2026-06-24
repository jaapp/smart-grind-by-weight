#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"

class ReadyScreen {
public:
    static constexpr int kTabCount = 5;     // Single, Double, Custom, MENU, Scale
    static constexpr int kScaleTabIndex = 4;

private:
    lv_obj_t* screen;
    lv_obj_t* tabview;
    lv_obj_t* profile_tabs[4];
    lv_obj_t* weight_labels[3];
    lv_obj_t* menu_tab;
    lv_obj_t* scale_tab;
    lv_obj_t* scale_weight_label;
    lv_obj_t* scale_tare_button;
    lv_obj_t* scale_grind_button;
    lv_obj_t* page_dots_container;
    lv_obj_t* page_dots[kTabCount];
    bool visible;

public:
    void create();
    void show();
    void hide();
    void update_profile_values(const float values[3], GrindMode mode);
    void set_active_tab(int tab);
    void set_profile_long_press_handler(lv_event_cb_t handler);
    void update_scale_weight(float weight);
    void update_page_dots(int active_index);

    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_menu_tab() const { return menu_tab; }
    lv_obj_t* get_scale_tare_button() const { return scale_tare_button; }
    lv_obj_t* get_scale_grind_button() const { return scale_grind_button; }

private:
    void create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight);
    void create_menu_page(lv_obj_t* parent);
    void create_scale_page(lv_obj_t* parent);
    void create_page_dots();
};
