#include "ready_screen.h"
#include <Arduino.h>
#include <cmath>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../ui_helpers.h"

void ReadyScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Create tabview
    tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tabview, LV_ALIGN_CENTER, 0, 0);

    // Hide tab buttons for swipe-only interface
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);

    // Transparent background
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    // Add mode tabs
    mode_tabs[TAB_MANUAL] = lv_tabview_add_tab(tabview, "Manual");
    mode_tabs[TAB_TIME] = lv_tabview_add_tab(tabview, "Time");
    mode_tabs[TAB_WEIGHT] = lv_tabview_add_tab(tabview, "Weight");
    menu_tab = lv_tabview_add_tab(tabview, "MENU");
    mode_tabs[TAB_MENU] = menu_tab;

    create_manual_page(mode_tabs[TAB_MANUAL]);
    create_target_page(mode_tabs[TAB_TIME], GrindMode::TIME);
    create_target_page(mode_tabs[TAB_WEIGHT], GrindMode::WEIGHT);
    create_menu_page(menu_tab);

    visible = false;
    manual_tare_pulse_active = false;
}

void ReadyScreen::create_target_page(lv_obj_t* parent, GrindMode mode) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 0, 0);

    const GrindModeTraits& traits = get_grind_mode_traits(mode);
    int index = static_cast<int>(mode);

    lv_obj_t* name_label;
    (void)create_profile_label(parent, &name_label, &target_labels[index]);
    lv_label_set_text(name_label, traits.upper_name);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICKABLE);

    char value_text[24];
    float default_value = (mode == GrindMode::TIME) ? USER_DEFAULT_TARGET_TIME_S : USER_DEFAULT_TARGET_WEIGHT_G;
    format_ready_value(value_text, sizeof(value_text), mode, default_value);
    lv_label_set_text(target_labels[index], value_text);
    lv_obj_add_flag(target_labels[index], LV_OBJ_FLAG_CLICKABLE);
}

void ReadyScreen::create_manual_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 4, 0);

    lv_obj_t* name_label = lv_label_create(parent);
    lv_label_set_text(name_label, "MANUAL");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    manual_time_label = lv_label_create(parent);
    lv_label_set_text(manual_time_label, "0.0s");
    lv_obj_set_style_text_font(manual_time_label, &lv_font_montserrat_60, 0);
    lv_obj_set_style_text_color(manual_time_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_add_flag(manual_time_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(manual_time_label, 20);

    manual_weight_label = lv_label_create(parent);
    lv_label_set_text(manual_weight_label, "0.0g");
    lv_obj_set_style_text_font(manual_weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(manual_weight_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_add_flag(manual_weight_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(manual_weight_label, 20);
}

void ReadyScreen::create_menu_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 20, 0);

    // Info label
    lv_obj_t* info_label = lv_label_create(parent);
    lv_label_set_text(info_label, "MAIN\nMENU");
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
}

void ReadyScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void ReadyScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void ReadyScreen::update_target_values(float weight_g, float time_s) {
    char text[24];
    lv_obj_t* weight_label = target_labels[static_cast<int>(GrindMode::WEIGHT)];
    if (weight_label) {
        format_ready_value(text, sizeof(text), GrindMode::WEIGHT, weight_g);
        lv_label_set_text(weight_label, text);
    }
    lv_obj_t* time_label = target_labels[static_cast<int>(GrindMode::TIME)];
    if (time_label) {
        format_ready_value(text, sizeof(text), GrindMode::TIME, time_s);
        lv_label_set_text(time_label, text);
    }
}

void ReadyScreen::update_manual_time(float elapsed_s) {
    if (!manual_time_label) {
        return;
    }
    char text[16];
    snprintf(text, sizeof(text), "%.1fs", elapsed_s);
    if (strcmp(lv_label_get_text(manual_time_label), text) != 0) {
        lv_label_set_text(manual_time_label, text);
    }
}

void ReadyScreen::update_manual_weight(float weight_g) {
    if (!manual_weight_label) {
        return;
    }
    if (manual_tare_pulse_active) {
        lv_obj_set_style_text_color(manual_weight_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        manual_tare_pulse_active = false;
    }
    char text[24];
    snprintf(text, sizeof(text), SYS_WEIGHT_DISPLAY_FORMAT, weight_g);
    if (strcmp(lv_label_get_text(manual_weight_label), text) != 0) {
        lv_label_set_text(manual_weight_label, text);
    }
}

void ReadyScreen::show_manual_tare_pulse(uint32_t now_ms) {
    if (!manual_weight_label) {
        return;
    }
    manual_tare_pulse_active = true;
    if (strcmp(lv_label_get_text(manual_weight_label), "TARE") != 0) {
        lv_label_set_text(manual_weight_label, "TARE");
    }
    float s = 0.5f + 0.5f * sinf(now_ms * (6.2831853f / 1000.0f));
    lv_color_t color = lv_color_mix(lv_color_hex(THEME_COLOR_TEXT_PRIMARY),
                                    lv_color_hex(THEME_COLOR_NEUTRAL),
                                    static_cast<uint8_t>(s * 255.0f));
    lv_obj_set_style_text_color(manual_weight_label, color, 0);
}

void ReadyScreen::reset_manual_readouts() {
    update_manual_time(0.0f);
    update_manual_weight(0.0f);
}

void ReadyScreen::set_active_tab(int tab) {
    if (tab >= 0 && tab < 4) {
        lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
    }
}

void ReadyScreen::set_target_long_press_handler(lv_event_cb_t handler) {
    for (int i = 0; i < 2; i++) {
        if (target_labels[i]) {
            lv_obj_add_event_cb(target_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
    }
}
