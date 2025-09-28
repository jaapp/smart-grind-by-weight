#include "calibration_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "ui_helpers.h"

void CalibrationScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 6, 0);

    top_button_row = create_dual_button_row(screen, &ok_button, &cancel_button, LV_SYMBOL_OK, LV_SYMBOL_CLOSE, lv_color_hex(THEME_COLOR_SUCCESS), lv_color_hex(THEME_COLOR_NEUTRAL), 80, &lv_font_montserrat_32);

    // Title label (center top)
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "CALIBRATION");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -90);

    // Instruction label (center)
    instruction_label = lv_label_create(screen);
    lv_label_set_text(instruction_label, "Remove all weight");
    lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instruction_label, LV_ALIGN_CENTER, 0, -20);

    // Weight label (center) - current weight or calibration weight
    weight_label = lv_label_create(screen);
    lv_label_set_text(weight_label, "0");
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, 55);

    lv_obj_t* bottom_button_row = create_dual_button_row(screen, &minus_btn, &plus_btn, LV_SYMBOL_MINUS, LV_SYMBOL_PLUS, lv_color_hex(THEME_COLOR_PRIMARY), lv_color_hex(THEME_COLOR_PRIMARY), 100, &lv_font_montserrat_32);
    lv_obj_align(bottom_button_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(minus_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(plus_btn, LV_OBJ_FLAG_HIDDEN);

    // Hidden weight input (keeping for compatibility but not used in UI)
    weight_input = lv_textarea_create(screen);
    lv_obj_set_size(weight_input, 1, 1);
    lv_obj_add_flag(weight_input, LV_OBJ_FLAG_HIDDEN);

    current_step = CAL_STEP_EMPTY;
    calibration_weight = 20.0f; // Default calibration weight
    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void CalibrationScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void CalibrationScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void CalibrationScreen::set_step(CalibrationStep step) {
    current_step = step;
    
    switch (step) {
        case CAL_STEP_EMPTY:
            lv_label_set_text(instruction_label, "Remove all weight\nPress OK when empty");
            lv_obj_set_style_pad_hor(top_button_row, 0, 0);
            lv_obj_clear_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(plus_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(minus_btn, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case CAL_STEP_WEIGHT:
            lv_label_set_text(instruction_label, "Place known weight\nAdjust weight value\n with +/- buttons");
            lv_obj_clear_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(plus_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(minus_btn, LV_OBJ_FLAG_HIDDEN);
            update_calibration_weight(calibration_weight);
            break;
            
        case CAL_STEP_COMPLETE:
            lv_label_set_text(instruction_label, "Calibration complete!");
            lv_obj_set_style_pad_hor(top_button_row, 10, 0);
            lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(plus_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(minus_btn, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

void CalibrationScreen::update_current_weight(float weight) {
    // Only update current weight display when not in weight input step
    if (current_step != CAL_STEP_WEIGHT) {
        char weight_text[16];
        if (current_step == CAL_STEP_COMPLETE) {
            // Step 3: Show calibrated weight in grams
            snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
        } else {
            // Step 1: Show raw sensor values
            long raw_display = (long)weight;
            snprintf(weight_text, sizeof(weight_text), SYS_RAW_VALUE_FORMAT, raw_display);
        }
        lv_label_set_text(weight_label, weight_text);
    }
}

void CalibrationScreen::update_calibration_weight(float weight) {
    calibration_weight = weight;
    
    // Update the weight_input for compatibility
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), "%.1f", weight);
    lv_textarea_set_text(weight_input, weight_text);
    
    // Also update the main weight label if we're in the weight step
    if (current_step == CAL_STEP_WEIGHT) {
        char weight_display[16];
        snprintf(weight_display, sizeof(weight_display), SYS_WEIGHT_DISPLAY_FORMAT, weight);
        lv_label_set_text(weight_label, weight_display);
    }
}