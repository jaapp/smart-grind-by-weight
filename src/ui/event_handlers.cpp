#include "event_handlers.h"
#include "ui_manager.h"
#include "../config/constants.h"
#include "../config/system_config.h"
#include "../config/logging.h"
#include "components/ui_operations.h"
#include "components/blocking_overlay.h"
#include "screens/calibration_screen.h"
#include "../bluetooth/manager.h"

// ProfileEventHandler implementation
void ProfileEventHandler::handle_tab_change(int tab) {
    ui_manager->current_tab = tab;
    
    // Only set profile if not on DEV tab (tab 3)
    if (tab < 3) {
        ui_manager->profile_controller->set_current_profile(tab);
        ui_manager->refresh_ready_profiles();
    }
}

void ProfileEventHandler::handle_profile_long_press() {
    if (ui_manager->state_machine->is_state(UIState::READY) && ui_manager->current_tab < 3) {
        // Store the current tab so we can return to it after editing
        if (ui_manager->current_mode == GrindMode::TIME) {
            ui_manager->original_weight = ui_manager->profile_controller->get_current_time();
        } else {
            ui_manager->original_weight = ui_manager->profile_controller->get_current_weight();
        }
        ui_manager->edit_weight = ui_manager->original_weight;
        ui_manager->edit_screen.set_time_mode(ui_manager->current_mode == GrindMode::TIME);
        ui_manager->update_edit_weight_display();
        ui_manager->switch_to_state(UIState::EDIT);
    }
}

void ProfileEventHandler::handle_edit_save() {
    if (ui_manager->current_mode == GrindMode::TIME) {
        ui_manager->profile_controller->update_current_time(ui_manager->edit_weight);
    } else {
        ui_manager->profile_controller->update_current_weight(ui_manager->edit_weight);
    }
    ui_manager->profile_controller->save_profiles();
    ui_manager->refresh_ready_profiles();
    
    ui_manager->switch_to_state(UIState::READY);
}

void ProfileEventHandler::handle_edit_cancel() {
    ui_manager->edit_weight = ui_manager->original_weight;
    ui_manager->edit_screen.set_time_mode(ui_manager->current_mode == GrindMode::TIME);
    ui_manager->update_edit_weight_display();
    ui_manager->switch_to_state(UIState::READY);
}

