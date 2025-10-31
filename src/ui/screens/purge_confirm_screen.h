#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

class PurgeConfirmScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* title_label;
    lv_obj_t* message_label;
    lv_obj_t* checkbox;
    bool visible;

public:
    void create();
    void show();
    void hide();

    bool is_visible() const { return visible; }
    bool is_checkbox_checked() const;
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_checkbox() const { return checkbox; }
};
