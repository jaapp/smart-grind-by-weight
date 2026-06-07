#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"

class ReadyScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* tabview;
    lv_obj_t* profile_tabs[4];
    lv_obj_t* weight_labels[3];
    lv_obj_t* advanced_panel;
    lv_obj_t* advanced_profile_cells[3];
    lv_obj_t* advanced_profile_name_labels[3];
    lv_obj_t* advanced_profile_value_labels[3];
    lv_obj_t* bean_card;
    lv_obj_t* bean_name_label;
    lv_obj_t* bean_roaster_label;
    lv_obj_t* bean_mahlgrad_label;
    lv_obj_t* bean_usage_label;
    lv_obj_t* bean_usage_bar;
    lv_obj_t* menu_tab;
    lv_obj_t* status_label;
    lv_timer_t* status_timer;
    int current_profile_index;
    bool advanced_ui_enabled;
    bool visible;

public:
    void create();
    void show();
    void hide();
    void update_profile_values(const float values[3], GrindMode mode);
    void update_bean_summary(bool has_active, const char* name, const char* roaster,
                             uint16_t mahlgrad_x2, uint32_t dose_used_x10,
                             uint32_t purge_used_x10, uint16_t bag_size_g);
    void set_active_tab(int tab);
    void set_advanced_ui_enabled(bool enabled);
    void set_profile_long_press_handler(lv_event_cb_t handler);
    void show_transient_status(const char* text, uint32_t duration_ms);
    void clear_status();
    
    bool is_visible() const { return visible; }
    bool is_advanced_ui_enabled() const { return advanced_ui_enabled; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_advanced_panel() const { return advanced_panel; }
    lv_obj_t* get_menu_tab() const { return menu_tab; }
    lv_obj_t* get_bean_card() const { return bean_card; }
    lv_obj_t* get_advanced_profile_cell(int index) const {
        return (index >= 0 && index < 3) ? advanced_profile_cells[index] : nullptr;
    }
    
private:
    void create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight);
    void create_advanced_page(lv_obj_t* parent);
    void refresh_profile_selection();
    void sync_advanced_visibility();
    void create_dose_icon(lv_obj_t* parent, int profile_index);
    void create_menu_page(lv_obj_t* parent);
    static void status_timer_cb(lv_timer_t* timer);
};
