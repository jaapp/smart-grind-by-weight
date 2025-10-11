#include "settings_controller.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <cstdint>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../../logging/grind_logging.h"
#include "../../system/diagnostics_controller.h"
#include "../../system/statistics_manager.h"
#include "../components/blocking_overlay.h"
#include "../components/ui_operations.h"
#include "../event_bridge_lvgl.h"
#include "../ui_helpers.h"
#include "../ui_manager.h"

SettingsUIController::SettingsUIController(UIManager* manager)
    : ui_manager_(manager) {}

void SettingsUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    using ET = EventBridgeLVGL::EventType;

    EventBridgeLVGL::register_handler(ET::SETTINGS_CALIBRATE, [this](lv_event_t*) { handle_calibrate(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_RESET, [this](lv_event_t*) { handle_reset(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_PURGE, [this](lv_event_t*) { handle_purge(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_MOTOR_TEST, [this](lv_event_t*) { handle_motor_test(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_TARE, [this](lv_event_t*) { handle_tare(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_AUTOTUNE, [this](lv_event_t*) { handle_autotune(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_DIAGNOSTIC_RESET, [this](lv_event_t*) { handle_diagnostics_reset(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_BACK, [this](lv_event_t*) { handle_back(); });
    EventBridgeLVGL::register_handler(ET::SETTINGS_REFRESH_STATS, [this](lv_event_t*) { handle_refresh_stats(); });

    EventBridgeLVGL::register_handler(ET::BLE_TOGGLE, [this](lv_event_t*) { handle_ble_toggle(); });
    EventBridgeLVGL::register_handler(ET::BLE_STARTUP_TOGGLE, [this](lv_event_t*) { handle_ble_startup_toggle(); });
    EventBridgeLVGL::register_handler(ET::LOGGING_TOGGLE, [this](lv_event_t*) { handle_logging_toggle(); });

    EventBridgeLVGL::register_handler(ET::GRIND_MODE_SWIPE_TOGGLE, [this](lv_event_t*) { handle_grind_mode_swipe_toggle(); });
    EventBridgeLVGL::register_handler(ET::GRIND_MODE_RADIO_BUTTON, [this](lv_event_t*) { handle_grind_mode_radio_button(); });
    EventBridgeLVGL::register_handler(ET::GRIND_MODE_AUTO_START_TOGGLE, [this](lv_event_t*) { handle_auto_start_toggle(); });
    EventBridgeLVGL::register_handler(ET::GRIND_MODE_AUTO_RETURN_TOGGLE, [this](lv_event_t*) { handle_auto_return_toggle(); });

    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_NORMAL_SLIDER, [this](lv_event_t*) { handle_brightness_normal_slider(); });
    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_NORMAL_SLIDER_RELEASED, [this](lv_event_t*) { handle_brightness_normal_slider_released(); });
    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_SCREENSAVER_SLIDER, [this](lv_event_t*) { handle_brightness_screensaver_slider(); });
    EventBridgeLVGL::register_handler(ET::BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED, [this](lv_event_t*) { handle_brightness_screensaver_slider_released(); });

    auto register_lvgl_event = [](lv_obj_t* obj, lv_event_code_t code, ET type) {
        if (!obj) {
            return;
        }
        lv_obj_add_event_cb(obj, EventBridgeLVGL::dispatch_event, code,
                            reinterpret_cast<void*>(static_cast<intptr_t>(type)));
    };

    register_lvgl_event(ui_manager_->settings_screen.get_cal_button(), LV_EVENT_CLICKED, ET::SETTINGS_CALIBRATE);
    register_lvgl_event(ui_manager_->settings_screen.get_purge_button(), LV_EVENT_CLICKED, ET::SETTINGS_PURGE);
    register_lvgl_event(ui_manager_->settings_screen.get_reset_button(), LV_EVENT_CLICKED, ET::SETTINGS_RESET);
    register_lvgl_event(ui_manager_->settings_screen.get_diag_reset_button(), LV_EVENT_CLICKED, ET::SETTINGS_DIAGNOSTIC_RESET);
    register_lvgl_event(ui_manager_->settings_screen.get_motor_test_button(), LV_EVENT_CLICKED, ET::SETTINGS_MOTOR_TEST);
    register_lvgl_event(ui_manager_->settings_screen.get_tare_button(), LV_EVENT_CLICKED, ET::SETTINGS_TARE);
    register_lvgl_event(ui_manager_->settings_screen.get_autotune_button(), LV_EVENT_CLICKED, ET::SETTINGS_AUTOTUNE);
    register_lvgl_event(ui_manager_->settings_screen.get_refresh_stats_button(), LV_EVENT_CLICKED, ET::SETTINGS_REFRESH_STATS);
    register_lvgl_event(ui_manager_->settings_screen.get_ble_toggle(), LV_EVENT_VALUE_CHANGED, ET::BLE_TOGGLE);
    register_lvgl_event(ui_manager_->settings_screen.get_ble_startup_toggle(), LV_EVENT_VALUE_CHANGED, ET::BLE_STARTUP_TOGGLE);
    register_lvgl_event(ui_manager_->settings_screen.get_logging_toggle(), LV_EVENT_VALUE_CHANGED, ET::LOGGING_TOGGLE);
    register_lvgl_event(ui_manager_->settings_screen.get_grind_mode_swipe_toggle(), LV_EVENT_VALUE_CHANGED, ET::GRIND_MODE_SWIPE_TOGGLE);
    register_lvgl_event(ui_manager_->settings_screen.get_auto_start_toggle(), LV_EVENT_VALUE_CHANGED, ET::GRIND_MODE_AUTO_START_TOGGLE);
    register_lvgl_event(ui_manager_->settings_screen.get_auto_return_toggle(), LV_EVENT_VALUE_CHANGED, ET::GRIND_MODE_AUTO_RETURN_TOGGLE);
    register_lvgl_event(ui_manager_->settings_screen.get_brightness_normal_slider(), LV_EVENT_VALUE_CHANGED, ET::BRIGHTNESS_NORMAL_SLIDER);
    register_lvgl_event(ui_manager_->settings_screen.get_brightness_normal_slider(), LV_EVENT_RELEASED, ET::BRIGHTNESS_NORMAL_SLIDER_RELEASED);
    register_lvgl_event(ui_manager_->settings_screen.get_brightness_screensaver_slider(), LV_EVENT_VALUE_CHANGED, ET::BRIGHTNESS_SCREENSAVER_SLIDER);
    register_lvgl_event(ui_manager_->settings_screen.get_brightness_screensaver_slider(), LV_EVENT_RELEASED, ET::BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED);
}

void SettingsUIController::update() {
    if (!ui_manager_) {
        return;
    }

    WeightSensor* sensor = ui_manager_->hardware_manager->get_weight_sensor();
    unsigned long uptime_ms = millis();
    size_t free_heap = ESP.getFreeHeap();

    ui_manager_->settings_screen.update_info(sensor, uptime_ms, free_heap);
    ui_manager_->settings_screen.update_diagnostics(sensor);
    ui_manager_->settings_screen.update_ble_status();
}

void SettingsUIController::handle_calibrate() {
    if (ui_manager_) {
        ui_manager_->switch_to_state(UIState::CALIBRATION);
    }
}

void SettingsUIController::handle_reset() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "FACTORY RESET",
        "This will reset all settings to factory defaults:\n\n"
        "• Profile weights\n"
        "• Calibration data\n"
        "• Grind history\n"
        "• Lifetime statistics\n\n"
        "This action cannot be undone.",
        "RESET",
        lv_color_hex(THEME_COLOR_ERROR),
        [this]() { perform_factory_reset(); },
        "CANCEL",
        [this]() { return_to_settings(); }
    );
}

void SettingsUIController::handle_purge() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "PURGE LOGS",
        "This will remove all saved grind log files from flash.\n"
        "Lifetime statistics will be preserved."
        "\n\n"
        "This action cannot be undone.",
        "PURGE LOGS",
        lv_color_hex(THEME_COLOR_ERROR),
        [this]() { execute_purge_operation(); },
        "CANCEL",
        [this]() { return_to_settings(); }
    );
}

void SettingsUIController::handle_motor_test() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "MOTOR TEST",
        "Motor will be engaged for 1 second."
        "\n\n"
        "Make sure grinder is safe to run.",
        "RUN",
        lv_color_hex(THEME_COLOR_SUCCESS),
        [this]() { run_motor_test(); },
        "CANCEL",
        [this]() { return_to_settings(); }
    );
}

void SettingsUIController::handle_tare() {
    if (!ui_manager_) return;
    UIOperations::execute_tare(ui_manager_->get_hardware_manager(), [this]() {
        if (ui_manager_) {
            ui_manager_->refresh_auto_action_settings();
        }
    });
}

void SettingsUIController::handle_autotune() {
    if (!ui_manager_) return;

    // Show confirmation screen with setup instructions
    auto autotune_controller = ui_manager_->autotune_controller_.get();
    if (autotune_controller) {
        ui_manager_->show_confirmation(
            "Auto-Tune Setup",
            "Before starting:\n\n"
            "- Beans loaded\n"
            "- Cup on scale\n\n"
            "Process takes ~1 min.",
            "START",
            lv_color_hex(THEME_COLOR_ACCENT),
            [autotune_controller]() { autotune_controller->confirm_and_begin(); },
            "CANCEL",
            [this]() { return_to_settings(); }
        );
    }
}

void SettingsUIController::handle_back() {
    if (!ui_manager_) return;
    ui_manager_->set_current_tab(3);
    ui_manager_->switch_to_state(UIState::READY);
}

void SettingsUIController::handle_refresh_stats() {
    if (!ui_manager_) return;
    ui_manager_->settings_screen.refresh_statistics();
}

void SettingsUIController::handle_diagnostics_reset() {
    if (!ui_manager_) return;

    ui_manager_->show_confirmation(
        "Reset Diagnostics",
        "This will clear all active diagnostic warnings.\n\nContinue?",
        "RESET",
        lv_color_hex(THEME_COLOR_WARNING),
        [this]() { perform_diagnostics_reset(); },
        "CANCEL",
        [this]() { return_to_settings(); }
    );
}

void SettingsUIController::perform_diagnostics_reset() {
    if (!ui_manager_) return;

    auto* diagnostics = ui_manager_->diagnostics_controller_.get();
    if (diagnostics) {
        diagnostics->reset_diagnostic(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED);
        diagnostics->reset_diagnostic(DiagnosticCode::MECHANICAL_INSTABILITY);
        diagnostics->reset_noise_tracking();
    }

    auto* grind_controller = ui_manager_->get_grind_controller();
    if (grind_controller) {
        grind_controller->reset_mechanical_anomaly_count();
    }

    auto* hardware = ui_manager_->get_hardware_manager();
    auto* sensor = hardware ? hardware->get_weight_sensor() : nullptr;
    if (sensor) {
        ui_manager_->settings_screen.update_diagnostics(sensor);
    }
}

void SettingsUIController::handle_ble_toggle() {
    if (!ui_manager_ || !ui_manager_->bluetooth_manager) return;

    auto* ble = ui_manager_->bluetooth_manager;
    if (ble->is_enabled()) {
        ble->disable();
        LOG_DEBUG_PRINTLN("Bluetooth disabled by user");
        ui_manager_->settings_screen.update_ble_status();
        return;
    }

    auto completion = [this]() {
        ui_manager_->settings_screen.update_ble_status();
    };

    auto operation = [ble]() {
        ble->enable();
        LOG_DEBUG_PRINTLN("Bluetooth enabled by user (30 minute timeout)");
    };

    auto& overlay = BlockingOperationOverlay::getInstance();
    overlay.show_and_execute(BlockingOperation::BLE_ENABLING, operation, completion);
}

void SettingsUIController::handle_ble_startup_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->settings_screen.get_ble_startup_toggle();
    if (!toggle) return;

    bool startup_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("bluetooth", false);
    prefs.putBool("startup", startup_enabled);
    prefs.end();

    LOG_DEBUG_PRINTLN(startup_enabled ? "Bluetooth startup enabled" : "Bluetooth startup disabled");
}

void SettingsUIController::handle_logging_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->settings_screen.get_logging_toggle();
    if (!toggle) return;

    bool logging_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("logging", false);
    prefs.putBool("enabled", logging_enabled);
    prefs.end();

    LOG_DEBUG_PRINTLN(logging_enabled ? "Logging enabled" : "Logging disabled");
}

void SettingsUIController::handle_grind_mode_swipe_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->settings_screen.get_grind_mode_swipe_toggle();
    if (!toggle) return;

    bool swipe_enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("swipe", false);
    prefs.putBool("enabled", swipe_enabled);
    prefs.end();

    LOG_DEBUG_PRINTLN(swipe_enabled ? "Grind mode swipe gestures enabled" : "Grind mode swipe gestures disabled");
}

void SettingsUIController::handle_grind_mode_radio_button() {
    if (!ui_manager_ || !ui_manager_->profile_controller) return;

    lv_obj_t* radio_group = ui_manager_->settings_screen.get_grind_mode_radio_group();
    if (!radio_group) return;

    int selected_index = radio_button_group_get_selection(radio_group);
    if (selected_index < 0) return;

    GrindMode new_mode = (selected_index == 0) ? GrindMode::WEIGHT : GrindMode::TIME;
    ui_manager_->profile_controller->set_grind_mode(new_mode);
    ui_manager_->current_mode = new_mode;
    if (ui_manager_->ready_controller_) {
        ui_manager_->ready_controller_->refresh_profiles();
    }
    ui_manager_->edit_target = get_current_profile_target(*ui_manager_->profile_controller, new_mode);
    if (ui_manager_->state_machine->is_state(UIState::EDIT)) {
        if (ui_manager_->edit_controller_) {
            ui_manager_->edit_controller_->update_display();
        }
    }

    LOG_DEBUG_PRINTLN(selected_index == 0 ? "Grind mode set to WEIGHT via radio button" : "Grind mode set to TIME via radio button");
}

void SettingsUIController::handle_auto_start_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->settings_screen.get_auto_start_toggle();
    if (!toggle) return;

    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("autogrind", false);
    prefs.putBool("auto_start", enabled);
    prefs.end();

    if (ui_manager_) {
        ui_manager_->refresh_auto_action_settings();
    }

    LOG_DEBUG_PRINTLN(enabled ? "Auto-start on cup enabled" : "Auto-start on cup disabled");
}

