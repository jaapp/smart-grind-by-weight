#include "settings_screen.h"
#include <Arduino.h>
#include "../../config/build_info.h"
#include "../../config/git_info.h"
#include "../../config/constants.h"
#include "../../config/hardware_config.h"
#include "../../logging/grind_logging.h"
#include "../../hardware/hardware_manager.h"
#include "grinding_screen.h"
#include "../event_bridge_lvgl.h"

static void back_event_handler(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
    lv_obj_t * menu = (lv_obj_t *)lv_event_get_user_data(e);

    if(lv_menu_back_button_is_root(menu, obj)) {
        // Call the EventBridge handle_event directly with SETTINGS_BACK event
        EventBridgeLVGL::handle_event(EventBridgeLVGL::EventType::SETTINGS_BACK, e);
    }
}

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

    // Create menu instead of tabview
    menu = lv_menu_create(screen);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_align(menu, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(menu, 0, 0);
    lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
    lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);

    // Get header and set up flex layout
    lv_obj_t* header = lv_menu_get_main_header(menu);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(header, 10, 0);

    // Get the header label
    lv_obj_t* header_label = lv_obj_get_child(header, -1);
    if (header_label) {
        lv_obj_set_style_text_align(header_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(header_label, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(header_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    }
    // Get and style the chevron first
    lv_obj_t* back_chevron = lv_menu_get_main_header_back_button(menu);
    lv_obj_set_style_text_font(back_chevron, &lv_font_montserrat_32, 0);
    lv_obj_set_ext_click_area(back_chevron, 200);
    lv_obj_set_style_text_color(back_chevron, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);

    // Force layout update to get proper chevron dimensions
    lv_obj_update_layout(header);

    // Create spacer using chevron's actual size
    // Spacer is used to center name of the page name in the header
    lv_obj_t* spacer = lv_obj_create(header);
    lv_obj_set_size(spacer, lv_obj_get_width(back_chevron), lv_obj_get_height(back_chevron));
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    // Create main page last
    lv_obj_t* main_page = lv_menu_page_create(menu, "Settings");

    // Create sub-pages with titles
    info_tab = lv_menu_page_create(menu, "Info");
    create_info_page(info_tab);

    bluetooth_tab = lv_menu_page_create(menu, "Bluetooth");
    create_bluetooth_page(bluetooth_tab);

    display_tab = lv_menu_page_create(menu, "Display");
    create_display_page(display_tab);

    tools_tab = lv_menu_page_create(menu, "Tools");
    create_tools_page(tools_tab);

    reset_tab = lv_menu_page_create(menu, "Data");
    create_data_page(reset_tab);

    // Create menu items and link to sub-pages
    lv_obj_t* info_item = create_menu_item(main_page, "System Info");
    lv_menu_set_load_page_event(menu, info_item, info_tab);

    lv_obj_t* bluetooth_item = create_menu_item(main_page, "Bluetooth");
    lv_menu_set_load_page_event(menu, bluetooth_item, bluetooth_tab);

    lv_obj_t* display_item = create_menu_item(main_page, "Display");
    lv_menu_set_load_page_event(menu, display_item, display_tab);

    lv_obj_t* tools_item = create_menu_item(main_page, "Tools");
    lv_menu_set_load_page_event(menu, tools_item, tools_tab);

    lv_obj_t* data_item = create_menu_item(main_page, "Data");
    lv_menu_set_load_page_event(menu, data_item, reset_tab);

    // Set main page as active (menu will be the landing page)
    lv_menu_set_page(menu, main_page);
    
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
    // create_back_button();

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

    // // Title
    // lv_obj_t* title = lv_label_create(parent);
    // lv_label_set_text(title, "System Info");
    // lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    // lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_SECONDARY), 0);

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

    // Stats and refresh button moved to Data tab - Grind Logging section
}


void SettingsScreen::create_bluetooth_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Enable vertical scrolling on the settings page
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    create_toggle_row(parent, "Enabled", &ble_toggle);
    create_toggle_row(parent, "Startup", &ble_startup_toggle);

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
}

void SettingsScreen::create_display_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Enable vertical scrolling on the settings page
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    create_slider_row(parent, "Brightness", &brightness_normal_label, &brightness_normal_slider);
    create_slider_row(parent, "Screensaver", &brightness_screensaver_label, &brightness_screensaver_slider, lv_color_hex(THEME_COLOR_WARNING));
}

