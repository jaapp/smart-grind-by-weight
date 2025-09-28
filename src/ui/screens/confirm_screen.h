#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

#define THEME_BUTTON_WIDTH_PX 120
#define CONFIRM_BUTTON_HEIGHT 60

class ConfirmScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* title_label;
    lv_obj_t* message_label;
    lv_obj_t* confirm_button;
    lv_obj_t* cancel_button;
    lv_obj_t* confirm_button_label;
    lv_obj_t* cancel_button_label;
    bool visible;

public:
    void create();
    void show(const char* title, const char* message, 
              const char* confirm_text, lv_color_t confirm_color,
              const char* cancel_text = "CANCEL");
    void show(); // Show with current content (no parameters)
    void hide();
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_confirm_button() const { return confirm_button; }
    lv_obj_t* get_cancel_button() const { return cancel_button; }
};

// Color helpers for common confirmation button types - use theme colors
// These can be passed directly to show() method as lv_color_hex(COLOR_xxx)