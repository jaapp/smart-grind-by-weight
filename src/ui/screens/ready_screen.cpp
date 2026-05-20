#include "ready_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../ui_helpers.h"

namespace {

lv_obj_t* create_icon_part(lv_obj_t* parent, int x, int y, int width, int height,
                           lv_color_t color, int radius) {
    lv_obj_t* part = lv_obj_create(parent);
    lv_obj_set_size(part, width, height);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_style_bg_color(part, color, 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(part, 0, 0);
    lv_obj_set_style_pad_all(part, 0, 0);
    lv_obj_set_style_radius(part, radius, 0);
    lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    return part;
}

void create_steam(lv_obj_t* parent, int x, int y, int height, lv_color_t color) {
    lv_obj_t* steam = create_icon_part(parent, x, y, 7, height, color, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(steam, LV_OPA_50, 0);
}

void create_dot(lv_obj_t* parent, int x, int y, int size, lv_color_t color) {
    lv_obj_t* dot = create_icon_part(parent, x, y, size, size, color, LV_RADIUS_CIRCLE);
    lv_obj_set_style_shadow_width(dot, 8, 0);
    lv_obj_set_style_shadow_color(dot, color, 0);
    lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
}

void create_cup(lv_obj_t* parent, int x, int y, int width, int height, lv_color_t accent) {
    lv_obj_t* saucer = create_icon_part(parent, x - 8, y + height + 8, width + 20, 7,
                                        lv_color_hex(0x4A4A4A), 4);
    lv_obj_set_style_bg_opa(saucer, LV_OPA_70, 0);

    lv_obj_t* handle = lv_obj_create(parent);
    lv_obj_set_size(handle, 20, height - 8);
    lv_obj_set_pos(handle, x + width - 5, y + 7);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 4, 0);
    lv_obj_set_style_border_color(handle, accent, 0);
    lv_obj_set_style_border_opa(handle, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* body = create_icon_part(parent, x, y, width, height, accent, 10);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(body, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(body, 14, 0);
    lv_obj_set_style_shadow_color(body, accent, 0);
    lv_obj_set_style_shadow_opa(body, LV_OPA_50, 0);

    lv_obj_t* coffee = create_icon_part(parent, x + 7, y + 7, width - 14, 7,
                                        lv_color_hex(0x201713), LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(coffee, LV_OPA_80, 0);

    lv_obj_t* shine = create_icon_part(parent, x + 9, y + 16, 6, height - 22,
                                       lv_color_hex(THEME_COLOR_TEXT_PRIMARY), LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(shine, LV_OPA_30, 0);
}

} // namespace

void ReadyScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    status_timer = nullptr;

    // Create tabview
    tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tabview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Hide tab buttons for swipe-only interface
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);

    // Transparent background
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    // Add profile tabs
    profile_tabs[0] = lv_tabview_add_tab(tabview, "Single");
    profile_tabs[1] = lv_tabview_add_tab(tabview, "Double");
    profile_tabs[2] = lv_tabview_add_tab(tabview, "Custom");
    menu_tab = lv_tabview_add_tab(tabview, "MENU");
    profile_tabs[3] = menu_tab;

    // Default weights
    float default_weights[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G, USER_DOUBLE_ESPRESSO_WEIGHT_G, USER_CUSTOM_PROFILE_WEIGHT_G};
    const char* names[3] = {"SINGLE", "DOUBLE", "CUSTOM"};
    
    for (int i = 0; i < 3; i++) {
        create_profile_page(profile_tabs[i], i, names[i], default_weights[i]);
    }

    // Create menu tab page
    create_menu_page(menu_tab);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "");
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, 260);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(status_label);

    update_profile_values(default_weights, GrindMode::WEIGHT);

    visible = false;
}

void ReadyScreen::create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    create_dose_icon(parent, profile_index);

    lv_obj_t* name_label;
    (void)create_profile_label(parent, &name_label, &weight_labels[profile_index]);
    lv_label_set_text(name_label, profile_name);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(weight_labels[profile_index], weight_text);
    lv_obj_add_flag(weight_labels[profile_index], LV_OBJ_FLAG_CLICKABLE);
}

void ReadyScreen::create_dose_icon(lv_obj_t* parent, int profile_index) {
    lv_obj_t* icon = lv_obj_create(parent);
    lv_obj_set_size(icon, 132, 80);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    switch (profile_index) {
        case 0:
            create_steam(icon, 62, 8, 18, lv_color_hex(THEME_COLOR_ACCENT));
            create_cup(icon, 42, 30, 48, 32, lv_color_hex(THEME_COLOR_ACCENT));
            break;

        case 1:
            create_steam(icon, 43, 10, 15, lv_color_hex(THEME_COLOR_PRIMARY));
            create_steam(icon, 82, 10, 15, lv_color_hex(THEME_COLOR_WARNING));
            create_cup(icon, 25, 34, 38, 27, lv_color_hex(THEME_COLOR_PRIMARY));
            create_cup(icon, 68, 34, 38, 27, lv_color_hex(THEME_COLOR_WARNING));
            break;

        default:
            create_dot(icon, 37, 11, 11, lv_color_hex(THEME_COLOR_PRIMARY));
            create_dot(icon, 61, 6, 12, lv_color_hex(THEME_COLOR_ACCENT));
            create_dot(icon, 87, 12, 10, lv_color_hex(THEME_COLOR_SUCCESS));
            create_cup(icon, 42, 34, 48, 32, lv_color_hex(THEME_COLOR_NEUTRAL));
            create_steam(icon, 63, 18, 12, lv_color_hex(THEME_COLOR_SECONDARY));
            break;
    }
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
    clear_status();
    visible = false;
}

void ReadyScreen::update_profile_values(const float values[3], GrindMode mode) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            char text[24];
            format_ready_value(text, sizeof(text), mode, values[i]);
            lv_label_set_text(weight_labels[i], text);
        }
    }
}

void ReadyScreen::set_active_tab(int tab) {
    if (tab >= 0 && tab < 4) {
        lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
    }
}

void ReadyScreen::set_profile_long_press_handler(lv_event_cb_t handler) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            lv_obj_add_event_cb(weight_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
    }
}

void ReadyScreen::show_transient_status(const char* text, uint32_t duration_ms) {
    if (!status_label) {
        return;
    }

    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = nullptr;
    }

    lv_label_set_text(status_label, text ? text : "");
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(status_label);

    status_timer = lv_timer_create(status_timer_cb, duration_ms, this);
    if (status_timer) {
        lv_timer_set_repeat_count(status_timer, 1);
    }
}

void ReadyScreen::clear_status() {
    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = nullptr;
    }

    if (status_label) {
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ReadyScreen::status_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<ReadyScreen*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    if (self->status_label) {
        lv_obj_add_flag(self->status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (self->status_timer == timer) {
        self->status_timer = nullptr;
    }
}
