#include "edit_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"

void EditScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Save button (top left)
    save_btn = lv_btn_create(screen);
    lv_obj_set_size(save_btn, THEME_BUTTON_WIDTH_PX, 75);
    lv_obj_align(save_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_32, 0);
    lv_obj_center(save_label);

    // Cancel button (top right)
    cancel_btn = lv_btn_create(screen);
    lv_obj_set_size(cancel_btn, THEME_BUTTON_WIDTH_PX, 75);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_border_width(cancel_btn, 0, 0);
    
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_32, 0);
    lv_obj_center(cancel_label);

    // Profile name label (center)
    profile_label = lv_label_create(screen);
    lv_label_set_text(profile_label, "DOUBLE");
    lv_obj_set_style_text_font(profile_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_align(profile_label, LV_ALIGN_CENTER, 0, -40);

    // Weight label (center)
    weight_label = lv_label_create(screen);
    lv_label_set_text(weight_label, "18.0g");
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, 10);

    // Minus button (bottom left)
    minus_btn = lv_btn_create(screen);
    lv_obj_set_size(minus_btn, THEME_BUTTON_WIDTH_PX, THEME_BUTTON_HEIGHT_PX);
    lv_obj_align(minus_btn, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(THEME_COLOR_ERROR), 0);
    lv_obj_set_style_border_width(minus_btn, 0, 0);
    
    lv_obj_t* minus_label = lv_label_create(minus_btn);
    lv_label_set_text(minus_label, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_32, 0);
    lv_obj_center(minus_label);

    // Plus button (bottom right)
    plus_btn = lv_btn_create(screen);
    lv_obj_set_size(plus_btn, THEME_BUTTON_WIDTH_PX, THEME_BUTTON_HEIGHT_PX);
    lv_obj_align(plus_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(THEME_COLOR_ERROR), 0);
    lv_obj_set_style_border_width(plus_btn, 0, 0);
    
    lv_obj_t* plus_label = lv_label_create(plus_btn);
    lv_label_set_text(plus_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_32, 0);
    lv_obj_center(plus_label);

    visible = false;
    show_time_mode = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void EditScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void EditScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void EditScreen::update_profile_name(const char* name) {
    lv_label_set_text(profile_label, name);
}

void EditScreen::update_weight(float weight) {
    char weight_text[16];
    if (show_time_mode) {
        snprintf(weight_text, sizeof(weight_text), "%.1fs", weight);
    } else {
        snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    }
    lv_label_set_text(weight_label, weight_text);
}

void EditScreen::set_time_mode(bool time_mode) {
    show_time_mode = time_mode;
}