void SettingsUIController::handle_auto_return_toggle() {
    if (!ui_manager_) return;

    auto* toggle = ui_manager_->settings_screen.get_auto_return_toggle();
    if (!toggle) return;

    bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    Preferences prefs;
    prefs.begin("autogrind", false);
    prefs.putBool("auto_return", enabled);
    prefs.end();

    if (ui_manager_) {
        ui_manager_->refresh_auto_action_settings();
    }

    LOG_DEBUG_PRINTLN(enabled ? "Auto return on cup removal enabled" : "Auto return on cup removal disabled");
}

void SettingsUIController::handle_brightness_normal_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->settings_screen.get_brightness_normal_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    ui_manager_->get_hardware_manager()->get_display()->set_brightness(brightness);
    ui_manager_->settings_screen.update_brightness_labels(brightness_percent, -1);
    LOG_DEBUG_PRINTF("Normal brightness set to %d%% (%.2f)\n", brightness_percent, brightness);
}

void SettingsUIController::handle_brightness_normal_slider_released() {
    auto* slider = ui_manager_->settings_screen.get_brightness_normal_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    Preferences prefs;
    prefs.begin("brightness", false);
    prefs.putFloat("normal", brightness);
    prefs.end();
}

void SettingsUIController::handle_brightness_screensaver_slider() {
    if (!ui_manager_) return;

    auto* slider = ui_manager_->settings_screen.get_brightness_screensaver_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    ui_manager_->get_hardware_manager()->get_display()->set_brightness(brightness);
    ui_manager_->settings_screen.update_brightness_labels(-1, brightness_percent);
    LOG_DEBUG_PRINTF("Screensaver brightness set to %d%% (%.2f)\n", brightness_percent, brightness);
}

