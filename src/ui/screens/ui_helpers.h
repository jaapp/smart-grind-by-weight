#pragma once
#include <lvgl.h>
#include "../../config/ui_theme.h"

inline void style_as_button(lv_obj_t* object, int32_t width = 260, int32_t height = 80) {
    lv_obj_set_style_radius(object, THEME_CORNER_RADIUS_PX, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(object, &lv_font_montserrat_28, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_hor(object, 20, 0);
    lv_obj_set_style_size(object, width, height, 0);
    
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);   
}

inline lv_obj_t* create_button(lv_obj_t* parent, const char* text,
                                lv_color_t bg_color = lv_color_hex(THEME_COLOR_NEUTRAL),
                                int32_t width = 260, int32_t height = 80){ 
    lv_obj_t* button = lv_btn_create(parent);
    style_as_button(button, width, height);
    lv_obj_set_style_bg_color(button, bg_color, 0);
    
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    
    return button;  
}