#include "confirm_screen.h"
#include <Arduino.h>
#include "ui_helpers.h"

void ConfirmScreen::create() {
      screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 6, 0);
    lv_obj_set_style_pad_hor(screen, 0, 0);
    lv_obj_set_style_pad_gap(screen, 5, 0);
    
    // Set up flex layout (column)
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Title label
    title_label = lv_label_create(screen);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title_label, LV_PCT(100));
    // Title takes only the space it needs
    lv_obj_set_flex_grow(title_label, 0);
    
    lv_obj_t *message_container = lv_obj_create(screen);
    lv_obj_set_width(message_container, LV_PCT(100));
    lv_obj_set_flex_grow(message_container, 1);
    lv_obj_set_style_bg_opa(message_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(message_container, 0, 0);

    // Set up container to center content
    lv_obj_set_layout(message_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(message_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(message_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(message_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(message_container, LV_DIR_VER);

    // Create the actual message label inside the container
    message_label = lv_label_create(message_container);
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(message_label, LV_PCT(100));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    lv_obj_update_layout(message_container);
    lv_obj_scroll_to_y(message_container, 0, LV_ANIM_OFF);  // Scroll to top

    create_dual_button_row(screen, &confirm_button, &cancel_button, "Confirm", "Cancel", lv_color_hex(THEME_COLOR_SUCCESS));
    confirm_button_label = lv_obj_get_child(confirm_button, -1);
    cancel_button_label = lv_obj_get_child(cancel_button, -1);
    
    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void ConfirmScreen::show(const char* title, const char* message, 
                        const char* confirm_text, lv_color_t confirm_color,
                        const char* cancel_text) {
    // Set title and color
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, confirm_color, 0);
    
    // Set message
    lv_label_set_text(message_label, message);
    
    // Set confirm button
    lv_label_set_text(confirm_button_label, confirm_text);
    lv_obj_set_style_bg_color(confirm_button, confirm_color, 0);
    
    // Set cancel button
    lv_label_set_text(cancel_button_label, cancel_text);
    
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void ConfirmScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void ConfirmScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}