void SettingsUIController::handle_brightness_screensaver_slider_released() {
    auto* slider = ui_manager_->settings_screen.get_brightness_screensaver_slider();
    if (!slider) return;

    int brightness_percent = lv_slider_get_value(slider);
    if (brightness_percent < HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT) {
        brightness_percent = HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT;
        lv_slider_set_value(slider, brightness_percent, LV_ANIM_OFF);
    }
    float brightness = brightness_percent / 100.0f;

    Preferences prefs;
    prefs.begin("brightness", false);
    prefs.putFloat("screensaver", brightness);
    prefs.end();

    float normal = get_normal_brightness();
    ui_manager_->get_hardware_manager()->get_display()->set_brightness(normal);
    LOG_DEBUG_PRINTF("Touch released - restored normal brightness to %.2f\n", normal);
}

void SettingsUIController::perform_factory_reset() {
    if (!ui_manager_) return;

    LOG_DEBUG_PRINTLN("Factory reset: clearing NVS preferences and rebooting...");

    nvs_flash_deinit();
    esp_err_t erase_result = nvs_flash_erase();

    if (erase_result == ESP_OK) {
        LOG_DEBUG_PRINTLN("Factory reset: NVS erase successful. Restarting device...");
    } else {
        LOG_DEBUG_PRINTF("Factory reset: NVS erase failed (code %d). Forcing restart...\n",
                         static_cast<int>(erase_result));
    }

    delay(100);
    esp_restart();
}