void SettingsScreen::create_tools_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 20, 0);
    
    // Disable scrolling on the page container itself
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    tare_button = create_button(parent, "Tare Scale");
    cal_button = create_button(parent, "Calibrate");
    motor_test_button = create_button(parent, "Motor Test");
}

void SettingsScreen::create_data_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Enable vertical scrolling on the reset page
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);


    create_toggle_row(parent, "Logging", &logging_toggle);

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

    refresh_stats_button = create_button(parent, "Refresh Stats");
    // Reset separator
    create_separator(parent, "Reset");

    purge_button = create_button(parent, "Purge History", lv_color_hex(THEME_COLOR_WARNING));
    lv_obj_set_style_margin_bottom(purge_button, 20, 0); 
    reset_button = create_button(parent, "Factory Reset", lv_color_hex(THEME_COLOR_ERROR));
}

void SettingsScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
    update_ble_status();
    update_brightness_sliders();
    update_bluetooth_startup_toggle();
    update_logging_toggle();
    refresh_statistics();
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
    lv_timer_handler();
    
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
    if (normal_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        normal_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(brightness_normal_slider, HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT, LV_ANIM_OFF);
    } 
        
    int screensaver_percent = lv_slider_get_value(brightness_screensaver_slider);
    if (screensaver_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT){
        screensaver_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(brightness_screensaver_slider, HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT, LV_ANIM_OFF);
    } 
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
    lv_obj_set_style_pad_ver(separator_container, 10, 0);
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

void SettingsScreen::update_logging_toggle() {
    if (!logging_toggle) return;

    // Read from the "logging" namespace using a local Preferences instance
    Preferences prefs;
    prefs.begin("logging", true); // read-only

    // Load logging enabled value from preferences (default to false)
    bool logging_enabled = prefs.getBool("enabled", false);
    prefs.end();

    // Update toggle state
    if (logging_enabled) {
        lv_obj_add_state(logging_toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(logging_toggle, LV_STATE_CHECKED);
    }
}

lv_obj_t* SettingsScreen::create_menu_item(lv_obj_t* parent, const char* text) {
    lv_obj_t* cont = lv_menu_cont_create(parent);
    style_as_button(cont);
    lv_obj_set_style_margin_bottom(cont, 10, 0);

    // Set layout
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* label = lv_label_create(cont);
    lv_label_set_text(label, text);

    lv_obj_t* chevron = lv_label_create(cont);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    return cont;
}

lv_obj_t* SettingsScreen::create_toggle_row(lv_obj_t* parent, const char* text, lv_obj_t** out_toggle) {
    lv_obj_t* row_container = lv_obj_create(parent);
    style_as_button(row_container);
    lv_obj_set_style_margin_bottom(row_container, 10, 0);

    // Set layout
    lv_obj_set_layout(row_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* label = lv_label_create(row_container);
    lv_label_set_text(label, text);

    *out_toggle = lv_switch_create(row_container);
    lv_obj_set_size(*out_toggle, 80, 40);
    lv_obj_set_ext_click_area(*out_toggle, 20);
    
    return row_container;
}

lv_obj_t* SettingsScreen::create_slider_row(lv_obj_t* parent, const char* text, lv_obj_t** label, lv_obj_t** slider, lv_color_t slider_color, uint32_t min, uint32_t max) {
    lv_obj_t* row_container = lv_obj_create(parent);
    style_as_button(row_container, 260, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_bottom(row_container, 10, 0);
    // Set layout

    lv_obj_set_layout(row_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row_container, 20, 0);
    lv_obj_set_style_pad_all(row_container, 20, 0);

    *label = lv_label_create(row_container);
    lv_label_set_text(*label, text);

    *slider = lv_slider_create(row_container);
    lv_obj_set_size(*slider, 220, 40);
    lv_obj_set_ext_click_area(*slider, 20);
    lv_slider_set_range(*slider, min, max);
    lv_obj_set_style_bg_color(*slider, lv_color_hex(THEME_COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*slider, slider_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(*slider, LV_OPA_TRANSP, LV_PART_KNOB);

    
    return row_container;
}

// lv_obj_t* SettingsScreen::create_data_label(lv_obj_t* parent, const char* text) {
//     lv_obj_t* label = lv_label_create(parent);
//     lv_label_set_text(label, text);
//     lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
//     lv_obj_set_style_text_color(label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
//     lv_obj_set_width(label, 280);
//     lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
//     lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
//     return label;
// }