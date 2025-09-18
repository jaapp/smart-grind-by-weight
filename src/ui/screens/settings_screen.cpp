#include "settings_screen.h"
#include <Arduino.h>
#include "../../config/build_info.h"
#include "../../config/git_info.h"
#include "../../config/constants.h"
#include "../../logging/grind_logging.h"
#include "../../hardware/hardware_manager.h"
#include "grinding_screen.h"

void SettingsScreen::create(BluetoothManager* bluetooth, GrindController* grind_ctrl, GrindingScreen* grind_screen, class HardwareManager* hw_mgr) {
    bluetooth_manager = bluetooth;
    grind_controller = grind_ctrl;
    grinding_screen = grind_screen;
    hardware_manager = hw_mgr;
    
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Create tabview for pagination
    tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(80));
    lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
    
    // Only disable vertical scrolling on tabview content, keep horizontal for page swiping
    lv_obj_t* content = lv_tabview_get_content(tabview);
    if (content) {
        lv_obj_set_scroll_dir(content, LV_DIR_HOR);  // Only allow horizontal scrolling
        lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);  // Hide scrollbars
    }

    // Hide tab buttons for swipe-only interface
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);

    // Transparent background
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    // Add tabs in order: Tools -> Info -> Settings -> Reset (Info will be default)
    tools_tab = lv_tabview_add_tab(tabview, "Tools");
    info_tab = lv_tabview_add_tab(tabview, "Info");
    settings_tab = lv_tabview_add_tab(tabview, "Settings");
    reset_tab = lv_tabview_add_tab(tabview, "Reset");

    // Create pages
    create_tools_page(tools_tab);
    create_info_page(info_tab);
    create_settings_page(settings_tab);
    create_reset_page(reset_tab);
    
    // Set Info page as default (middle tab)
    lv_tabview_set_act(tabview, 1, LV_ANIM_OFF);
    
    // Create taring overlay (initially hidden)
    taring_overlay = lv_obj_create(screen);
    lv_obj_set_size(taring_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_align(taring_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(taring_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(taring_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(taring_overlay, 0, 0);
    lv_obj_set_style_pad_all(taring_overlay, 0, 0);
    
    // Taring label
    taring_label = lv_label_create(taring_overlay);
    lv_label_set_text(taring_label, "TARING...\nPlease wait");
    lv_obj_set_style_text_font(taring_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(taring_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(taring_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(taring_label, LV_ALIGN_CENTER, 0, 0);
    
    // Hide the overlay initially
    lv_obj_add_flag(taring_overlay, LV_OBJ_FLAG_HIDDEN);
    
    // Create back button (common to all pages)
    create_back_button();

    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::create_info_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 15, 0);
    lv_obj_set_style_pad_top(parent, 20, 0);
    
    // Enable vertical scrolling for the info page content
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "System Info");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    // Load cell info
    info_label = lv_label_create(parent);
    char initial_loadcell_text[32];
    snprintf(initial_loadcell_text, sizeof(initial_loadcell_text), "Load Cell: " SYS_WEIGHT_DISPLAY_FORMAT, 0.0f);
    lv_label_set_text(info_label, initial_loadcell_text);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(info_label, 280);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_LEFT, 0);

    // Uptime
    uptime_label = lv_label_create(parent);
    lv_label_set_text(uptime_label, "Uptime: 00:00:00");
    lv_obj_set_style_text_font(uptime_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(uptime_label, 280);
    lv_label_set_long_mode(uptime_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(uptime_label, LV_TEXT_ALIGN_LEFT, 0);

    // Memory info
    memory_label = lv_label_create(parent);
    lv_label_set_text(memory_label, "Free Heap: 0 KB");
    lv_obj_set_style_text_font(memory_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(memory_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(memory_label, 280);
    lv_label_set_long_mode(memory_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(memory_label, LV_TEXT_ALIGN_LEFT, 0);

    // Version info
    version_label = lv_label_create(parent);
    lv_label_set_text(version_label, "Firmware: v" INTERNAL_FIRMWARE_VERSION);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(version_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_width(version_label, 280);
    lv_label_set_long_mode(version_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(version_label, LV_TEXT_ALIGN_LEFT, 0);
    
    // Build info
    build_label = lv_label_create(parent);
    char build_info[64];
    // Show build number prominently for BLE OTA testing
    snprintf(build_info, sizeof(build_info), "Build: #%d", BUILD_NUMBER);
    lv_label_set_text(build_label, build_info);
    lv_obj_set_style_text_font(build_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(build_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_width(build_label, 280);
    lv_label_set_long_mode(build_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(build_label, LV_TEXT_ALIGN_LEFT, 0);

    // Sessions info (number of recorded grind sessions)
    sessions_label = lv_label_create(parent);
    lv_label_set_text(sessions_label, "Sessions: --");
    lv_obj_set_style_text_font(sessions_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sessions_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(sessions_label, 280);
    lv_label_set_long_mode(sessions_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(sessions_label, LV_TEXT_ALIGN_LEFT, 0);

    // Events info (total number of events across all sessions)
    events_label = lv_label_create(parent);
    lv_label_set_text(events_label, "Events: --");
    lv_obj_set_style_text_font(events_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(events_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(events_label, 280);
    lv_label_set_long_mode(events_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(events_label, LV_TEXT_ALIGN_LEFT, 0);

    // Measurements info (total number of measurements across all sessions)
    measurements_label = lv_label_create(parent);
    lv_label_set_text(measurements_label, "Measurements: --");
    lv_obj_set_style_text_font(measurements_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(measurements_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_width(measurements_label, 280);
    lv_label_set_long_mode(measurements_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(measurements_label, LV_TEXT_ALIGN_LEFT, 0);

    // Refresh statistics button
    refresh_stats_button = lv_btn_create(parent);
    lv_obj_set_size(refresh_stats_button, 200, 60);
    lv_obj_set_style_bg_color(refresh_stats_button, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(refresh_stats_button, 0, 0);
    lv_obj_set_style_radius(refresh_stats_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* refresh_label = lv_label_create(refresh_stats_button);
    lv_label_set_text(refresh_label, "REFRESH STATS");
    lv_obj_set_style_text_font(refresh_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(refresh_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(refresh_label);
}

void SettingsScreen::create_settings_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 15, 0);
    lv_obj_set_style_pad_top(parent, 20, 0);

    // Enable vertical scrolling on the settings page
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    // Bluetooth separator
    create_separator(parent, "Bluetooth");

    // BLE OTA Toggle
    lv_obj_t* ble_container = lv_obj_create(parent);
    lv_obj_set_size(ble_container, 260, 80);
    lv_obj_set_layout(ble_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ble_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ble_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ble_container, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_border_width(ble_container, 0, 0);
    lv_obj_set_style_radius(ble_container, 8, 0);
    lv_obj_clear_flag(ble_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ble_label = lv_label_create(ble_container);
    lv_label_set_text(ble_label, "Enabled");
    lv_obj_set_style_text_font(ble_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    ble_toggle = lv_switch_create(ble_container);
    lv_obj_set_size(ble_toggle, 100, 60);

    // BLE Startup Toggle
    lv_obj_t* ble_startup_container = lv_obj_create(parent);
    lv_obj_set_size(ble_startup_container, 260, 80);
    lv_obj_set_layout(ble_startup_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ble_startup_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ble_startup_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ble_startup_container, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_border_width(ble_startup_container, 0, 0);
    lv_obj_set_style_radius(ble_startup_container, 8, 0);
    lv_obj_clear_flag(ble_startup_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ble_startup_label = lv_label_create(ble_startup_container);
    lv_label_set_text(ble_startup_label, "Startup");
    lv_obj_set_style_text_font(ble_startup_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_startup_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    ble_startup_toggle = lv_switch_create(ble_startup_container);
    lv_obj_set_size(ble_startup_toggle, 100, 60);

    // BLE Status label
    ble_status_label = lv_label_create(parent);
    lv_label_set_text(ble_status_label, "Bluetooth: Disabled");
    lv_obj_set_style_text_font(ble_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_status_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_clear_flag(ble_status_label, LV_OBJ_FLAG_SCROLLABLE);

    // BLE Timer label
    ble_timer_label = lv_label_create(parent);
    lv_label_set_text(ble_timer_label, "");
    lv_obj_set_style_text_font(ble_timer_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_timer_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_clear_flag(ble_timer_label, LV_OBJ_FLAG_SCROLLABLE);

    // Display separator
    create_separator(parent, "Display");

    // Normal Brightness Slider
    lv_obj_t* brightness_normal_container = lv_obj_create(parent);
    lv_obj_set_size(brightness_normal_container, 260, 104);
    lv_obj_set_layout(brightness_normal_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(brightness_normal_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(brightness_normal_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(brightness_normal_container, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_border_width(brightness_normal_container, 0, 0);
    lv_obj_set_style_radius(brightness_normal_container, 8, 0);
    lv_obj_set_style_pad_all(brightness_normal_container, 10, 0);
    lv_obj_set_style_pad_gap(brightness_normal_container, 8, 0);

    brightness_normal_label = lv_label_create(brightness_normal_container);
    lv_label_set_text(brightness_normal_label, "Brightness: 100%");
    lv_obj_set_style_pad_bottom(brightness_normal_label, 4, 0);
    lv_obj_set_style_text_font(brightness_normal_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(brightness_normal_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    brightness_normal_slider = lv_slider_create(brightness_normal_container);
    lv_obj_set_size(brightness_normal_slider, 210, 40);
    lv_slider_set_range(brightness_normal_slider, 15, 100); // 15% minimum to prevent inoperability
    lv_slider_set_value(brightness_normal_slider, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_normal_slider, lv_color_hex(THEME_COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_normal_slider, lv_color_hex(THEME_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_normal_slider, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), LV_PART_KNOB);

    // Screensaver Brightness Slider
    lv_obj_t* brightness_screensaver_container = lv_obj_create(parent);
    lv_obj_set_size(brightness_screensaver_container, 260, 104);
    lv_obj_set_layout(brightness_screensaver_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(brightness_screensaver_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(brightness_screensaver_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(brightness_screensaver_container, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_border_width(brightness_screensaver_container, 0, 0);
    lv_obj_set_style_radius(brightness_screensaver_container, 8, 0);
    lv_obj_set_style_pad_all(brightness_screensaver_container, 10, 0);
    lv_obj_set_style_pad_gap(brightness_screensaver_container, 8, 0);

    brightness_screensaver_label = lv_label_create(brightness_screensaver_container);
    lv_label_set_text(brightness_screensaver_label, "Dimmed: 35%");
    lv_obj_set_style_pad_bottom(brightness_screensaver_label, 4, 0);
    lv_obj_set_style_text_font(brightness_screensaver_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(brightness_screensaver_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    brightness_screensaver_slider = lv_slider_create(brightness_screensaver_container);
    lv_obj_set_size(brightness_screensaver_slider, 210, 40);
    lv_slider_set_range(brightness_screensaver_slider, 15, 100); // 15% minimum to prevent inoperability
    lv_slider_set_value(brightness_screensaver_slider, 35, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_screensaver_slider, lv_color_hex(THEME_COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_screensaver_slider, lv_color_hex(THEME_COLOR_WARNING), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_screensaver_slider, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), LV_PART_KNOB);

}

void SettingsScreen::create_tools_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 20, 0);
    
    // Disable scrolling on the page container itself
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Tools");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    // Tare button
    tare_button = lv_btn_create(parent);
    lv_obj_set_size(tare_button, 240, 80);
    lv_obj_set_style_bg_color(tare_button, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_set_style_border_width(tare_button, 0, 0);
    lv_obj_set_style_radius(tare_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* tare_label = lv_label_create(tare_button);
    lv_label_set_text(tare_label, "TARE SCALE");
    lv_obj_set_style_text_font(tare_label, &lv_font_montserrat_24, 0);
    lv_obj_center(tare_label);

    // Calibration button
    cal_button = lv_btn_create(parent);
    lv_obj_set_size(cal_button, 240, 80);
    lv_obj_set_style_bg_color(cal_button, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_set_style_border_width(cal_button, 0, 0);
    lv_obj_set_style_radius(cal_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* cal_label = lv_label_create(cal_button);
    lv_label_set_text(cal_label, "CALIBRATE");
    lv_obj_set_style_text_font(cal_label, &lv_font_montserrat_24, 0);
    lv_obj_center(cal_label);

    // Motor test button
    motor_test_button = lv_btn_create(parent);
    lv_obj_set_size(motor_test_button, 240, 80);
    lv_obj_set_style_bg_color(motor_test_button, lv_color_hex(THEME_COLOR_ERROR), 0);
    lv_obj_set_style_border_width(motor_test_button, 0, 0);
    lv_obj_set_style_radius(motor_test_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* motor_test_label = lv_label_create(motor_test_button);
    lv_label_set_text(motor_test_label, "MOTOR TEST");
    lv_obj_set_style_text_font(motor_test_label, &lv_font_montserrat_24, 0);
    lv_obj_center(motor_test_label);
}

void SettingsScreen::create_reset_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 30, 0);
    lv_obj_set_style_pad_top(parent, 40, 0);
    
    // Disable scrolling on the page container itself
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    // Title
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Reset & Clear");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    // Purge Grind History button
    purge_button = lv_btn_create(parent);
    lv_obj_set_size(purge_button, 260, 80);
    lv_obj_set_style_bg_color(purge_button, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_set_style_border_width(purge_button, 0, 0);
    lv_obj_set_style_radius(purge_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* purge_label = lv_label_create(purge_button);
    lv_label_set_text(purge_label, "PURGE HISTORY");
    lv_obj_set_style_text_font(purge_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(purge_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(purge_label);

    // Factory Reset button
    reset_button = lv_btn_create(parent);
    lv_obj_set_size(reset_button, 260, 80);
    lv_obj_set_style_bg_color(reset_button, lv_color_hex(THEME_COLOR_ERROR), 0);
    lv_obj_set_style_border_width(reset_button, 0, 0);
    lv_obj_set_style_radius(reset_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* reset_label = lv_label_create(reset_button);
    lv_label_set_text(reset_label, "FACTORY RESET");
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(reset_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(reset_label);
}

void SettingsScreen::create_back_button() {
    back_button = lv_btn_create(screen);
    lv_obj_set_size(back_button, LV_PCT(100), LV_PCT(20));
    lv_obj_align(back_button, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_border_width(back_button, 0, 0);
    lv_obj_set_style_radius(back_button, THEME_CORNER_RADIUS_PX, 0);
    
    lv_obj_t* back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, "BACK");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(back_label);
}

void SettingsScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
    update_ble_status();
    update_brightness_sliders();
    update_bluetooth_startup_toggle();
}

void SettingsScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void SettingsScreen::update_info(const char* load_cell_info, unsigned long uptime_ms, size_t free_heap) {
    if (!visible) return;
    
    // Update load cell reading
    lv_label_set_text(info_label, load_cell_info);
    
    // Update uptime - use compact format to avoid horizontal scrolling
    unsigned long seconds = uptime_ms / 1000;
    unsigned long hours = seconds / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;
    
    char uptime_text[48];
    snprintf(uptime_text, sizeof(uptime_text), "Up: %02lu:%02lu:%02lu", hours, minutes, seconds);
    lv_label_set_text(uptime_label, uptime_text);
    
    // Update memory info - use compact format
    char memory_text[48];
    snprintf(memory_text, sizeof(memory_text), "RAM: %zu KB", free_heap / 1024);
    lv_label_set_text(memory_label, memory_text);
}

void SettingsScreen::update_ble_status() {
    if (!visible || !bluetooth_manager) return;
    
    // Update toggle state
    if (bluetooth_manager->is_enabled()) {
        lv_obj_add_state(ble_toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(ble_toggle, LV_STATE_CHECKED);
    }
    
    // Update status text
    if (bluetooth_manager->is_enabled()) {
        if (bluetooth_manager->is_connected()) {
            lv_label_set_text(ble_status_label, "Connected");
        } else {
            lv_label_set_text(ble_status_label, "Advertising");
        }
        lv_obj_clear_flag(ble_status_label, LV_OBJ_FLAG_HIDDEN);
        
        // Show remaining time
        unsigned long remaining_ms = bluetooth_manager->get_bluetooth_timeout_remaining_ms();
        unsigned long remaining_min = remaining_ms / (60 * 1000);
        char timer_text[64];
        snprintf(timer_text, sizeof(timer_text), "Auto-disable in: %lu min", remaining_min);
        lv_label_set_text(ble_timer_label, timer_text);
        lv_obj_clear_flag(ble_timer_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        // When nothing to display hide the status labels
        lv_obj_add_flag(ble_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ble_timer_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsScreen::show_taring_overlay() {
    lv_obj_clear_flag(taring_overlay, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::hide_taring_overlay() {
    lv_obj_add_flag(taring_overlay, LV_OBJ_FLAG_HIDDEN);
}


void SettingsScreen::set_session_count(uint32_t count) {
    if (!sessions_label) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "Sessions: %lu", (unsigned long)count);
    lv_label_set_text(sessions_label, buf);
}

void SettingsScreen::set_event_count(uint32_t count) {
    if (!events_label) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "Events: %lu", (unsigned long)count);
    lv_label_set_text(events_label, buf);
}

void SettingsScreen::set_measurement_count(uint32_t count) {
    if (!measurements_label) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "Measurements: %lu", (unsigned long)count);
    lv_label_set_text(measurements_label, buf);
}

void SettingsScreen::refresh_statistics() {
    if (!visible) return;
    
    // Show loading indicators
    lv_label_set_text(sessions_label, "Sessions: Loading...");
    lv_label_set_text(events_label, "Events: Loading...");
    lv_label_set_text(measurements_label, "Measurements: Loading...");
    
    // Force a UI update to show loading text
    lv_task_handler();
    
    // Perform the expensive IO operations
    uint32_t session_count = grind_logger.get_total_flash_sessions();
    uint32_t event_count = grind_logger.count_total_events_in_flash();
    uint32_t measurement_count = grind_logger.count_total_measurements_in_flash();
    
    // Update with actual values
    set_session_count(session_count);
    set_event_count(event_count);
    set_measurement_count(measurement_count);
}

void SettingsScreen::update_brightness_sliders() {
    if (!hardware_manager || !brightness_normal_slider || !brightness_screensaver_slider) return;
    
    // Read from the dedicated "brightness" namespace using a local Preferences
    // instance so we don't interfere with the global shared handle.
    Preferences prefs;
    prefs.begin("brightness", true); // read-only
    
    // Load brightness values from preferences (default to compile-time values)
    float normal_brightness = prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
    float screensaver_brightness = prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
    prefs.end();
    
    // Convert from 0.0-1.0 to 15-100 range
    int normal_percent = (int)(normal_brightness * 100);
    int screensaver_percent = (int)(screensaver_brightness * 100);
    
    // Ensure minimum 15%
    if (normal_percent < 15) normal_percent = 15;
    if (screensaver_percent < 15) screensaver_percent = 15;
    
    // Update sliders
    lv_slider_set_value(brightness_normal_slider, normal_percent, LV_ANIM_OFF);
    lv_slider_set_value(brightness_screensaver_slider, screensaver_percent, LV_ANIM_OFF);
    
    update_brightness_labels();
}

void SettingsScreen::update_brightness_labels() {
    if (!brightness_normal_label || !brightness_screensaver_label) return;
    
    int normal_percent = lv_slider_get_value(brightness_normal_slider);
    int screensaver_percent = lv_slider_get_value(brightness_screensaver_slider);
    
    char normal_text[32];
    char screensaver_text[32];
    
    snprintf(normal_text, sizeof(normal_text), "Brightness: %d%%", normal_percent);
    snprintf(screensaver_text, sizeof(screensaver_text), "Dimmed: %d%%", screensaver_percent);
    
    lv_label_set_text(brightness_normal_label, normal_text);
    lv_label_set_text(brightness_screensaver_label, screensaver_text);
}

lv_obj_t* SettingsScreen::create_separator(lv_obj_t* parent, const char* text) {
    // Create separator container
    lv_obj_t* separator_container = lv_obj_create(parent);
    lv_obj_set_size(separator_container, 280, 40);
    lv_obj_set_style_bg_opa(separator_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(separator_container, 0, 0);
    lv_obj_set_style_pad_all(separator_container, 0, 0);
    lv_obj_set_layout(separator_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(separator_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(separator_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create left line
    lv_obj_t* left_line = lv_obj_create(separator_container);
    lv_obj_set_size(left_line, LV_SIZE_CONTENT, 2);
    lv_obj_set_flex_grow(left_line, 1);
    lv_obj_set_style_bg_color(left_line, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_border_width(left_line, 0, 0);
    lv_obj_set_style_radius(left_line, 1, 0);

    // Create text label
    lv_obj_t* separator_label = lv_label_create(separator_container);
    lv_label_set_text(separator_label, text);
    lv_obj_set_style_text_font(separator_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(separator_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_pad_left(separator_label, 10, 0);
    lv_obj_set_style_pad_right(separator_label, 10, 0);

    // Create right line
    lv_obj_t* right_line = lv_obj_create(separator_container);
    lv_obj_set_size(right_line, LV_SIZE_CONTENT, 2);
    lv_obj_set_flex_grow(right_line, 1);
    lv_obj_set_style_bg_color(right_line, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_border_width(right_line, 0, 0);
    lv_obj_set_style_radius(right_line, 1, 0);

    return separator_container;
}

void SettingsScreen::update_bluetooth_startup_toggle() {
    if (!ble_startup_toggle) return;

    // Read from the "bluetooth" namespace using a local Preferences instance
    Preferences prefs;
    prefs.begin("bluetooth", true); // read-only

    // Load startup value from preferences (default to false)
    bool startup_enabled = prefs.getBool("startup", true);
    prefs.end();

    // Update toggle state
    if (startup_enabled) {
        lv_obj_add_state(ble_startup_toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(ble_startup_toggle, LV_STATE_CHECKED);
    }
}