void SettingsUIController::execute_purge_operation() {
    if (!ui_manager_) return;

    auto completion = [this]() {
        return_to_settings();
        ui_manager_->settings_screen.refresh_statistics(false);
    };

    auto purge_task = []() {
        LOG_DEBUG_PRINTLN("\n=== PURGE GRIND LOGS INITIATED ===");
        extern GrindLogger grind_logger;
        bool success = grind_logger.clear_all_sessions_from_flash();
        if (success) {
            LOG_DEBUG_PRINTLN("Grind logs purged successfully - reinitializing logger...");
        } else {
            LOG_DEBUG_PRINTLN("ERROR: Failed to purge all grind log data!");
        }
    };

    auto& overlay = BlockingOperationOverlay::getInstance();
    overlay.show_and_execute(BlockingOperation::CUSTOM, purge_task, completion,
                             "PURGING LOGS...\nPlease wait");
}

void SettingsUIController::run_motor_test() {
    if (!ui_manager_) return;

    auto* grinder = ui_manager_->get_hardware_manager()->get_grinder();
    if (!grinder) return;

    ui_manager_->set_background_active(true);
    grinder->start_pulse_rmt(1000);

    // Update statistics for motor test (1000ms = 1 second)
    statistics_manager.update_motor_test(1000);

    stop_motor_timer();
    motor_timer_ = lv_timer_create(static_motor_timer_cb, 2000, this);
    if (motor_timer_) {
        lv_timer_set_user_data(motor_timer_, this);
    }
}

