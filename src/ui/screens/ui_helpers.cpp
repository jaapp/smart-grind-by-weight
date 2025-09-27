#include "ui_helpers.h"
#include <cstdio>

void style_as_button(lv_obj_t* object, int32_t width, int32_t height, const lv_font_t* font) {
    lv_obj_set_style_radius(object, THEME_CORNER_RADIUS_PX, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_hor(object, 20, 0);
    if (width >= 0){
        lv_obj_set_style_width(object, width, 0);
    }
    if (height >= 0){
        lv_obj_set_style_height(object, height, 0);
    }
    
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_button(lv_obj_t* parent, const char* text, lv_color_t bg_color, int32_t width, int32_t height, const lv_font_t* font){ 
    lv_obj_t* button = lv_btn_create(parent);
    style_as_button(button, width, height, font);
    lv_obj_set_style_bg_color(button, bg_color, 0);
    
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    
    return button;  
}

void set_label_text_int(lv_obj_t* label, int32_t value, const char* unit) {
    if (!label) return;
    char buf[24];

    if (unit) {
        snprintf(buf, sizeof(buf), "%ld %s", value, unit);
    } else {
        snprintf(buf, sizeof(buf), "%ld", value);
    }

    lv_label_set_text(label, buf);
}

void set_label_text_float(lv_obj_t* label, float value, const char* unit) {
    if (!label) return;
    char buf[24];
    
    if (unit) {
        snprintf(buf, sizeof(buf), "%.2fg %s", value, unit);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", value);
    }

    lv_label_set_text(label, buf);
}

lv_obj_t* create_profile_label(lv_obj_t* parent, lv_obj_t** profile_label, lv_obj_t** weight_label){
    lv_obj_t* label_container = lv_obj_create(parent);
    lv_obj_set_size(label_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(label_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label_container, 0, 0);
    lv_obj_set_style_pad_all(label_container, 0, 0);
    
    // Set up button container as horizontal flex
    lv_obj_set_layout(label_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(label_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(label_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(label_container, 0, 0);

    *profile_label = lv_label_create(label_container);
    lv_label_set_text(*profile_label, "DOUBLE");
    lv_obj_set_style_text_font(*profile_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(*profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    
    *weight_label = lv_label_create(label_container);
    lv_label_set_text(*weight_label, "18.0g");
    lv_obj_set_style_text_font(*weight_label, &lv_font_montserrat_60, 0);
    lv_obj_set_style_text_color(*weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    return label_container;
}

lv_obj_t* create_dual_button_row(lv_obj_t* parent, lv_obj_t** left_button, lv_obj_t** right_button, const char* left_name, const char* right_name, lv_color_t left_color, lv_color_t right_color, int height, const lv_font_t* font){
    lv_obj_t *row_container = lv_obj_create(parent);
    lv_obj_set_size(row_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_container, 0, 0);
    lv_obj_set_style_pad_all(row_container, 0, 0);
    
    lv_obj_set_layout(row_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row_container, 10, 0);

    *left_button = create_button(row_container, left_name, left_color, -1, height, font);
    lv_obj_set_flex_grow(*left_button, 1);

    *right_button = create_button(row_container, right_name, right_color, -1, height, font);
    lv_obj_set_flex_grow(*right_button, 1);

    return row_container;
}

