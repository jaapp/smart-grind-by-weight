#pragma once
#include <lvgl.h>
#include "../../config/ui_theme.h"
#include "../../controllers/grind_mode.h"

class EditScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* profile_label;
    lv_obj_t* weight_label;
    lv_obj_t* save_btn;
    lv_obj_t* cancel_btn;
    lv_obj_t* plus_btn;
    lv_obj_t* minus_btn;
    bool visible;
    GrindMode mode;

public:
    void create();
    void show();
    void hide();
    void update_profile_name(const char* name);
    void update_target(float value);
    void set_mode(GrindMode time_mode);
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_save_btn() const { return save_btn; }
    lv_obj_t* get_cancel_btn() const { return cancel_btn; }
    lv_obj_t* get_plus_btn() const { return plus_btn; }
    lv_obj_t* get_minus_btn() const { return minus_btn; }
};
