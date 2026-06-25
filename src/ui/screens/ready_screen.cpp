#include "ready_screen.h"
#include "arduino_compat.h"
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
#include "../ui_helpers.h"

void ReadyScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

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
    scale_tab = lv_tabview_add_tab(tabview, "Scale");

    // Default weights
    float default_weights[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G, USER_DOUBLE_ESPRESSO_WEIGHT_G, USER_CUSTOM_PROFILE_WEIGHT_G};
    const char* names[3] = {"SINGLE", "DOUBLE", "CUSTOM"};
    
    for (int i = 0; i < 3; i++) {
        create_profile_page(profile_tabs[i], i, names[i], default_weights[i]);
    }

    // Create menu tab page
    create_menu_page(menu_tab);

    // Create scale tab page (live weight + manual grind)
    create_scale_page(scale_tab);

    // iOS-style page-indicator dots across the bottom
    create_page_dots();

    update_profile_values(default_weights, GrindMode::WEIGHT);

    visible = false;
}

void ReadyScreen::create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 0, 0);

    lv_obj_t* name_label;
    (void)create_profile_label(parent, &name_label, &weight_labels[profile_index]);
    lv_label_set_text(name_label, profile_name);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(weight_labels[profile_index], weight_text);
    lv_obj_add_flag(weight_labels[profile_index], LV_OBJ_FLAG_CLICKABLE);
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

// Circular action button matching the main grind/pulse buttons
static lv_obj_t* create_round_button(lv_obj_t* parent, const char* text, uint32_t color_hex) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 100, 100);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color_hex), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(label);
    return btn;
}

void ReadyScreen::create_scale_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 10, 0);
    lv_obj_set_style_pad_bottom(parent, 16, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Top spacer centres the weight block (mirrors the profile/custom screen)
    lv_obj_t* top_spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(top_spacer);
    lv_obj_set_width(top_spacer, LV_PCT(100));
    lv_obj_set_flex_grow(top_spacer, 1);

    lv_obj_t* subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "Live weight");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);

    scale_weight_label = lv_label_create(parent);
    lv_label_set_text(scale_weight_label, "0.0g");
    lv_obj_set_style_text_font(scale_weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(scale_weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(scale_weight_label, LV_TEXT_ALIGN_CENTER, 0);

    // Bottom spacer pins the button row near the bottom of the page
    lv_obj_t* bottom_spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(bottom_spacer);
    lv_obj_set_width(bottom_spacer, LV_PCT(100));
    lv_obj_set_flex_grow(bottom_spacer, 1);

    // Two round buttons side by side: TARE (zero) + GRIND (hold-to-run)
    lv_obj_t* button_row = lv_obj_create(parent);
    lv_obj_remove_style_all(button_row);
    lv_obj_set_size(button_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(button_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(button_row, 20, 0);

    using ET = EventBridgeLVGL::EventType;

    scale_tare_button = create_round_button(button_row, "TARE", THEME_COLOR_NEUTRAL);
    lv_obj_add_event_cb(scale_tare_button, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(ET::SCALE_TARE)));

    scale_grind_button = create_round_button(button_row, "GRIND", THEME_COLOR_PRIMARY);
    // LV_EVENT_ALL: the handler distinguishes PRESSED / RELEASED / PRESS_LOST for hold-to-grind
    lv_obj_add_event_cb(scale_grind_button, EventBridgeLVGL::dispatch_event, LV_EVENT_ALL,
                        reinterpret_cast<void*>(static_cast<intptr_t>(ET::SCALE_GRIND)));
}

void ReadyScreen::create_page_dots() {
    page_dots_container = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(page_dots_container);
    lv_obj_set_size(page_dots_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // Lowest element on the screen, mirroring the status indicators at the top
    lv_obj_align(page_dots_container, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_clear_flag(page_dots_container, LV_OBJ_FLAG_SCROLLABLE);
    // Opaque (black) background so a repaint fully clears the strip - prevents
    // stale-pixel artifacts from the dots floating over the animating tabview.
    // Invisible against the black screen background.
    lv_obj_set_style_bg_opa(page_dots_container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(page_dots_container, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_pad_all(page_dots_container, 4, 0);
    lv_obj_set_layout(page_dots_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(page_dots_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page_dots_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(page_dots_container, 10, 0);

    for (int i = 0; i < kTabCount; i++) {
        lv_obj_t* dot = lv_obj_create(page_dots_container);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        page_dots[i] = dot;
    }
    update_page_dots(0);
}

void ReadyScreen::update_page_dots(int active_index) {
    for (int i = 0; i < kTabCount; i++) {
        if (!page_dots[i]) {
            continue;
        }
        bool active = (i == active_index);
        lv_obj_set_style_bg_color(page_dots[i],
                                  lv_color_hex(active ? THEME_COLOR_TEXT_PRIMARY : THEME_COLOR_NEUTRAL), 0);
        lv_obj_set_style_bg_opa(page_dots[i], active ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_invalidate(page_dots[i]);
    }
    // Repaint the whole row (overlay on the active screen) so the edge dots don't
    // leave stale pixels when the tabview animates underneath them.
    if (page_dots_container) {
        lv_obj_invalidate(page_dots_container);
    }
}

void ReadyScreen::update_scale_weight(float weight) {
    if (!scale_weight_label) {
        return;
    }
    char buffer[24];
    snprintf(buffer, sizeof(buffer), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(scale_weight_label, buffer);
}

void ReadyScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    // Page dots live on the active screen (overlay), so toggle them with the home screen
    if (page_dots_container) {
        lv_obj_clear_flag(page_dots_container, LV_OBJ_FLAG_HIDDEN);
    }
    visible = true;
}

void ReadyScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    if (page_dots_container) {
        lv_obj_add_flag(page_dots_container, LV_OBJ_FLAG_HIDDEN);
    }
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
    if (tab >= 0 && tab < kTabCount) {
        lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
        update_page_dots(tab);
    }
}

void ReadyScreen::set_profile_long_press_handler(lv_event_cb_t handler) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            lv_obj_add_event_cb(weight_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
    }
}