void ProfileEventHandler::handle_edit_plus(lv_event_code_t code) {
    if (code == LV_EVENT_CLICKED) {
        if (ui_manager->current_mode == GrindMode::TIME) {
            ui_manager->edit_weight = ui_manager->profile_controller->clamp_time(ui_manager->edit_weight + USER_FINE_TIME_ADJUSTMENT_S);
        } else {
            ui_manager->edit_weight = ui_manager->profile_controller->clamp_weight(ui_manager->edit_weight + USER_FINE_WEIGHT_ADJUSTMENT_G);
        }
        ui_manager->update_edit_weight_display();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ui_manager->start_jog_timer(1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_manager->stop_jog_timer();
    }
}

void ProfileEventHandler::handle_edit_minus(lv_event_code_t code) {
    if (code == LV_EVENT_CLICKED) {
        if (ui_manager->current_mode == GrindMode::TIME) {
            ui_manager->edit_weight = ui_manager->profile_controller->clamp_time(ui_manager->edit_weight - USER_FINE_TIME_ADJUSTMENT_S);
        } else {
            ui_manager->edit_weight = ui_manager->profile_controller->clamp_weight(ui_manager->edit_weight - USER_FINE_WEIGHT_ADJUSTMENT_G);
        }
        ui_manager->update_edit_weight_display();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ui_manager->start_jog_timer(-1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_manager->stop_jog_timer();
    }
}

// GrindEventHandler implementation
void GrindEventHandler::handle_grind_button() {
    BLE_LOG("[%lums BUTTON_PRESS] Grind button pressed in state: %s\n", 
            millis(), ui_manager->state_machine->is_state(UIState::READY) ? "READY" :
                     ui_manager->state_machine->is_state(UIState::GRINDING) ? "GRINDING" :
                     ui_manager->state_machine->is_state(UIState::GRIND_COMPLETE) ? "GRIND_COMPLETE" :
                     ui_manager->state_machine->is_state(UIState::GRIND_TIMEOUT) ? "GRIND_TIMEOUT" : "OTHER");
    
    if (ui_manager->state_machine->is_state(UIState::READY)) {
        // Check if we're on the DEV tab (tab 3)
        if (ui_manager->current_tab == 3) {
            ui_manager->switch_to_state(UIState::SETTINGS);
        } else {
            // Set the profile ID for logging before starting grind
            ui_manager->grind_controller->set_grind_profile_id(ui_manager->profile_controller->get_current_profile());
            
            // Start grind with non-blocking tare - no overlay needed
            // Let the event system handle the UI state transition
            BLE_LOG("[%lums GRIND_START] About to call start_grind()\n", millis());
            ui_manager->error_message[0] = '\0';
            ui_manager->error_grind_weight = 0.0f;
            ui_manager->error_grind_progress = 0;
            float target_weight = ui_manager->profile_controller->get_current_weight();
            float target_time_seconds = ui_manager->profile_controller->get_current_time();
            uint32_t target_time_ms = static_cast<uint32_t>((target_time_seconds * 1000.0f) + 0.5f);
            ui_manager->grind_controller->start_grind(target_weight, target_time_ms, ui_manager->current_mode);
            BLE_LOG("[%lums GRIND_START] start_grind() returned\n", millis());
        }
    } else if (ui_manager->state_machine->is_state(UIState::GRINDING)) {
        ui_manager->grind_controller->stop_grind();
        // The UI will transition to READY state upon receiving the STOPPED event from the controller.
    } else if (ui_manager->state_machine->is_state(UIState::GRIND_COMPLETE)) {
        // OK button pressed - acknowledge completion to return to ready screen
        ui_manager->grind_controller->return_to_idle();
    } else if (ui_manager->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        // X button pressed - acknowledge timeout to return to ready screen
        ui_manager->grind_controller->return_to_idle();
    }
}

// SettingsEventHandler implementation
void SettingsEventHandler::handle_settings_calibrate() {
    ui_manager->switch_to_state(UIState::CALIBRATION);
}

// Static callbacks for confirmations
static UIManager* confirmation_ui_manager = nullptr;

static void factory_reset_callback() {
    if (!confirmation_ui_manager) return;
    
    DEBUG_PRINTLN("\n=== FACTORY RESET INITIATED ===");
    
    // Reset all profiles to defaults
    confirmation_ui_manager->get_profile_controller()->set_profile_weight(0, USER_SINGLE_ESPRESSO_WEIGHT_G);
    confirmation_ui_manager->get_profile_controller()->set_profile_weight(1, USER_DOUBLE_ESPRESSO_WEIGHT_G);
    confirmation_ui_manager->get_profile_controller()->set_profile_weight(2, USER_CUSTOM_PROFILE_WEIGHT_G);
    
    // Reset calibration
    confirmation_ui_manager->get_hardware_manager()->get_load_cell()->set_calibration_factor(USER_DEFAULT_CALIBRATION_FACTOR);
    confirmation_ui_manager->get_hardware_manager()->get_load_cell()->save_calibration();
    
    // Clear old txt file if it exists
    if (LittleFS.exists("/last_grind.txt")) {
        LittleFS.remove("/last_grind.txt");
    }
    
    DEBUG_PRINTLN("Factory reset completed.");
    
    // Return to developer screen
    confirmation_ui_manager->set_current_tab(3);
    confirmation_ui_manager->switch_to_state(UIState::SETTINGS);
}

static void perform_purge_operation() {
    DEBUG_PRINTLN("\n=== PURGE GRIND HISTORY INITIATED ===");
    
    // Clear new time-series sessions from flash
    extern GrindLogger grind_logger;
    bool success = grind_logger.clear_all_sessions_from_flash();
    
    if (success) {
        DEBUG_PRINTLN("Grind history purged successfully - reinitializing logger...");
    } else {
        DEBUG_PRINTLN("ERROR: Failed to purge all grind history data!");
    }
}

static void purge_operation_complete() {
    if (!confirmation_ui_manager) return;
    
    // Return to developer screen
    confirmation_ui_manager->set_current_tab(3);
    confirmation_ui_manager->switch_to_state(UIState::SETTINGS);
}

static void purge_grind_history_callback() {
    if (!confirmation_ui_manager) return;
    
    // Show blocking overlay while performing the purge operation
    auto& overlay = BlockingOperationOverlay::getInstance();
    overlay.show_and_execute(
        BlockingOperation::CUSTOM,
        perform_purge_operation,
        purge_operation_complete,
        "PURGING HISTORY...\nPlease wait"
    );
}

void SettingsEventHandler::handle_settings_reset() {
    confirmation_ui_manager = ui_manager;
    ui_manager->show_confirmation(
        "FACTORY RESET",
        "WARNING!\n\n"
        "This will reset all settings\n"
        "to factory defaults:\n\n"
        "• Profile weights\n"
        "• Calibration data\n"
        "• Grind history\n\n"
        "This action cannot be undone.",
        LV_SYMBOL_REFRESH " RESET",
        lv_color_hex(THEME_COLOR_ERROR),
        factory_reset_callback
    );
}

void SettingsEventHandler::handle_settings_purge() {
    confirmation_ui_manager = ui_manager;
    ui_manager->show_confirmation(
        "PURGE GRIND HISTORY",
        "WARNING!\n\n"
        "This will permanently\n"
        "delete all grind history\n"
        "data from flash memory.\n\n"
        "This action cannot\n"
        "be undone.",
        LV_SYMBOL_TRASH " PURGE",
        lv_color_hex(THEME_COLOR_ERROR),
        purge_grind_history_callback
    );
}

static void motor_test_callback() {
    if (!confirmation_ui_manager) return;
    
    // Use RMT pulse for precise 1000ms motor test
    confirmation_ui_manager->get_hardware_manager()->get_grinder()->start_pulse_rmt(1000);
    
    // Wait for pulse to complete (with timeout)
    unsigned long start_time = millis();
    while (!confirmation_ui_manager->get_hardware_manager()->get_grinder()->is_pulse_complete() 
           && (millis() - start_time) < 2000) {
        delay(10); // Small delay to prevent busy waiting
    }
    
    // Go back to developer screen
    confirmation_ui_manager->set_current_tab(3);
    confirmation_ui_manager->switch_to_state(UIState::SETTINGS);
}

void SettingsEventHandler::handle_settings_motor_test() {
    confirmation_ui_manager = ui_manager;
    ui_manager->show_confirmation(
        "MOTOR TEST",
        "WARNING!\n\n"
        "Motor will be engaged\n"
        "for 1 second.\n\n"
        "Make sure grinder is\n"
        "safe to run.",
        LV_SYMBOL_PLAY " RUN",
        lv_color_hex(THEME_COLOR_SUCCESS),
        motor_test_callback
    );
}


void SettingsEventHandler::handle_settings_measurements_data() {
    BLE_LOG("=== MEASUREMENT DATA EXPORT ===\n");
    ui_manager->grind_controller->send_measurements_data();
    //ui_manager->grind_controller->export_measurements_csv();
    BLE_LOG("=== END MEASUREMENT DATA ===\n");
}

void SettingsEventHandler::handle_settings_ble_export() {
    if (ui_manager->bluetooth_manager->is_enabled() && ui_manager->bluetooth_manager->is_connected()) {
        BLE_LOG("=== BLE MEASUREMENT DATA EXPORT ===\n");
        ui_manager->bluetooth_manager->start_data_export();
        BLE_LOG("BLE export started - check laptop BLE receiver\n");
    } else {
        BLE_LOG("BLE not enabled or not connected - enable BLE first\n");
    }
}

void SettingsEventHandler::handle_settings_tare() {
    DEBUG_PRINTLN("Manual tare requested from developer tools");
    
    // Execute tare using unified system
    UIOperations::execute_tare(ui_manager->hardware_manager);
}

void SettingsEventHandler::handle_settings_back() {
    // Go back to the DEV tab when returning from developer mode
    ui_manager->current_tab = 3;
    ui_manager->switch_to_state(UIState::READY);
}

void SettingsEventHandler::handle_ble_toggle() {
    if (!ui_manager->bluetooth_manager) return;
    
    if (ui_manager->bluetooth_manager->is_enabled()) {
        ui_manager->bluetooth_manager->disable();
        DEBUG_PRINTLN("Bluetooth disabled by user");
    } else {
        // Use common blocking overlay for BLE enabling
        auto ble_enable_operation = [this]() {
            ui_manager->bluetooth_manager->enable();
            DEBUG_PRINTLN("Bluetooth enabled by user (30 minute timeout)");
        };
        
        auto completion = [this]() {
            // Update the developer screen immediately
            ui_manager->settings_screen.update_ble_status();
        };
        
        auto& overlay = BlockingOperationOverlay::getInstance();
        overlay.show_and_execute(BlockingOperation::BLE_ENABLING, ble_enable_operation, completion);
        return; // Exit early since update_ble_status is called in completion
    }
    
    // Update the developer screen immediately
    ui_manager->settings_screen.update_ble_status();
}

void SettingsEventHandler::handle_ble_startup_toggle() {
    if (!ui_manager->settings_screen.get_ble_startup_toggle()) return;

    // Get the current state of the toggle
    bool startup_enabled = lv_obj_has_state(ui_manager->settings_screen.get_ble_startup_toggle(), LV_STATE_CHECKED);

    // Save to preferences
    Preferences prefs;
    prefs.begin("bluetooth", false); // read-write
    prefs.putBool("startup", startup_enabled);
    prefs.end();

    DEBUG_PRINTLN(startup_enabled ? "Bluetooth startup enabled" : "Bluetooth startup disabled");
}

void SettingsEventHandler::handle_logging_toggle() {
    if (!ui_manager->settings_screen.get_logging_toggle()) return;

    // Get the current state of the toggle
    bool logging_enabled = lv_obj_has_state(ui_manager->settings_screen.get_logging_toggle(), LV_STATE_CHECKED);

    // Save to preferences
    Preferences prefs;
    prefs.begin("logging", false); // read-write
    prefs.putBool("enabled", logging_enabled);
    prefs.end();

    DEBUG_PRINTLN(logging_enabled ? "Logging enabled" : "Logging disabled");
}

void SettingsEventHandler::handle_brightness_normal_slider() {
    if (!ui_manager->hardware_manager) return;
    
    lv_obj_t* slider = ui_manager->settings_screen.get_brightness_normal_slider();
    if (!slider) return;
    
    int brightness_percent = lv_slider_get_value(slider);
    float brightness = brightness_percent / 100.0f;
    
    // Apply brightness immediately for reactive feedback
    ui_manager->hardware_manager->get_display()->set_brightness(brightness);
    
    // Update label
    ui_manager->settings_screen.update_brightness_labels();
    // Do not persist on move; commit on release only
    DEBUG_PRINTF("Normal brightness set to %d%% (%.2f)\n", brightness_percent, brightness);
}

void SettingsEventHandler::handle_brightness_normal_slider_released() {
    if (!ui_manager->hardware_manager) return;
    
    lv_obj_t* slider = ui_manager->settings_screen.get_brightness_normal_slider();
    if (!slider) return;
    
    int brightness_percent = lv_slider_get_value(slider);
    float brightness = brightness_percent / 100.0f;
    
    // Persist on release only
    Preferences prefs;
    prefs.begin("brightness", false);
    prefs.putFloat("normal", brightness);
    prefs.end();
}

void SettingsEventHandler::handle_brightness_screensaver_slider() {
    if (!ui_manager->hardware_manager) return;
    
    lv_obj_t* slider = ui_manager->settings_screen.get_brightness_screensaver_slider();
    if (!slider) return;
    
    int brightness_percent = lv_slider_get_value(slider);
    float brightness = brightness_percent / 100.0f;
    
    // Apply screensaver brightness for preview during sliding
    ui_manager->hardware_manager->get_display()->set_brightness(brightness);
    
    // Update label
    ui_manager->settings_screen.update_brightness_labels();
    
    // Do not persist on move; commit on release only
    DEBUG_PRINTF("Screensaver brightness set to %d%% (%.2f)\n", brightness_percent, brightness);
}

void SettingsEventHandler::handle_brightness_screensaver_slider_released() {
    if (!ui_manager->hardware_manager) return;
    
    // Persist screensaver brightness on release
    lv_obj_t* slider = ui_manager->settings_screen.get_brightness_screensaver_slider();
    if (slider) {
        int brightness_percent = lv_slider_get_value(slider);
        float brightness = brightness_percent / 100.0f;
        Preferences prefs;
        prefs.begin("brightness", false);
        prefs.putFloat("screensaver", brightness);
        prefs.end();
    }
    
    // Restore normal brightness when user releases the screensaver slider
    float normal_brightness = ui_manager->get_normal_brightness();
    ui_manager->hardware_manager->get_display()->set_brightness(normal_brightness);
    
    DEBUG_PRINTF("Touch released - restored normal brightness to %.2f\n", normal_brightness);
}

void SettingsEventHandler::handle_confirm() {
    // Execute the pending confirmation callback if set
    if (ui_manager->pending_confirm_callback) {
        ui_manager->pending_confirm_callback();
        ui_manager->pending_confirm_callback = nullptr;
    }
}

void SettingsEventHandler::handle_confirm_cancel() {
    // Clear any pending callback and go back to developer screen
    ui_manager->pending_confirm_callback = nullptr;
    ui_manager->current_tab = 3;
    ui_manager->switch_to_state(UIState::SETTINGS);
}

// CalibrationEventHandler implementation
void CalibrationEventHandler::handle_cal_ok() {
    CalibrationStep step = ui_manager->calibration_screen.get_step();
    
    switch (step) {
        case CAL_STEP_EMPTY:
            // Execute tare with completion callback
            UIOperations::execute_tare(ui_manager->hardware_manager, [this]() {
                ui_manager->calibration_screen.set_step(CAL_STEP_WEIGHT);
            });
            break;
            
        case CAL_STEP_WEIGHT:
            // Execute calibration with completion callback
            {
                float cal_weight = ui_manager->calibration_screen.get_calibration_weight();
                UIOperations::execute_calibration(ui_manager->hardware_manager, cal_weight, [this]() {
                    ui_manager->calibration_screen.set_step(CAL_STEP_COMPLETE);
                });
            }
            break;
            
        case CAL_STEP_COMPLETE:
            // Go back to developer mode and ensure we're on DEV tab
            ui_manager->current_tab = 3;
            ui_manager->switch_to_state(UIState::SETTINGS);
            break;
    }
}

void CalibrationEventHandler::handle_cal_cancel() {
    // Go back to developer mode and ensure we're on DEV tab
    ui_manager->current_tab = 3;
    ui_manager->switch_to_state(UIState::SETTINGS);
}

void CalibrationEventHandler::handle_cal_plus(lv_event_code_t code) {
    if (code == LV_EVENT_CLICKED) {
        float cal_weight = ui_manager->calibration_screen.get_calibration_weight();
        cal_weight = ui_manager->profile_controller->clamp_weight(cal_weight + USER_FINE_WEIGHT_ADJUSTMENT_G);
        ui_manager->calibration_screen.update_calibration_weight(cal_weight);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ui_manager->start_jog_timer(1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_manager->stop_jog_timer();
    }
}

void CalibrationEventHandler::handle_cal_minus(lv_event_code_t code) {
    if (code == LV_EVENT_CLICKED) {
        float cal_weight = ui_manager->calibration_screen.get_calibration_weight();
        cal_weight = ui_manager->profile_controller->clamp_weight(cal_weight - USER_FINE_WEIGHT_ADJUSTMENT_G);
        ui_manager->calibration_screen.update_calibration_weight(cal_weight);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ui_manager->start_jog_timer(-1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_manager->stop_jog_timer();
    }
}
