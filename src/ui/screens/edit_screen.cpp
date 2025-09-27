#include "edit_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "ui_helpers.h"

void EditScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 0, 0);
    lv_obj_set_style_pad_hor(screen, 6, 0);


    // Set up flex layout (column)
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *top_button_container = lv_obj_create(screen);
    lv_obj_set_size(top_button_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top_button_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_button_container, 0, 0);
    lv_obj_set_style_pad_all(top_button_container, 0, 0);
    
    // Set up button container as horizontal flex
    lv_obj_set_layout(top_button_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top_button_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_button_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(top_button_container, 10, 0);

    save_btn = create_button(top_button_container, LV_SYMBOL_OK, lv_color_hex(THEME_COLOR_SUCCESS));
    lv_obj_set_style_text_font(lv_obj_get_child(save_btn, -1), &lv_font_montserrat_32, 0);
    lv_obj_set_flex_grow(save_btn, 1);

    cancel_btn = create_button(top_button_container, LV_SYMBOL_CLOSE, lv_color_hex(THEME_COLOR_NEUTRAL));
    lv_obj_set_style_text_font(lv_obj_get_child(cancel_btn, -1), &lv_font_montserrat_32, 0);
    lv_obj_set_flex_grow(cancel_btn, 1);

    create_profile_label(screen, &profile_label, &weight_label);

    lv_obj_t *bottom_button_container = lv_obj_create(screen);
    lv_obj_set_size(bottom_button_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottom_button_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_button_container, 0, 0);
    lv_obj_set_style_pad_all(bottom_button_container, 0, 0);
    
    // Set up button container as horizontal flex
    lv_obj_set_layout(bottom_button_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom_button_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_button_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(bottom_button_container, 10, 0);

    minus_btn = create_button(bottom_button_container, LV_SYMBOL_MINUS, lv_color_hex(THEME_COLOR_PRIMARY), -1);
    lv_obj_set_style_text_font(lv_obj_get_child(minus_btn, -1), &lv_font_montserrat_32, 0);
    lv_obj_set_flex_grow(minus_btn, 1);

    plus_btn = create_button(bottom_button_container, LV_SYMBOL_PLUS, lv_color_hex(THEME_COLOR_PRIMARY), -1);
    lv_obj_set_style_text_font(lv_obj_get_child(plus_btn, -1), &lv_font_montserrat_32, 0);
    lv_obj_set_flex_grow(plus_btn, 1);


    visible = false;
    mode = GrindMode::WEIGHT;
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

void EditScreen::update_target(float value) {
    char text[16];
    format_ready_value(text, sizeof(text), mode, value);
    lv_label_set_text(weight_label, text);
}

void EditScreen::set_mode(GrindMode time_mode) {
    mode = time_mode;
}
