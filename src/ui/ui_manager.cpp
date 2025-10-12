#include "ui_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include "../config/constants.h"
#include "screens/calibration_screen.h"
#include "../logging/grind_logging.h"
#include "../controllers/grind_mode_traits.h"
#include <utility>
// Static instance pointer for grind event callbacks
UIManager* UIManager::instance = nullptr;

UIManager::~UIManager() = default;

void UIManager::init(HardwareManager* hw_mgr, StateMachine* sm, 
                     ProfileController* pc, GrindController* gc, BluetoothManager* bluetooth) {
    hardware_manager = hw_mgr;
    state_machine = sm;
    profile_controller = pc;
    grind_controller = gc;
    bluetooth_manager = bluetooth;
    
    // Set static instance for event callbacks
    instance = this;
    
    edit_target = 0.0f;
    original_target = 0.0f;
    calibration_weight = USER_CALIBRATION_REFERENCE_WEIGHT_G;
    current_tab = profile_controller->get_current_profile();
    current_mode = profile_controller->get_grind_mode();
    jog_timer = nullptr;
    // Initialize the unified overlay system
    BlockingOperationOverlay::getInstance().init();
    jog_start_time = 0;
    jog_stage = 1;
    
    // Initialize controller scaffolding (instances only)
    init_controllers();

    create_ui();

    // Register controller event hooks now that the UI elements exist
    register_controller_events();

    refresh_auto_action_settings();

    if (grind_controller) {
        grind_controller->set_diagnostics_controller(diagnostics_controller_.get());
    }
    
    // Set initial brightness from preferences
    float initial_brightness = USER_SCREEN_BRIGHTNESS_NORMAL;
    if (settings_controller_) {
        initial_brightness = settings_controller_->get_normal_brightness();
    }
    hardware_manager->get_display()->set_brightness(initial_brightness);
    
    // Register grind event callback
    grind_controller->set_ui_event_callback(GrindingUIController::dispatch_event);
    
    
    initialized = true;
}

void UIManager::create_ui() {
    // Set background style
    static lv_style_t style_screen;
    lv_style_init(&style_screen);
#if defined(DEBUG_ENABLE_LOADCELL_MOCK) && (DEBUG_ENABLE_LOADCELL_MOCK != 0)
    lv_style_set_bg_color(&style_screen, lv_color_hex(THEME_COLOR_BACKGROUND_MOCK));
#else
    lv_style_set_bg_color(&style_screen, lv_color_hex(THEME_COLOR_BACKGROUND));
#endif
    lv_obj_add_style(lv_scr_act(), &style_screen, 0);

    // Create all screens
    ready_screen.create();
    edit_screen.create();
    grinding_screen.init(hardware_manager->get_preferences());
    grinding_screen.create();
    grinding_screen.set_mode(current_mode);
    settings_screen.create(bluetooth_manager, grind_controller, &grinding_screen, hardware_manager, diagnostics_controller_.get());
    calibration_screen.create();
    confirm_screen.create();
    autotune_screen.create();
    ota_screen.create();
    ota_update_failed_screen.create();
    
    if (ready_controller_) {
        ready_controller_->refresh_profiles();
    }

    if (grinding_controller_) {
        grinding_controller_->build_controls();
    }

    if (status_indicator_controller_) {
        status_indicator_controller_->build();
    }
    
    // Set up initial state
    ready_screen.hide();
    edit_screen.hide();
    grinding_screen.hide();
    settings_screen.hide();
    calibration_screen.hide();
    confirm_screen.hide();
    autotune_screen.hide();
    ota_screen.hide();
    ota_update_failed_screen.hide();
    
    // Initialize UI to current state (set by state_machine during boot)
    switch_to_state(state_machine->get_current_state());
}

void UIManager::update() {
    if (!initialized) return;

    // Update diagnostics controller
    if (diagnostics_controller_) {
        diagnostics_controller_->update(hardware_manager, grind_controller, millis());
    }

    if (screen_timeout_controller_) {
        screen_timeout_controller_->update();
    }

    bool ota_cycle_consumed = false;
    if (ota_data_export_controller_) {
        ota_cycle_consumed = ota_data_export_controller_->update();
    }

    if (ota_cycle_consumed) {
        return;
    }
    
    // Update based on current state
    UIState current = state_machine->get_current_state();
    
    switch (current) {
        case UIState::GRINDING:
            // Event-driven updates - no polling needed
            break;
            
        case UIState::SETTINGS:
            if (settings_controller_) {
                settings_controller_->update();
            }
            break;
            
        case UIState::CALIBRATION:
            if (calibration_controller_) {
                calibration_controller_->update();
            }
            break;

        case UIState::AUTOTUNING:
            if (autotune_controller_) {
                autotune_controller_->update();
            }
            break;

        case UIState::READY:
            // Ready state - no special handling needed
            break;
            
        default:
            break;
    }

    update_auto_actions();

    if (grinding_controller_) {
        grinding_controller_->update(current);
    }

    if (status_indicator_controller_) {
        status_indicator_controller_->update();
    }
}

