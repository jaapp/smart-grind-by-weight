#include "ota_update_failed_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"

void OtaUpdateFailedScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 20, 0);

    // Use flex layout for centering
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(screen, 20, 0);

    // Title label - warning icon and text
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Update Failed");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_set_width(title_label, LV_PCT(90));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);

    // Main message
    message_label = lv_label_create(screen);
    lv_label_set_text(message_label, "The firmware update failed.\nThe device is still running the previous version.");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(message_label, LV_PCT(90));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    // Details label (build numbers)
    details_label = lv_label_create(screen);
    lv_obj_set_style_text_font(details_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(details_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(details_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(details_label, LV_PCT(90));
    lv_label_set_long_mode(details_label, LV_LABEL_LONG_WRAP);

    // OK button
    ok_button = lv_btn_create(screen);
    lv_obj_set_size(ok_button, OTA_WARNING_BUTTON_WIDTH, OTA_WARNING_BUTTON_HEIGHT);
    lv_obj_set_style_bg_color(ok_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(ok_button, 0, 0);
    lv_obj_set_style_radius(ok_button, 12, 0);
    
    ok_button_label = lv_label_create(ok_button);
    lv_label_set_text(ok_button_label, "OK");
    lv_obj_set_style_text_font(ok_button_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ok_button_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(ok_button_label);

    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void OtaUpdateFailedScreen::show(const char* expected_build) {
    // Update details with build information
    char details_text[128];
    snprintf(details_text, sizeof(details_text), 
             "Expected: Build #%s\nCurrent: Build #%d", 
             expected_build, BUILD_NUMBER);
    lv_label_set_text(details_label, details_text);

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void OtaUpdateFailedScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}