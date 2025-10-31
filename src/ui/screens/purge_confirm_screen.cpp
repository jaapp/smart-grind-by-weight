#include "purge_confirm_screen.h"
#include <Arduino.h>
#include "../ui_helpers.h"

void PurgeConfirmScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    // Set width to 100% and height to exclude bottom 120px button area
    // This prevents the screen from blocking touch events to the buttons
    lv_obj_set_width(screen, LV_PCT(100));
    lv_obj_set_height(screen, lv_display_get_vertical_resolution(lv_display_get_default()) - 120);
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_top(screen, 6, 0);
    lv_obj_set_style_pad_bottom(screen, 20, 0);  // Reduced padding since screen no longer covers buttons
    lv_obj_set_style_pad_left(screen, 0, 0);
    lv_obj_set_style_pad_right(screen, 0, 0);
    lv_obj_set_style_pad_gap(screen, 5, 0);

    // Set up flex layout (column)
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title label
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Grinder Purged");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_flex_grow(title_label, 0);

    // Message container
    lv_obj_t* message_container = lv_obj_create(screen);
    lv_obj_set_width(message_container, LV_PCT(100));
    lv_obj_set_flex_grow(message_container, 1);
    lv_obj_set_style_bg_opa(message_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(message_container, 0, 0);
    lv_obj_set_layout(message_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(message_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(message_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(message_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(message_container, LV_DIR_VER);

    // Message label
    message_label = lv_label_create(message_container);
    lv_label_set_text(message_label, "Remove the purge grinds if desired.");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(message_label, LV_PCT(90));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    lv_obj_update_layout(message_container);
    lv_obj_scroll_to_y(message_container, 0, LV_ANIM_OFF);

    // Checkbox with label - 2x normal size
    checkbox = lv_checkbox_create(screen);
    lv_checkbox_set_text(checkbox, "Always keep");
    lv_obj_set_style_text_font(checkbox, &lv_font_montserrat_32, 0);  // Larger font
    lv_obj_set_style_text_color(checkbox, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(checkbox, 260);  // Set max width to prevent overflow

    // Scale up the checkbox indicator to 2x size
    lv_obj_set_style_transform_scale(checkbox, 200, LV_PART_INDICATOR);  // 200 = 2.0x scale

    // NOTE: No buttons created here - reuse existing grind_button_ (CANCEL) and pulse_button_ (OK)
    // positioned by GrindingUIController::update_button_layout() when in PURGE_CONFIRM phase

    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void PurgeConfirmScreen::show() {
    if (checkbox) {
        // Always default to unchecked to prevent accidental preference changes
        lv_obj_clear_state(checkbox, LV_STATE_CHECKED);
    }
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(screen);  // Bring to front, above all other UI elements
    visible = true;
}

void PurgeConfirmScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

bool PurgeConfirmScreen::is_checkbox_checked() const {
    if (!checkbox) return false;
    return lv_obj_has_state(checkbox, LV_STATE_CHECKED);
}
