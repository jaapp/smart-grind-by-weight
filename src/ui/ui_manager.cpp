#include "ui_manager.h"
#include <Arduino.h>
#include "../config/ui_theme.h"
#include "../config/constants.h"
#include "../config/system_config.h"
#include "../config/user_config.h"
#include "../config/build_info.h"
#include "screens/calibration_screen.h"
#include "event_handlers.h"
#include "../logging/grind_logging.h"

// Static instance pointer for grind event callbacks
UIManager* UIManager::instance = nullptr;


void UIManager::init(HardwareManager* hw_mgr, StateMachine* sm, 
                     ProfileController* pc, GrindController* gc, BluetoothManager* bluetooth) {
    hardware_manager = hw_mgr;
    state_machine = sm;
    profile_controller = pc;
    grind_controller = gc;
    bluetooth_manager = bluetooth;
    
    // Set static instance for event callbacks
    instance = this;
    
    edit_weight = 0.0f;
    original_weight = 0.0f;
    calibration_weight = USER_CALIBRATION_REFERENCE_WEIGHT_G;
    final_grind_weight = 0.0f;
    final_grind_progress = 0;
    timeout_grind_weight = 0.0f;
    timeout_grind_progress = 0;
    timeout_phase_name = "";
    current_tab = profile_controller->get_current_profile();
    jog_timer = nullptr;
    motor_timer = nullptr;
    grind_complete_timer = nullptr;
    grind_timeout_timer = nullptr;
    pending_confirm_callback = nullptr;
    
    // Initialize OTA failure state
    ota_failed_expected_build[0] = '\0';
    data_export_active = false;
    
    // Initialize screen timeout state
    screen_dimmed = false;
    
    // Initialize event handlers
    profile_handler = new ProfileEventHandler(this);
    grind_handler = new GrindEventHandler(this);
    settings_handler = new SettingsEventHandler(this);
    calibration_handler = new CalibrationEventHandler(this);
    
    // Initialize the unified overlay system
    BlockingOperationOverlay::getInstance().init();
    jog_start_time = 0;
    jog_stage = 1;
    
    create_ui();
    
    // Set initial brightness from preferences
    hardware_manager->get_display()->set_brightness(get_normal_brightness());
    
    // Register grind event callback
    grind_controller->set_ui_event_callback(grind_event_handler);
    
    
    initialized = true;
}

void UIManager::create_ui() {
    // Set background style
    static lv_style_t style_screen;
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(THEME_COLOR_BACKGROUND));
    lv_obj_add_style(lv_scr_act(), &style_screen, 0);

    // Create all screens
    ready_screen.create();
    edit_screen.create();
    grinding_screen.init(hardware_manager->get_preferences());
    grinding_screen.create();
    settings_screen.create(bluetooth_manager, grind_controller, &grinding_screen, hardware_manager);
    calibration_screen.create();
    confirm_screen.create();
    ota_screen.create();
    ota_update_failed_screen.create();
    
    // Initialize ready screen with correct weights from profile controller
    float weights[3];
    for (int i = 0; i < 3; i++) {
        weights[i] = profile_controller->get_profile_weight(i);
    }
    ready_screen.update_weights(weights);
    
    create_grind_button();
    create_ble_status_icon();
    setup_event_handlers();
    
    // Set up initial state
    ready_screen.hide();
    edit_screen.hide();
    grinding_screen.hide();
    settings_screen.hide();
    calibration_screen.hide();
    confirm_screen.hide();
    ota_screen.hide();
    ota_update_failed_screen.hide();
    
    // Set up event handler for OTA failure screen OK button
    lv_obj_add_event_cb(ota_update_failed_screen.get_ok_button(), [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
            ui_manager->switch_to_state(UIState::READY);
            // Clear the OTA failure state
            ui_manager->ota_failed_expected_build[0] = '\0';
        }
    }, LV_EVENT_CLICKED, this);
    
    // Initialize UI to current state (set by state_machine during boot)
    switch_to_state(state_machine->get_current_state());
}