void UIManager::switch_to_state(UIState new_state) {
    state_machine->transition_to(new_state);

    // Hide all screens before showing the requested one
    ready_screen.hide();
    edit_screen.hide();
    grinding_screen.hide();
    settings_screen.hide();
    calibration_screen.hide();
    confirm_screen.hide();
    autotune_screen.hide();
    ota_screen.hide();
    ota_update_failed_screen.hide();

    switch (new_state) {
        case UIState::READY:
            ready_screen.show();
            ready_screen.set_active_tab(current_tab);
            grinding_screen.set_mode(current_mode);
            if (ready_controller_) {
                ready_controller_->refresh_profiles();
            }
            break;

        case UIState::EDIT:
            edit_screen.show();
            edit_screen.update_profile_name(profile_controller->get_current_name());
            edit_screen.set_mode(current_mode);
            edit_screen.update_target(edit_target);
            break;

        case UIState::GRINDING:
            LOG_UI_DEBUG("[%lums UI_SCREEN_VISIBLE] GRINDING screen showing\n", millis());
            grinding_screen.show();
            break;

        case UIState::GRIND_COMPLETE:
            grinding_screen.show();
            break;

        case UIState::GRIND_TIMEOUT:
            grinding_screen.show();
            break;

        case UIState::SETTINGS:
            settings_screen.show();
            break;

        case UIState::CALIBRATION: {
            float saved_cal_weight = hardware_manager->get_weight_sensor()->get_saved_calibration_weight();
            calibration_screen.show();
            calibration_screen.set_step(CAL_STEP_EMPTY);
            calibration_screen.update_calibration_weight(saved_cal_weight);
            break;
        }

        case UIState::CONFIRM:
            confirm_screen.show();
            break;

        case UIState::AUTOTUNING:
            autotune_screen.show();
            break;

        case UIState::OTA_UPDATE:
            ota_screen.show();
            ota_screen.update_progress(0);
            break;

        case UIState::OTA_UPDATE_FAILED:
            if (ota_data_export_controller_) {
                ota_data_export_controller_->show_failure_screen();
            }
            break;
    }

    if (grinding_controller_) {
        grinding_controller_->on_state_changed(new_state);
        grinding_controller_->update_grind_button_icon();
    }
}

void UIManager::show_confirmation(const char* title, const char* message, 
                                 const char* confirm_text, lv_color_t confirm_color,
                                 std::function<void()> on_confirm,
                                 const char* cancel_text,
                                 std::function<void()> on_cancel) {
    if (confirm_controller_) {
        confirm_controller_->show(title, message, confirm_text, confirm_color,
                                  std::move(on_confirm), cancel_text, std::move(on_cancel));
    }
}

void UIManager::init_controllers() {
    ready_controller_ = std::make_unique<ReadyUIController>(this);
    edit_controller_ = std::make_unique<EditUIController>(this);
    grinding_controller_ = std::make_unique<GrindingUIController>(this);
    settings_controller_ = std::make_unique<SettingsUIController>(this);
    status_indicator_controller_ = std::make_unique<StatusIndicatorController>(this);
    calibration_controller_ = std::make_unique<CalibrationUIController>(this);
    autotune_controller_ = std::make_unique<AutoTuneUIController>(this);
    confirm_controller_ = std::make_unique<ConfirmUIController>(this);
    ota_data_export_controller_ = std::make_unique<OtaDataExportController>(this);
    screen_timeout_controller_ = std::make_unique<ScreenTimeoutController>(this);
    jog_adjust_controller_ = std::make_unique<JogAdjustController>(this);
    diagnostics_controller_ = std::make_unique<DiagnosticsController>();

    // Initialize diagnostics controller
    if (diagnostics_controller_) {
        diagnostics_controller_->init(hardware_manager);
    }
}

void UIManager::register_controller_events() {
    EventBridgeLVGL::set_ui_manager(this);
    if (ready_controller_) ready_controller_->register_events();
    if (edit_controller_) edit_controller_->register_events();
    if (grinding_controller_) grinding_controller_->register_events();
    if (settings_controller_) settings_controller_->register_events();
    if (calibration_controller_) calibration_controller_->register_events();
    if (autotune_controller_) autotune_controller_->register_events();
    if (confirm_controller_) confirm_controller_->register_events();
    if (ota_data_export_controller_) ota_data_export_controller_->register_events();
    if (screen_timeout_controller_) screen_timeout_controller_->register_events();
    if (jog_adjust_controller_) jog_adjust_controller_->register_events();
}

