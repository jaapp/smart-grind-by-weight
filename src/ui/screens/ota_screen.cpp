#include "ota_screen.h"
#include "arduino_compat.h"
#include "../../config/constants.h"

void OTAScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 20, 0);

    // Disable touch input for the entire screen to lock it during operations
    lv_obj_add_flag(screen, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    // Use flex layout for centering
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(screen, 30, 0);

    // Title label
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Updating");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);

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
    lv_obj_clear_flag(progress_arc, LV_OBJ_FLAG_CLICKABLE);

    // Percentage label (inside arc)
    percentage_label = lv_label_create(progress_arc);
    lv_label_set_text(percentage_label, "0%");
    lv_obj_set_style_text_font(percentage_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(percentage_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(percentage_label);
    
    // Status label below the arc
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Receiving update....");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status_label, LV_PCT(100));
    
    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void OTAScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void OTAScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void OTAScreen::update_progress(int percent) {
    lv_arc_set_value(progress_arc, percent);
    
    char percentage_text[8];
    snprintf(percentage_text, sizeof(percentage_text), "%d%%", percent);
    lv_label_set_text(percentage_label, percentage_text);
}

void OTAScreen::update_status(const char* status) {
    lv_label_set_text(status_label, status);
}

void OTAScreen::update_title(const char* title) {
    lv_label_set_text(title_label, title);
}

void OTAScreen::show_ota_mode() {
    // Ensure screen size is properly set
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    
    update_title("Updating");
    update_status("Receiving update....");
    update_progress(0);
    show();
}

void OTAScreen::show_data_export_mode() {
    // Ensure screen size matches OTA mode exactly
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    
    update_title("Exporting Data");
    update_status("Preparing export...");
    update_progress(0);
    show();
}