void UIManager::create_grind_button() {
    grind_button = lv_btn_create(lv_scr_act());
    lv_obj_set_size(grind_button, 100, 100);
    lv_obj_align(grind_button, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(grind_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(grind_button, 0, 0);
    lv_obj_set_style_shadow_width(grind_button, 0, 0);

    grind_icon = lv_img_create(grind_button);
    lv_img_set_src(grind_icon, LV_SYMBOL_PLAY);
    lv_obj_center(grind_icon);
    lv_obj_set_style_text_font(grind_icon, &lv_font_montserrat_24, 0);
}

void UIManager::create_ble_status_icon() {
    ble_status_icon = lv_label_create(lv_scr_act());
    lv_label_set_text(ble_status_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(ble_status_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_status_icon, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_align(ble_status_icon, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(ble_status_icon, LV_OBJ_FLAG_HIDDEN); // Hidden by default
}

void UIManager::update_ble_status_icon() {
    if (bluetooth_manager && bluetooth_manager->is_enabled()) {
        lv_obj_clear_flag(ble_status_icon, LV_OBJ_FLAG_HIDDEN);
        if (bluetooth_manager->is_connected()) {
            lv_obj_set_style_text_color(ble_status_icon, lv_color_hex(THEME_COLOR_SUCCESS), 0);
        } else {
            lv_obj_set_style_text_color(ble_status_icon, lv_color_hex(THEME_COLOR_ACCENT), 0);
        }
    } else {
        lv_obj_add_flag(ble_status_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

void UIManager::setup_event_handlers() {
    EventBridgeLVGL::set_ui_manager(this);
    
    // Helper macro to cast event type to user data
    #define EVENT_DATA(type) reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::type))
    
    // Set up tabview event handler
    lv_obj_t* tabview = ready_screen.get_tabview();
    if (tabview) {
        lv_obj_add_event_cb(tabview, EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED, EVENT_DATA(TAB_CHANGE));
    }
    
    // Set up profile long press handlers
    ready_screen.set_profile_long_press_handler(EventBridgeLVGL::profile_long_press_handler);
    
    // Set up grind button event handler
    lv_obj_add_event_cb(grind_button, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(GRIND_BUTTON));
    
    // Set up edit screen event handlers
    lv_obj_add_event_cb(edit_screen.get_save_btn(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(EDIT_SAVE));
    lv_obj_add_event_cb(edit_screen.get_cancel_btn(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(EDIT_CANCEL));
    lv_obj_add_event_cb(edit_screen.get_plus_btn(), EventBridgeLVGL::dispatch_event, LV_EVENT_ALL, EVENT_DATA(EDIT_PLUS));
    lv_obj_add_event_cb(edit_screen.get_minus_btn(), EventBridgeLVGL::dispatch_event, LV_EVENT_ALL, EVENT_DATA(EDIT_MINUS));
    
    // Set up developer screen handlers
    lv_obj_add_event_cb(settings_screen.get_cal_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_CALIBRATE));
    lv_obj_add_event_cb(settings_screen.get_purge_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_PURGE));
    lv_obj_add_event_cb(settings_screen.get_reset_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_RESET));
    lv_obj_add_event_cb(settings_screen.get_motor_test_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_MOTOR_TEST));
    lv_obj_add_event_cb(settings_screen.get_tare_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_TARE));
    lv_obj_add_event_cb(settings_screen.get_back_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_BACK));
    lv_obj_add_event_cb(settings_screen.get_refresh_stats_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(SETTINGS_REFRESH_STATS));
    lv_obj_add_event_cb(settings_screen.get_ble_toggle(), EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED, EVENT_DATA(BLE_TOGGLE));
    lv_obj_add_event_cb(settings_screen.get_ble_startup_toggle(), EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED, EVENT_DATA(BLE_STARTUP_TOGGLE));
    lv_obj_add_event_cb(settings_screen.get_logging_toggle(), EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED, EVENT_DATA(LOGGING_TOGGLE));
    lv_obj_add_event_cb(settings_screen.get_brightness_normal_slider(), EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED, EVENT_DATA(BRIGHTNESS_NORMAL_SLIDER));
    lv_obj_add_event_cb(settings_screen.get_brightness_normal_slider(), EventBridgeLVGL::dispatch_event, LV_EVENT_RELEASED, EVENT_DATA(BRIGHTNESS_NORMAL_SLIDER_RELEASED));
    lv_obj_add_event_cb(settings_screen.get_brightness_screensaver_slider(), EventBridgeLVGL::dispatch_event, LV_EVENT_VALUE_CHANGED, EVENT_DATA(BRIGHTNESS_SCREENSAVER_SLIDER));
    lv_obj_add_event_cb(settings_screen.get_brightness_screensaver_slider(), EventBridgeLVGL::dispatch_event, LV_EVENT_RELEASED, EVENT_DATA(BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED));
    
    // Set up calibration screen handlers
    lv_obj_add_event_cb(calibration_screen.get_ok_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(CAL_OK));
    lv_obj_add_event_cb(calibration_screen.get_cancel_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(CAL_CANCEL));
    lv_obj_add_event_cb(calibration_screen.get_plus_btn(), EventBridgeLVGL::dispatch_event, LV_EVENT_ALL, EVENT_DATA(CAL_PLUS));
    lv_obj_add_event_cb(calibration_screen.get_minus_btn(), EventBridgeLVGL::dispatch_event, LV_EVENT_ALL, EVENT_DATA(CAL_MINUS));
    
    // Set up confirmation screen handlers
    lv_obj_add_event_cb(confirm_screen.get_confirm_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(CONFIRM));
    lv_obj_add_event_cb(confirm_screen.get_cancel_button(), EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED, EVENT_DATA(CONFIRM_CANCEL));
    
    // MODIFIED: Attach event handler for toggling grind screen layout to BOTH screens
    auto grind_screen_toggle_cb = [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
            if (ui_manager) {
                ui_manager->handle_grind_layout_toggle();
            }
        }
    };

    // Attach the same callback to both screen objects
    lv_obj_add_event_cb(grinding_screen.get_arc_screen_obj(), grind_screen_toggle_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(grinding_screen.get_chart_screen_obj(), grind_screen_toggle_cb, LV_EVENT_CLICKED, this);
    
    #undef EVENT_DATA
}

void UIManager::update() {
    if (!initialized) return;
    
    // Check and update screen timeout state
    update_screen_timeout_state();
    
    // Update BLE status icon
    update_ble_status_icon();
    
    // Check if OTA update is in progress and switch to OTA screen
    if (bluetooth_manager && bluetooth_manager->is_updating()) {
        if (!state_machine->is_state(UIState::OTA_UPDATE)) {
            ota_screen.show_ota_mode();
            switch_to_state(UIState::OTA_UPDATE);
        } else {
            // Update OTA progress
            int progress = (int)bluetooth_manager->get_ota_progress();
            ota_screen.update_progress(progress);
        }
        return; // Skip other UI updates during OTA
    }
    
    // Check if data export is in progress
    if (bluetooth_manager && bluetooth_manager->is_data_export_active()) {
        if (!data_export_active) {
            start_data_export_ui();
        } else {
            update_data_export_progress();
        }
    } else if (data_export_active) {
        stop_data_export_ui();
    }
    
    // Update based on current state
    UIState current = state_machine->get_current_state();
    
    switch (current) {
        case UIState::GRINDING:
            // Event-driven updates - no polling needed
            break;
            
        case UIState::SETTINGS:
            update_settings_state();
            break;
            
        case UIState::CALIBRATION:
            update_calibration_state();
            break;
            
        case UIState::GRIND_COMPLETE:
            update_grind_complete_state();
            break;
            
        case UIState::GRIND_TIMEOUT:
            update_grind_timeout_state();
            break;
            
        case UIState::READY:
            // Ready state - no special handling needed
            break;
            
        default:
            break;
    }
}

void UIManager::switch_to_state(UIState new_state) {
    state_machine->transition_to(new_state);
    
    // Hide all screens
    ready_screen.hide();
    edit_screen.hide();
    grinding_screen.hide();
    settings_screen.hide();
    calibration_screen.hide();
    confirm_screen.hide();
    ota_screen.hide();
    ota_update_failed_screen.hide();
    
    // Clean up any existing timers
    if (grind_complete_timer) {
        lv_timer_del(grind_complete_timer);
        grind_complete_timer = nullptr;
    }
    if (grind_timeout_timer) {
        lv_timer_del(grind_timeout_timer);
        grind_timeout_timer = nullptr;
    }
    
    switch (new_state) {
        case UIState::READY:
            ready_screen.show();
            ready_screen.set_active_tab(current_tab);
            lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case UIState::EDIT:
            edit_screen.show();
            edit_screen.update_profile_name(profile_controller->get_current_name());
            edit_screen.update_weight(edit_weight);
            lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case UIState::GRINDING:
            {
                UI_DEBUG_LOG("[%lums UI_SCREEN_VISIBLE] GRINDING screen showing\n", millis());
                WeightSensor* weight_sensor = hardware_manager->get_weight_sensor();
                grinding_screen.reset_chart_data(); // Explicitly reset chart for new grind
                grinding_screen.show();
                grinding_screen.update_profile_name(profile_controller->get_current_name());
                grinding_screen.update_target_weight(grind_controller->get_target_weight());
                grinding_screen.update_current_weight(weight_sensor->get_display_weight());
                grinding_screen.update_progress(0);
                lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            }
            break;
            
        case UIState::GRIND_COMPLETE:
            grinding_screen.show();
            grinding_screen.update_profile_name(profile_controller->get_current_name());
            grinding_screen.update_target_weight(grind_controller->get_target_weight());
            grinding_screen.update_current_weight(final_grind_weight); // Use captured final settled weight
            grinding_screen.update_progress(final_grind_progress); // Use captured final progress
            lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            
            // Start 1-minute timer to automatically return to ready
            grind_complete_timer = lv_timer_create(static_grind_complete_timeout_cb, 60000, this); // 60 seconds
            lv_timer_set_repeat_count(grind_complete_timer, 1);
            break;
            
        case UIState::GRIND_TIMEOUT:
            grinding_screen.show();
            // Show fixed "TIMEOUT" title
            grinding_screen.update_profile_name("TIMEOUT");
            // Show timeout phase where target weight is normally displayed
            char timeout_phase_display[64];
            snprintf(timeout_phase_display, sizeof(timeout_phase_display), "Stuck in: %s", timeout_phase_name);
            grinding_screen.update_target_weight_text(timeout_phase_display);
            grinding_screen.update_current_weight(timeout_grind_weight); // Use captured timeout weight
            grinding_screen.update_progress(timeout_grind_progress); // Use captured timeout progress
            lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            
            // Start 1-minute timer to automatically return to ready  
            grind_timeout_timer = lv_timer_create(static_grind_timeout_timer_cb, 60000, this); // 60 seconds
            lv_timer_set_repeat_count(grind_timeout_timer, 1);
            break;
            
        case UIState::SETTINGS:
            settings_screen.show();
            lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            // Statistics are now loaded on-demand via refresh button
            break;
            
        case UIState::CALIBRATION:
            {
                // Load the last used calibration weight
                float saved_cal_weight = hardware_manager->get_weight_sensor()->get_saved_calibration_weight();
                calibration_screen.show();
                calibration_screen.set_step(CAL_STEP_EMPTY);
                calibration_screen.update_calibration_weight(saved_cal_weight);
                lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            }
            break;
            
        case UIState::CONFIRM:
            // Confirm screen content is set when transitioning to this state
            // Content is configured by the caller before switching to CONFIRM state
            confirm_screen.show();
            lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case UIState::OTA_UPDATE:
            ota_screen.show();
            ota_screen.update_progress(0);
            lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            break;

        case UIState::OTA_UPDATE_FAILED:
            ota_update_failed_screen.show(ota_failed_expected_build);
            lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            break;
    }
    
    update_grind_button_icon();
}

void UIManager::update_grind_button_icon() {
    if (state_machine->is_state(UIState::GRINDING)) {
        lv_img_set_src(grind_icon, LV_SYMBOL_STOP);
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    } else if (state_machine->is_state(UIState::GRIND_COMPLETE)) {
        lv_img_set_src(grind_icon, LV_SYMBOL_OK);
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    } else if (state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        lv_img_set_src(grind_icon, LV_SYMBOL_CLOSE);
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_WARNING), 0);
    } else if (state_machine->is_state(UIState::READY) && current_tab == 3) {
        // Settings tab active on ready screen - show gray settings icon
        lv_img_set_src(grind_icon, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    } else {
        lv_img_set_src(grind_icon, LV_SYMBOL_PLAY);
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    }
}

// Event handlers
void UIManager::handle_tab_change(int tab) {
    profile_handler->handle_tab_change(tab);
    // Update grind button appearance based on active tab
    update_grind_button_icon();
}

void UIManager::handle_profile_long_press() {
    profile_handler->handle_profile_long_press();
}

void UIManager::handle_grind_button() {
    grind_handler->handle_grind_button();
}

void UIManager::handle_settings_calibrate() {
    settings_handler->handle_settings_calibrate();
}

void UIManager::handle_settings_reset() {
    settings_handler->handle_settings_reset();
}

void UIManager::handle_settings_purge() {
    settings_handler->handle_settings_purge();
}

void UIManager::handle_settings_motor_test() {
    settings_handler->handle_settings_motor_test();
}


void UIManager::handle_settings_tare() {
    settings_handler->handle_settings_tare();
}

void UIManager::handle_settings_back() {
    settings_handler->handle_settings_back();
}


void UIManager::handle_settings_refresh_stats() {
    settings_screen.refresh_statistics();
}

void UIManager::handle_cal_ok() {
    calibration_handler->handle_cal_ok();
}

void UIManager::handle_cal_cancel() {
    calibration_handler->handle_cal_cancel();
}

void UIManager::handle_ble_toggle() {
    settings_handler->handle_ble_toggle();
}

void UIManager::handle_ble_startup_toggle() {
    settings_handler->handle_ble_startup_toggle();
}

void UIManager::handle_logging_toggle() {
    settings_handler->handle_logging_toggle();
}

void UIManager::handle_brightness_normal_slider() {
    settings_handler->handle_brightness_normal_slider();
}

void UIManager::handle_brightness_normal_slider_released() {
    settings_handler->handle_brightness_normal_slider_released();
}

void UIManager::handle_brightness_screensaver_slider() {
    settings_handler->handle_brightness_screensaver_slider();
}

void UIManager::handle_brightness_screensaver_slider_released() {
    settings_handler->handle_brightness_screensaver_slider_released();
}

void UIManager::handle_confirm() {
    settings_handler->handle_confirm();
}

void UIManager::handle_confirm_cancel() {
    settings_handler->handle_confirm_cancel();
}

void UIManager::handle_grind_layout_toggle() {
    // Only allow toggling during an active grind
    if (state_machine->is_state(UIState::GRINDING) || state_machine->is_state(UIState::GRIND_COMPLETE) || state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        GrindScreenLayout current_layout = grinding_screen.get_layout();
        if (current_layout == GrindScreenLayout::MINIMAL_ARC) {
            grinding_screen.set_layout(GrindScreenLayout::NERDY_CHART);
        } else {
            grinding_screen.set_layout(GrindScreenLayout::MINIMAL_ARC);
        }
    }
}

void UIManager::show_confirmation(const char* title, const char* message, 
                                 const char* confirm_text, lv_color_t confirm_color,
                                 void (*on_confirm)(), const char* cancel_text) {
    pending_confirm_callback = on_confirm;
    confirm_screen.show(title, message, confirm_text, confirm_color, cancel_text);
    switch_to_state(UIState::CONFIRM);
}

void UIManager::handle_cal_plus(lv_event_code_t code) {
    calibration_handler->handle_cal_plus(code);
}

void UIManager::handle_cal_minus(lv_event_code_t code) {
    calibration_handler->handle_cal_minus(code);
}

void UIManager::handle_edit_save() {
    profile_handler->handle_edit_save();
}

void UIManager::handle_edit_cancel() {
    profile_handler->handle_edit_cancel();
}

void UIManager::handle_edit_plus(lv_event_code_t code) {
    profile_handler->handle_edit_plus(code);
}

void UIManager::handle_edit_minus(lv_event_code_t code) {
    profile_handler->handle_edit_minus(code);
}

void UIManager::update_edit_weight_display() {
    edit_screen.update_weight(edit_weight);
}

void UIManager::start_jog_timer(int direction) {
    jog_start_time = millis();
    jog_stage = 1;
    jog_direction = direction;
    
    if (jog_timer == nullptr) {
        jog_timer = lv_timer_create(static_jog_timer_cb, USER_JOG_STAGE_1_INTERVAL_MS, this);
    } else {
        lv_timer_set_user_data(jog_timer, this);
        lv_timer_set_period(jog_timer, USER_JOG_STAGE_1_INTERVAL_MS);
        lv_timer_resume(jog_timer);
    }
}

void UIManager::stop_jog_timer() {
    if (jog_timer) {
        lv_timer_pause(jog_timer);
    }
}

void UIManager::jog_timer_cb(lv_timer_t* timer) {
    
    // Check for acceleration stage transitions
    unsigned long elapsed = millis() - jog_start_time;
    
    // Determine current multiplier
    int multiplier = SYS_JOG_STAGE_1_MULTIPLIER;
    
    if (elapsed >= USER_JOG_STAGE_4_THRESHOLD_MS && jog_stage < 4) {
        // Transition to stage 4 (ultra fast)
        jog_stage = 4;
        lv_timer_set_period(timer, SYS_JOG_STAGE_4_INTERVAL_MS);
        multiplier = SYS_JOG_STAGE_4_MULTIPLIER;
    } else if (elapsed >= USER_JOG_STAGE_3_THRESHOLD_MS && jog_stage < 3) {
        // Transition to stage 3 (fast)
        jog_stage = 3;
        lv_timer_set_period(timer, SYS_JOG_STAGE_3_INTERVAL_MS);
        multiplier = SYS_JOG_STAGE_3_MULTIPLIER;
    } else if (elapsed >= USER_JOG_STAGE_2_THRESHOLD_MS && jog_stage < 2) {
        // Transition to stage 2 (medium)
        jog_stage = 2;
        lv_timer_set_period(timer, SYS_JOG_STAGE_2_INTERVAL_MS);
        multiplier = SYS_JOG_STAGE_2_MULTIPLIER;
    } else if (jog_stage == 2) {
        multiplier = SYS_JOG_STAGE_2_MULTIPLIER;
    } else if (jog_stage == 3) {
        multiplier = SYS_JOG_STAGE_3_MULTIPLIER;
    } else if (jog_stage == 4) {
        multiplier = SYS_JOG_STAGE_4_MULTIPLIER;
    }
    
    // Apply multiple increments based on current stage
    for (int i = 0; i < multiplier; i++) {
        // Check which mode we're in and update accordingly
        if (state_machine->is_state(UIState::EDIT)) {
            // Edit mode - update edit weight using ProfileController validation
            edit_weight = profile_controller->clamp_weight(
                edit_weight + jog_direction * USER_FINE_WEIGHT_ADJUSTMENT_G);
        } else if (state_machine->is_state(UIState::CALIBRATION)) {
            // Calibration mode - update calibration weight using ProfileController validation
            float cal_weight = calibration_screen.get_calibration_weight();
            cal_weight = profile_controller->clamp_weight(
                cal_weight + jog_direction * USER_FINE_WEIGHT_ADJUSTMENT_G);
            calibration_screen.update_calibration_weight(cal_weight);
        }
    }
    
    // Update display only once per timer callback
    if (state_machine->is_state(UIState::EDIT)) {
        update_edit_weight_display();
    }
}

void UIManager::motor_timer_cb(lv_timer_t* timer) {
    // Stop the motor
    hardware_manager->get_grinder()->stop();
    
    // Go back to developer screen
    current_tab = 3;
    switch_to_state(UIState::SETTINGS);
    
    // Clean up timer
    motor_timer = nullptr;
}

void UIManager::grind_complete_timeout_cb(lv_timer_t* timer) {
    // Automatically return to ready screen after 1 minute
    // Acknowledge completion to transition controller from COMPLETED to IDLE
    grind_controller->return_to_idle();
    
    // Clean up timer
    grind_complete_timer = nullptr;
}

void UIManager::update_ota_progress(int percent) {
    if (state_machine->is_state(UIState::OTA_UPDATE)) {
        ota_screen.update_progress(percent);
    }
}

void UIManager::update_ota_status(const char* status) {
    if (state_machine->is_state(UIState::OTA_UPDATE)) {
        ota_screen.update_status(status);
    }
}

void UIManager::show_ota_update_failed_warning(const char* expected_build) {
    // Store the expected build number before switching state
    set_ota_failure_info(expected_build);
    
    // Set up event handler for the OK button using user_data
    lv_obj_add_event_cb(ota_update_failed_screen.get_ok_button(), [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
            ui_manager->ota_update_failed_screen.hide();
            ui_manager->switch_to_state(UIState::READY);
        }
    }, LV_EVENT_CLICKED, this);
    switch_to_state(UIState::OTA_UPDATE_FAILED);
}

void UIManager::set_ota_failure_info(const char* expected_build) {
    if (expected_build && strlen(expected_build) > 0) {
        strncpy(ota_failed_expected_build, expected_build, sizeof(ota_failed_expected_build) - 1);
        ota_failed_expected_build[sizeof(ota_failed_expected_build) - 1] = '\0';
    }
}

// Data export methods
void UIManager::start_data_export_ui() {
    if (bluetooth_manager->is_data_export_active()) {
        data_export_active = true;
        ota_screen.show_data_export_mode();
        switch_to_state(UIState::OTA_UPDATE); // Reuse OTA state for screen locking
    }
}

void UIManager::update_data_export_progress() {
    if (data_export_active && bluetooth_manager->is_data_export_active()) {
        float progress = bluetooth_manager->get_data_export_progress();
        int percent = static_cast<int>(progress);
        
        ota_screen.update_progress(percent);
        
        // Update status to match OTA screen pattern exactly  
        ota_screen.update_status("Sending data....");
    }
    
    // Check if export completed or failed
    if (data_export_active && !bluetooth_manager->is_data_export_active()) {
        Serial.printf("UI: Data export ended - progress was at %d%%\n", static_cast<int>(bluetooth_manager->get_data_export_progress()));
        stop_data_export_ui();
    }
}

void UIManager::stop_data_export_ui() {
    if (data_export_active) {
        data_export_active = false;
        
        // Always ensure data export is properly stopped in bluetooth manager
        // This handles WeightSensor reinitialization even if export failed early
        if (bluetooth_manager) {
            bluetooth_manager->stop_data_export();
        }
        
        ota_screen.hide();
        switch_to_state(UIState::READY);
    }
}


// State-specific update methods
void UIManager::update_settings_state() {
    // Update developer screen with comprehensive system info
    WeightSensor* lc = hardware_manager->get_weight_sensor();
    // Show both instant and high-latency weight for diagnostic purposes
    float weight_instant = lc->get_instant_weight();
    float weight_smoothed = lc->get_weight_high_latency();
    int32_t raw_reading = lc->get_raw_adc_instant();
    int sample_count = lc->get_sample_count();
    
    char leight_sensor_info[256];
    snprintf(leight_sensor_info, sizeof(leight_sensor_info), 
        "Instant: %.2fg\nSmoothed: %.2fg\nSamples: %d\nRaw: %ld", 
        weight_instant, weight_smoothed, sample_count, (long)raw_reading);
    
    unsigned long uptime_ms = millis();
    size_t free_heap = ESP.getFreeHeap();
    
    settings_screen.update_info(leight_sensor_info, uptime_ms, free_heap);
    settings_screen.update_ble_status();
}

void UIManager::update_calibration_state() {
    // Update calibration screen with raw reading for step 1, calibrated weight for step 3
    CalibrationStep current_step = calibration_screen.get_step();
    if (current_step == CAL_STEP_COMPLETE) {
        // Show display weight in grams for smooth UI updates
        float weight = hardware_manager->get_weight_sensor()->get_display_weight();
        calibration_screen.update_current_weight(weight);
    } else {
        // Show raw sensor reading for better visibility of changes
        int32_t raw_reading = hardware_manager->get_weight_sensor()->get_raw_adc_instant();
        calibration_screen.update_current_weight((float)raw_reading);
    }
}

void UIManager::update_grind_complete_state() {
    // Show live weight updates - allows user to manually correct overshoots by scooping grinds out
    WeightSensor* weight_sensor = hardware_manager->get_weight_sensor();
    float current_weight = weight_sensor->get_display_weight();
    grinding_screen.update_current_weight(current_weight);
    
    // Keep the final progress percentage as captured (represents final grind completion status)
    grinding_screen.update_progress(final_grind_progress);
}

void UIManager::update_grind_timeout_state() {
    // Show static timeout weight - no live updates, display captured timeout values
    grinding_screen.update_current_weight(timeout_grind_weight);
    grinding_screen.update_progress(timeout_grind_progress);
    
    // TODO: Display timeout phase information in UI
    // For now, phase info is captured and can be used for debugging
}

void UIManager::update_screen_timeout_state() {
    if (!hardware_manager) return;
    
    TouchDriver* touch_driver = hardware_manager->get_display()->get_touch_driver();
    if (!touch_driver) return;
    
    // Don't timeout during grinding operations
    if (state_machine && state_machine->is_state(UIState::GRINDING)) {
        if (screen_dimmed) {
            hardware_manager->get_display()->set_brightness(get_normal_brightness());
            screen_dimmed = false;
        }
        return;
    }
    
    // Check if screen should be dimmed based on time since last touch or weight activity
    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    uint32_t ms_since_weight_activity = hardware_manager->get_weight_sensor()->get_ms_since_last_weight_activity();
    
    // Screen should stay bright if there's been recent touch OR weight activity
    uint32_t ms_since_last_activity = min(ms_since_touch, ms_since_weight_activity);
    bool should_be_dimmed = ms_since_last_activity >= USER_SCREEN_TIMEOUT_MS;
    
    if (should_be_dimmed && !screen_dimmed) {
        hardware_manager->get_display()->set_brightness(get_screensaver_brightness());
        screen_dimmed = true;
    } else if (!should_be_dimmed && screen_dimmed) {
        hardware_manager->get_display()->set_brightness(get_normal_brightness());
        screen_dimmed = false;
    }
}

void UIManager::grind_timeout_timer_cb(lv_timer_t* timer) {
    // Automatically return to ready screen after 1 minute
    // Acknowledge timeout to transition controller from TIMEOUT to IDLE
    grind_controller->return_to_idle();
    
    // Clean up timer
    grind_timeout_timer = nullptr;
}

// Static wrapper functions for LVGL callbacks
void UIManager::static_jog_timer_cb(lv_timer_t* timer) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_timer_get_user_data(timer));
    if (ui_manager) {
        ui_manager->jog_timer_cb(timer);
    }
}