void UIManager::set_background_active(bool active) {
#if DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR
    static lv_style_t style_bg;
    static bool style_initialized = false;

    if (!style_initialized) {
        lv_style_init(&style_bg);
        style_initialized = true;
    }

#if defined(DEBUG_ENABLE_LOADCELL_MOCK) && (DEBUG_ENABLE_LOADCELL_MOCK != 0)
    lv_color_t inactive_color = lv_color_hex(THEME_COLOR_BACKGROUND_MOCK);
#else
    lv_color_t inactive_color = lv_color_hex(THEME_COLOR_BACKGROUND);
#endif
    lv_color_t bg_color = active ? lv_color_hex(THEME_COLOR_GRINDER_ACTIVE) : inactive_color;
    lv_style_set_bg_color(&style_bg, bg_color);
    lv_obj_add_style(lv_scr_act(), &style_bg, 0);
#endif
}

void UIManager::refresh_auto_action_settings() {
    Preferences prefs;
    prefs.begin("autogrind", true);
    auto_actions_.auto_start_enabled = prefs.getBool("auto_start", false);
    auto_actions_.auto_return_enabled = prefs.getBool("auto_return", false);
    prefs.end();

    uint32_t now = millis();
    auto_actions_.last_auto_start_ms = now;
    auto_actions_.last_auto_return_ms = now;
}

void UIManager::update_auto_actions() {
    if ((!auto_actions_.auto_start_enabled && !auto_actions_.auto_return_enabled) ||
        !hardware_manager || !state_machine) {
        return;
    }

    auto* sensor = hardware_manager->get_weight_sensor();
    if (!sensor || !sensor->data_ready() || sensor->is_tare_in_progress()) {
        return;
    }

    const UIState current_state = state_machine->get_current_state();
    if (current_state == UIState::GRINDING || current_state == UIState::CALIBRATION) {
        return;
    }

    const uint32_t now = millis();
    const bool grinder_active = (grind_controller && grind_controller->is_active());
    const bool on_ready_tab = state_machine->is_state(UIState::READY) && current_tab < 3;

    if (auto_actions_.auto_start_enabled && on_ready_tab && !grinder_active && grinding_controller_) {
        auto* filter = sensor->get_raw_filter();

        // Extended window = settling period + trigger window
        constexpr uint32_t kExtendedWindow = USER_AUTO_GRIND_TRIGGER_SETTLING_MS + USER_AUTO_GRIND_TRIGGER_WINDOW_MS;

        if (filter && filter->get_buffer_time_span_ms() >= kExtendedWindow) {
            constexpr int kBaseSampleRequirement =
                (HW_LOADCELL_SAMPLE_RATE_SPS * kExtendedWindow) / 1000;
            constexpr int kMinSamplesForWindow = (kBaseSampleRequirement > 2) ? kBaseSampleRequirement : 2;

            if (sensor->get_sample_count() >= kMinSamplesForWindow) {
                // Check settled state first (cheap) to short-circuit expensive delta calculation
                if (sensor->is_settled()) {
                    float delta_g = 0.0f;
                    int samples_used = 0;
                    uint32_t span_ms = 0;

                    // Weight is settled - now check delta over extended window
                    if (sensor->get_weight_delta(kExtendedWindow, &delta_g, &samples_used, &span_ms) &&
                        samples_used >= kMinSamplesForWindow &&
                        span_ms <= kExtendedWindow &&
                        delta_g >= USER_AUTO_GRIND_TRIGGER_DELTA_G) {

                        const bool rearm_ready =
                            (now - auto_actions_.last_auto_start_ms) >= USER_AUTO_GRIND_REARM_DELAY_MS;

                        if (rearm_ready) {
                            LOG_BLE("[AUTO ACTION] Trigger confirmed: %.1fg over %lums with settled weight - auto-starting grind\n",
                                    static_cast<double>(delta_g),
                                    static_cast<unsigned long>(span_ms));
                            auto_actions_.last_auto_start_ms = now;
                            grinding_controller_->handle_grind_button();
                        }
                    }
                }
            }
        }
    }

    if (!auto_actions_.auto_return_enabled) {
        return;
    }

    if (state_machine->is_state(UIState::GRIND_COMPLETE) ||
        state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        constexpr float kCompleteExitThresholdG = 2.0f;  // Treat scale as empty once weight drops below this point
        const float live_weight = sensor->get_weight_low_latency();
        const bool rearm_ready =
            (now - auto_actions_.last_auto_return_ms) >= USER_AUTO_GRIND_REARM_DELAY_MS;

        if (live_weight <= kCompleteExitThresholdG && rearm_ready) {
            LOG_BLE("[AUTO ACTION] Detected near-empty scale - returning to ready screen\n");
            auto_actions_.last_auto_return_ms = now;
            if (grind_controller) {
                grind_controller->return_to_idle();
            }
        }
    }
}