void SettingsUIController::return_to_settings() {
    if (!ui_manager_) return;
    ui_manager_->set_current_tab(3);
    ui_manager_->switch_to_state(UIState::SETTINGS);
}

float SettingsUIController::get_normal_brightness() const {
    if (!ui_manager_ || !ui_manager_->hardware_manager) {
        return USER_SCREEN_BRIGHTNESS_NORMAL;
    }

    Preferences prefs;
    prefs.begin("brightness", true);
    float brightness = prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
    prefs.end();

    if (brightness < 0.15f) {
        brightness = 0.15f;
    }
    return brightness;
}

float SettingsUIController::get_screensaver_brightness() const {
    if (!ui_manager_ || !ui_manager_->hardware_manager) {
        return USER_SCREEN_BRIGHTNESS_DIMMED;
    }

    Preferences prefs;
    prefs.begin("brightness", true);
    float brightness = prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
    prefs.end();

    if (brightness < 0.15f) {
        brightness = 0.15f;
    }
    return brightness;
}

void SettingsUIController::stop_motor_timer() {
    if (motor_timer_) {
        lv_timer_del(motor_timer_);
        motor_timer_ = nullptr;
    }
}

void SettingsUIController::motor_timer_cb(lv_timer_t* timer) {
    if (!ui_manager_) {
        return;
    }

    auto* grinder = ui_manager_->get_hardware_manager()->get_grinder();
    if (grinder && !grinder->is_pulse_complete()) {
        grinder->stop();
    }

    stop_motor_timer();
    ui_manager_->set_background_active(false);
    return_to_settings();
}

void SettingsUIController::static_motor_timer_cb(lv_timer_t* timer) {
    if (!timer) {
        return;
    }
    auto* controller = static_cast<SettingsUIController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->motor_timer_cb(timer);
    }
}