void UIManager::static_motor_timer_cb(lv_timer_t* timer) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_timer_get_user_data(timer));
    if (ui_manager) {
        ui_manager->motor_timer_cb(timer);
    }
}

void UIManager::static_grind_complete_timeout_cb(lv_timer_t* timer) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_timer_get_user_data(timer));
    if (ui_manager) {
        ui_manager->grind_complete_timeout_cb(timer);
    }
}

void UIManager::static_grind_timeout_timer_cb(lv_timer_t* timer) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_timer_get_user_data(timer));
    if (ui_manager) {
        ui_manager->grind_timeout_timer_cb(timer);
    }
}

void UIManager::grind_event_handler(const GrindEventData& event_data) {
    if (!instance) return;
    
    switch (event_data.event) {
        case UIGrindEvent::PHASE_CHANGED:
            // Handle phase changes, particularly tare start/completion
            // Transition to GRINDING state when grind starts (any active grinding phase)
            if (event_data.phase != GrindPhase::IDLE && !instance->state_machine->is_state(UIState::GRINDING)) {
                UI_DEBUG_LOG("[%lums UI_TRANSITION] Switching to GRINDING state due to phase: %s\n", 
                        millis(), event_data.phase_display_text);
                WeightSensor* weight_sensor = instance->hardware_manager->get_weight_sensor();
                instance->grinding_screen.update_profile_name(instance->profile_controller->get_current_name());
                instance->grinding_screen.update_target_weight(instance->grind_controller->get_target_weight());
                instance->grinding_screen.update_current_weight(weight_sensor->get_display_weight());
                instance->grinding_screen.update_progress(0);
                instance->switch_to_state(UIState::GRINDING);

                // Acknowledge INITIALIZING phase transition to allow controller to proceed to SETUP
                if (event_data.phase == GrindPhase::INITIALIZING) {
                    instance->grind_controller->ui_acknowledge_phase_transition();
                    UI_DEBUG_LOG("[%lums UI_ACKNOWLEDGMENT] INITIALIZING phase confirmed, ready for SETUP\n", millis());
                }
            }
            
            // Update display based on current phase
            if (event_data.show_taring_text) {
                instance->grinding_screen.update_tare_display();
            } else {
                instance->grinding_screen.update_current_weight(event_data.current_weight);
                instance->grinding_screen.update_progress(event_data.progress_percent);
                
                // Add chart data point with flow rate during active grinding (but not when completed)
                if (event_data.phase != GrindPhase::IDLE && event_data.phase != GrindPhase::TARING && 
                    event_data.phase != GrindPhase::TARE_CONFIRM && event_data.phase != GrindPhase::INITIALIZING && 
                    event_data.phase != GrindPhase::SETUP && event_data.phase != GrindPhase::COMPLETED && 
                    event_data.phase != GrindPhase::TIMEOUT) {                    
                    instance->grinding_screen.add_chart_data_point(event_data.current_weight, event_data.flow_rate, millis());
                }
            }
            break;
            
        case UIGrindEvent::PROGRESS_UPDATED:
            // Update display with current progress
            if (event_data.show_taring_text) {
                instance->grinding_screen.update_tare_display();
            } else {
                instance->grinding_screen.update_current_weight(event_data.current_weight);
                instance->grinding_screen.update_progress(event_data.progress_percent);
                
                // Add chart data point with flow rate during active grinding (but not when completed)
                if (event_data.phase != GrindPhase::IDLE && event_data.phase != GrindPhase::TARING && 
                    event_data.phase != GrindPhase::TARE_CONFIRM && event_data.phase != GrindPhase::INITIALIZING && 
                    event_data.phase != GrindPhase::SETUP && event_data.phase != GrindPhase::COMPLETED && 
                    event_data.phase != GrindPhase::TIMEOUT) {                    
                    instance->grinding_screen.add_chart_data_point(event_data.current_weight, event_data.flow_rate, millis());
                }
            }
            break;
            
        case UIGrindEvent::COMPLETED:
            // Capture final weight and transition to completion state
            instance->final_grind_weight = event_data.final_weight;
            instance->final_grind_progress = event_data.progress_percent;
            BLE_LOG("GRIND COMPLETE - Final settled weight captured: %.2fg (Progress: %d%%)\n", 
                    instance->final_grind_weight, instance->final_grind_progress);
            instance->switch_to_state(UIState::GRIND_COMPLETE);
            
            // Start 60-second auto-return timer
            if (instance->grind_complete_timer) {
                lv_timer_del(instance->grind_complete_timer);
            }
            instance->grind_complete_timer = lv_timer_create(instance->static_grind_complete_timeout_cb, 60000, instance);
            lv_timer_set_repeat_count(instance->grind_complete_timer, 1);
            break;
            
        case UIGrindEvent::TIMEOUT:
            // Capture timeout information and transition to timeout state
            instance->timeout_grind_weight = event_data.timeout_weight;
            instance->timeout_grind_progress = event_data.timeout_progress;
            instance->timeout_phase_name = event_data.timeout_phase;
            BLE_LOG("GRIND TIMEOUT - Phase: %s, Weight: %.2fg (Progress: %d%%)\n", 
                    instance->timeout_phase_name, instance->timeout_grind_weight, instance->timeout_grind_progress);
            instance->switch_to_state(UIState::GRIND_TIMEOUT);
            
            // Start 60-second auto-return timer  
            if (instance->grind_timeout_timer) {
                lv_timer_del(instance->grind_timeout_timer);
            }
            instance->grind_timeout_timer = lv_timer_create(instance->static_grind_timeout_timer_cb, 60000, instance);
            lv_timer_set_repeat_count(instance->grind_timeout_timer, 1);
            break;
            
        case UIGrindEvent::STOPPED:
            // Grind was stopped by user or error
            // Clean up any active timers
            if (instance->grind_complete_timer) {
                lv_timer_del(instance->grind_complete_timer);
                instance->grind_complete_timer = nullptr;
            }
            if (instance->grind_timeout_timer) {
                lv_timer_del(instance->grind_timeout_timer);
                instance->grind_timeout_timer = nullptr;
            }
            instance->switch_to_state(UIState::READY);
            break;
            
        case UIGrindEvent::BACKGROUND_CHANGE:
#if USER_ENABLE_GRINDER_BACKGROUND_INDICATOR
            // Update background color based on grinder activity
            {
                static lv_style_t style_bg;
                static bool style_initialized = false;
                
                if (!style_initialized) {
                    lv_style_init(&style_bg);
                    style_initialized = true;
                }
                
                // Set background color based on grinder state
                lv_color_t bg_color = event_data.background_active ? 
                    lv_color_hex(THEME_COLOR_MOCK_GRINDER_ACTIVE) :  // Dark yellow when active
                    lv_color_hex(THEME_COLOR_BACKGROUND);            // Black when inactive
                    
                lv_style_set_bg_color(&style_bg, bg_color);
                lv_obj_add_style(lv_scr_act(), &style_bg, 0);
                
                UI_DEBUG_LOG("[UIManager] Background: %s\n", event_data.background_active ? "ACTIVE" : "INACTIVE");
            }
#else
            // Background indicator feature is disabled - no action needed
#endif
            break;
    }
}

// Helper methods for brightness settings
float UIManager::get_normal_brightness() const {
    if (!hardware_manager) return USER_SCREEN_BRIGHTNESS_NORMAL;
    
    // Use a local Preferences instance to avoid interfering with the shared
    // global Preferences handle used elsewhere (e.g., "grinder" namespace).
    Preferences prefs;
    prefs.begin("brightness", true); // Read-only
    float brightness = prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
    prefs.end();
    
    // Ensure minimum 15% brightness to prevent inoperability
    if (brightness < 0.15f) brightness = 0.15f;
    return brightness;
}

float UIManager::get_screensaver_brightness() const {
    if (!hardware_manager) return USER_SCREEN_BRIGHTNESS_DIMMED;
    
    // Use a local Preferences instance to avoid interfering with the shared
    // global Preferences handle used elsewhere (e.g., "grinder" namespace).
    Preferences prefs;
    prefs.begin("brightness", true); // Read-only
    float brightness = prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
    prefs.end();
    
    // Ensure minimum 15% brightness to prevent inoperability
    if (brightness < 0.15f) brightness = 0.15f;
    return brightness;
}
