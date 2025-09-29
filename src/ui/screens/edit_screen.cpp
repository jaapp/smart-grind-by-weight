#include "edit_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../ui_helpers.h"

void EditScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 6, 0);
    lv_obj_set_style_pad_hor(screen, 0, 0);

    // Set up flex layout (column)
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);


    create_dual_button_row(screen, &save_btn, &cancel_btn, LV_SYMBOL_OK, LV_SYMBOL_CLOSE, lv_color_hex(THEME_COLOR_SUCCESS), lv_color_hex(THEME_COLOR_NEUTRAL), 80, &lv_font_montserrat_32);

    create_profile_label(screen, &profile_label, &weight_label);

    create_dual_button_row(screen, &minus_btn, &plus_btn, LV_SYMBOL_MINUS, LV_SYMBOL_PLUS, lv_color_hex(THEME_COLOR_PRIMARY), lv_color_hex(THEME_COLOR_PRIMARY), 100, &lv_font_montserrat_32);

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
