#include "grinding_screen_arc.h"
#include <Arduino.h>
#include "../../config/constants.h"

void GrindingScreenArc::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0); // Keep transparent
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 20, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE); // Make the parent screen container clickable

    // Use flex layout for centering
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(screen, 30, 0);

    // Profile name label
    profile_label = lv_label_create(screen);
    lv_label_set_text(profile_label, "DOUBLE");
    lv_obj_set_style_text_font(profile_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    // Target weight label
    target_label = lv_label_create(screen);
    lv_label_set_text(target_label, "Target: 18.0g");
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_label_set_long_mode(target_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(target_label, 200);
    lv_obj_set_style_text_align(target_label, LV_TEXT_ALIGN_CENTER, 0);

    // Progress arc
    progress_arc = lv_arc_create(screen);
    lv_obj_set_size(progress_arc, THEME_PROGRESS_ARC_DIAMETER_PX, THEME_PROGRESS_ARC_DIAMETER_PX);
    lv_arc_set_range(progress_arc, 0, 100);
    lv_arc_set_value(progress_arc, 0);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(THEME_COLOR_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(progress_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_arc, 12, LV_PART_MAIN);
    lv_obj_remove_style(progress_arc, nullptr, LV_PART_KNOB);

    // Current weight label (inside arc)
    weight_label = lv_label_create(progress_arc);
    lv_label_set_text(weight_label, "0.0g");
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(weight_label);
    
    // MODIFIED: Ensure all child widgets pass click events to the parent screen
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen); i++) {
        lv_obj_clear_flag(lv_obj_get_child(screen, i), LV_OBJ_FLAG_CLICKABLE);
    }

    visible = false;
    time_mode = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void GrindingScreenArc::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void GrindingScreenArc::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void GrindingScreenArc::update_profile_name(const char* name) {
    lv_label_set_text(profile_label, name);
}

void GrindingScreenArc::update_target_weight(float weight) {
    if (time_mode) {
        return;
    }
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "Target: " SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(target_label, target_text);
}

void GrindingScreenArc::update_target_weight_text(const char* text) {
    lv_label_set_text(target_label, text);
}

void GrindingScreenArc::update_target_time(float seconds) {
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "Time: %.1fs", seconds);
    lv_label_set_text(target_label, target_text);
}

void GrindingScreenArc::update_current_weight(float weight) {
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(weight_label, weight_text);
}

void GrindingScreenArc::update_tare_display() {
    lv_label_set_text(weight_label, "TARE");
    lv_arc_set_value(progress_arc, 0);  // Reset arc to 0 during taring
}

void GrindingScreenArc::update_progress(int percent) {
    lv_arc_set_value(progress_arc, percent);
}

void GrindingScreenArc::set_time_mode(bool enabled) {
    time_mode = enabled;
}
