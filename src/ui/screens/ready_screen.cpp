#include "ready_screen.h"
#include <Arduino.h>
#include <cstdint>
#include "../../config/constants.h"
#include "../../controllers/bean_controller.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
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
    for (int i = 0; i < 3; ++i) {
        weight_labels[i] = nullptr;
        advanced_profile_cells[i] = nullptr;
        advanced_profile_name_labels[i] = nullptr;
        advanced_profile_value_labels[i] = nullptr;
    }
    advanced_panel = nullptr;
    bean_card = nullptr;
    bean_name_label = nullptr;
    bean_roaster_label = nullptr;
    bean_mahlgrad_label = nullptr;
    bean_usage_label = nullptr;
    bean_usage_bar = nullptr;
    wifi_footer_label = nullptr;
    bluetooth_footer_label = nullptr;
    current_profile_index = BeanController::kDoubleProfileIndex;
    advanced_ui_enabled = USER_READY_UI_ADVANCED_DEFAULT;

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
    create_advanced_page(screen);

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

    wifi_footer_label = lv_label_create(lv_scr_act());
    lv_label_set_text(wifi_footer_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_footer_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_footer_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_opa(wifi_footer_label, LV_OPA_60, 0);
    lv_obj_align(wifi_footer_label, LV_ALIGN_BOTTOM_LEFT, 16, -20);
    lv_obj_add_flag(wifi_footer_label, LV_OBJ_FLAG_HIDDEN);

    bluetooth_footer_label = lv_label_create(lv_scr_act());
    lv_label_set_text(bluetooth_footer_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bluetooth_footer_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bluetooth_footer_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_opa(bluetooth_footer_label, LV_OPA_60, 0);
    lv_obj_align(bluetooth_footer_label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_add_flag(bluetooth_footer_label, LV_OBJ_FLAG_HIDDEN);

    update_profile_values(default_weights, GrindMode::WEIGHT);
    set_active_tab(BeanController::kDoubleProfileIndex);
    set_advanced_ui_enabled(USER_READY_UI_ADVANCED_DEFAULT);

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

void ReadyScreen::create_advanced_page(lv_obj_t* parent) {
    advanced_panel = lv_obj_create(parent);
    lv_obj_set_size(advanced_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_align(advanced_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(advanced_panel, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(advanced_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(advanced_panel, 0, 0);
    lv_obj_set_style_pad_all(advanced_panel, 0, 0);
    lv_obj_clear_flag(advanced_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(advanced_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);

    bean_card = lv_btn_create(advanced_panel);
    lv_obj_set_size(bean_card, 260, 72);
    lv_obj_align(bean_card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(bean_card, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(bean_card, 1, 0);
    lv_obj_set_style_border_color(bean_card, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(bean_card, 4, 0);
    lv_obj_set_style_pad_all(bean_card, 0, 0);
    lv_obj_clear_flag(bean_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* change_label = lv_label_create(bean_card);
    lv_label_set_text(change_label, "Change");
    lv_obj_set_style_text_font(change_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(change_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(change_label, LV_ALIGN_TOP_RIGHT, -30, 44);

    bean_name_label = lv_label_create(bean_card);
    lv_label_set_text(bean_name_label, "No bean selected");
    lv_obj_set_size(bean_name_label, 196, 30);
    lv_label_set_long_mode(bean_name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(bean_name_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(bean_name_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(bean_name_label, LV_ALIGN_TOP_LEFT, 12, 10);

    bean_roaster_label = lv_label_create(bean_card);
    lv_label_set_text(bean_roaster_label, "Tap to choose");
    lv_obj_set_size(bean_roaster_label, 130, 18);
    lv_label_set_long_mode(bean_roaster_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(bean_roaster_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bean_roaster_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(bean_roaster_label, LV_ALIGN_TOP_LEFT, 12, 44);

    lv_obj_t* chevron = lv_label_create(bean_card);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chevron, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(chevron, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t* gs_title = lv_label_create(advanced_panel);
    lv_label_set_text(gs_title, "GRIND SIZE");
    lv_obj_set_width(gs_title, 240);
    lv_obj_set_style_text_align(gs_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(gs_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gs_title, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(gs_title, LV_ALIGN_TOP_MID, 0, 86);

    lv_obj_t* gs_box = lv_obj_create(advanced_panel);
    lv_obj_set_size(gs_box, 210, 96);
    lv_obj_align(gs_box, LV_ALIGN_TOP_MID, 0, 106);
    lv_obj_set_style_bg_color(gs_box, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(gs_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(gs_box, 2, 0);
    lv_obj_set_style_border_width(gs_box, 0, 0);
    lv_obj_set_style_pad_all(gs_box, 0, 0);
    lv_obj_clear_flag(gs_box, LV_OBJ_FLAG_SCROLLABLE);

    bean_mahlgrad_label = lv_label_create(gs_box);
    lv_label_set_text(bean_mahlgrad_label, "--");
    lv_obj_set_width(bean_mahlgrad_label, 204);
    lv_label_set_long_mode(bean_mahlgrad_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(bean_mahlgrad_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(bean_mahlgrad_label, &lv_font_montserrat_60, 0);
    lv_obj_set_style_text_color(bean_mahlgrad_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(bean_mahlgrad_label);

    bean_usage_label = lv_label_create(advanced_panel);
    lv_label_set_text(bean_usage_label, "Used 0.0g");
    lv_obj_set_width(bean_usage_label, 224);
    lv_label_set_long_mode(bean_usage_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(bean_usage_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(bean_usage_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bean_usage_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(bean_usage_label, LV_ALIGN_TOP_MID, 0, 218);

    bean_usage_bar = lv_bar_create(advanced_panel);
    lv_obj_set_size(bean_usage_bar, 224, 6);
    lv_obj_align(bean_usage_bar, LV_ALIGN_TOP_MID, 0, 241);
    lv_bar_set_range(bean_usage_bar, 0, 1000);
    lv_bar_set_value(bean_usage_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bean_usage_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bean_usage_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bean_usage_bar, lv_color_hex(THEME_COLOR_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bean_usage_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bean_usage_bar, 0, LV_PART_INDICATOR);

    lv_obj_t* profile_row = lv_obj_create(advanced_panel);
    lv_obj_set_size(profile_row, 280, 78);
    lv_obj_align(profile_row, LV_ALIGN_TOP_MID, 0, 258);
    lv_obj_set_style_bg_opa(profile_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(profile_row, 0, 0);
    lv_obj_set_style_pad_all(profile_row, 0, 0);
    lv_obj_clear_flag(profile_row, LV_OBJ_FLAG_SCROLLABLE);

    const char* cell_names[3] = {"Single", "Double", "Custom"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* cell = lv_btn_create(profile_row);
        advanced_profile_cells[i] = cell;
        lv_obj_set_size(cell, i == 1 ? 94 : 93, 78);
        lv_obj_set_pos(cell, i == 0 ? 0 : (i == 1 ? 93 : 187), 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x151515), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_radius(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(cell, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(cell, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::PROFILE_SELECT)));

        lv_obj_t* name_label = lv_label_create(cell);
        advanced_profile_name_labels[i] = name_label;
        lv_label_set_text(name_label, cell_names[i]);
        lv_obj_set_width(name_label, 88);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 14);

        lv_obj_t* value_label = lv_label_create(cell);
        advanced_profile_value_labels[i] = value_label;
        lv_obj_set_width(value_label, 88);
        lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(value_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_obj_align(value_label, LV_ALIGN_TOP_MID, 0, 44);
        lv_label_set_text(value_label, "-");
    }
    refresh_profile_selection();
    lv_obj_add_flag(advanced_panel, LV_OBJ_FLAG_HIDDEN);
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
    sync_advanced_visibility();
    if (wifi_footer_label) {
        lv_obj_clear_flag(wifi_footer_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (bluetooth_footer_label) {
        lv_obj_clear_flag(bluetooth_footer_label, LV_OBJ_FLAG_HIDDEN);
    }
    visible = true;
}

void ReadyScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    if (wifi_footer_label) {
        lv_obj_add_flag(wifi_footer_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (bluetooth_footer_label) {
        lv_obj_add_flag(bluetooth_footer_label, LV_OBJ_FLAG_HIDDEN);
    }
    clear_status();
    visible = false;
}

void ReadyScreen::update_profile_values(const float values[3], GrindMode mode) {
    for (int i = 0; i < 3; i++) {
        char text[24];
        format_ready_value(text, sizeof(text), mode, values[i]);
        if (weight_labels[i]) {
            lv_label_set_text(weight_labels[i], text);
        }
        if (advanced_profile_value_labels[i]) {
            lv_label_set_text(advanced_profile_value_labels[i], text);
        }
    }
}

void ReadyScreen::update_bean_summary(bool has_active, const char* name, const char* roaster,
                                      uint16_t mahlgrad_x2, uint32_t dose_used_x10,
                                      uint32_t purge_used_x10, uint16_t bag_size_g) {
    if (!bean_name_label || !bean_roaster_label || !bean_mahlgrad_label || !bean_usage_label) {
        return;
    }

    if (!has_active) {
        lv_label_set_text(bean_name_label, "No bean selected");
        lv_label_set_text(bean_roaster_label, "Tap to choose");
        lv_label_set_text(bean_mahlgrad_label, "--");
        lv_label_set_text(bean_usage_label, "Add beans in the web UI");
        if (bean_usage_bar) {
            lv_bar_set_value(bean_usage_bar, 0, LV_ANIM_OFF);
        }
        return;
    }

    lv_label_set_text(bean_name_label, name && name[0] ? name : "Unnamed bean");
    lv_label_set_text(bean_roaster_label, roaster && roaster[0] ? roaster : "No roaster");

    char gs_text[12];
    BeanController::format_mahlgrad(gs_text, sizeof(gs_text), mahlgrad_x2);
    lv_label_set_text(bean_mahlgrad_label, gs_text);

    const float dose_g = static_cast<float>(dose_used_x10) / 10.0f;
    const float purge_g = static_cast<float>(purge_used_x10) / 10.0f;
    const float total_g = dose_g + purge_g;
    char usage_text[64];
    if (bag_size_g > 0) {
        snprintf(usage_text, sizeof(usage_text), "Used %.1fg of %ug", total_g, static_cast<unsigned>(bag_size_g));
    } else if (purge_used_x10 > 0) {
        snprintf(usage_text, sizeof(usage_text), "%.1fg dose + %.1fg purge", dose_g, purge_g);
    } else {
        snprintf(usage_text, sizeof(usage_text), "%.1fg used", dose_g);
    }
    lv_label_set_text(bean_usage_label, usage_text);

    if (bean_usage_bar) {
        uint32_t progress = 0;
        if (bag_size_g > 0) {
            const uint32_t total_used_x10 = dose_used_x10 + purge_used_x10;
            const uint32_t bag_x10 = static_cast<uint32_t>(bag_size_g) * 10U;
            progress = bag_x10 > 0 ? (total_used_x10 * 1000U) / bag_x10 : 0;
            if (progress > 1000U) {
                progress = 1000U;
            }
        }
        lv_bar_set_value(bean_usage_bar, static_cast<int32_t>(progress), LV_ANIM_OFF);
    }
}

void ReadyScreen::set_active_tab(int tab) {
    if (advanced_ui_enabled && tab >= USER_PROFILE_COUNT) {
        tab = (current_profile_index >= 0 && current_profile_index < USER_PROFILE_COUNT)
                  ? current_profile_index
                  : BeanController::kDoubleProfileIndex;
    }
    if (tab >= 0 && tab < 4) {
        current_profile_index = tab < USER_PROFILE_COUNT ? tab : current_profile_index;
        if (tabview && static_cast<int>(lv_tabview_get_tab_act(tabview)) != tab) {
            lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
        }
        refresh_profile_selection();
        sync_advanced_visibility();
    }
}

void ReadyScreen::set_advanced_ui_enabled(bool enabled) {
    advanced_ui_enabled = enabled;
    sync_advanced_visibility();
}

void ReadyScreen::refresh_profile_selection() {
    for (int i = 0; i < 3; ++i) {
        const bool active = i == current_profile_index;
        if (advanced_profile_cells[i]) {
            lv_obj_set_style_bg_color(advanced_profile_cells[i],
                                      lv_color_hex(active ? 0x262626 : 0x151515),
                                      0);
        }
        if (advanced_profile_name_labels[i]) {
            lv_obj_set_style_text_color(advanced_profile_name_labels[i],
                                        lv_color_hex(active ? THEME_COLOR_TEXT_PRIMARY
                                                            : THEME_COLOR_TEXT_SECONDARY),
                                        0);
        }
        if (advanced_profile_value_labels[i]) {
            lv_obj_set_style_text_color(advanced_profile_value_labels[i],
                                        lv_color_hex(active ? THEME_COLOR_ACCENT
                                                            : THEME_COLOR_TEXT_SECONDARY),
                                        0);
        }
    }
}

void ReadyScreen::sync_advanced_visibility() {
    if (!advanced_panel) {
        return;
    }

    const int active_tab = tabview ? static_cast<int>(lv_tabview_get_tab_act(tabview)) : current_profile_index;
    const bool show_advanced = advanced_ui_enabled && active_tab < 3;
    if (show_advanced) {
        lv_obj_clear_flag(advanced_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(advanced_panel);
    } else {
        lv_obj_add_flag(advanced_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ReadyScreen::set_profile_long_press_handler(lv_event_cb_t handler) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            lv_obj_add_event_cb(weight_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
        if (advanced_profile_cells[i]) {
            lv_obj_add_event_cb(advanced_profile_cells[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
        if (advanced_profile_value_labels[i]) {
            lv_obj_add_event_cb(advanced_profile_value_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
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
