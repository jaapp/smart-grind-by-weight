#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"

class ReadyScreen {
public:
    // Mirrored by the tab constants in ProfileController
    static constexpr int TAB_MANUAL = 0;
    static constexpr int TAB_TIME = 1;
    static constexpr int TAB_WEIGHT = 2;
    static constexpr int TAB_MENU = 3;

    void create();
    void show();
    void hide();
    void update_target_values(float weight_g, float time_s);
    void update_manual_time(float elapsed_s);
    void update_manual_weight(float weight_g);
    void show_manual_tare_pulse(uint32_t now_ms);
    void reset_manual_readouts();
    void set_active_tab(int tab);
    void set_target_long_press_handler(lv_event_cb_t handler);

    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_menu_tab() const { return menu_tab; }
    lv_obj_t* get_manual_time_label() const { return manual_time_label; }
    lv_obj_t* get_manual_weight_label() const { return manual_weight_label; }

private:
    lv_obj_t* screen;
    lv_obj_t* tabview;
    lv_obj_t* mode_tabs[4];
    lv_obj_t* target_labels[2];
    lv_obj_t* manual_time_label;
    lv_obj_t* manual_weight_label;
    lv_obj_t* menu_tab;
    bool visible;
    bool manual_tare_pulse_active;

    void create_target_page(lv_obj_t* parent, GrindMode mode);
    void create_manual_page(lv_obj_t* parent);
    void create_menu_page(lv_obj_t* parent);
};
