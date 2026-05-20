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
                     ProfileController* pc, GrindController* gc, ConnectivityManager* connectivity) {
    hardware_manager = hw_mgr;
    state_machine = sm;
    profile_controller = pc;
    grind_controller = gc;
    connectivity_manager = connectivity;
    
    // Set static instance for event callbacks
    instance = this;
    basket_detector_.init(hardware_manager ? hardware_manager->get_preferences() : nullptr);
    
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
    if (menu_controller_) {
        initial_brightness = menu_controller_->get_normal_brightness();
    }
    hardware_manager->get_display()->set_brightness(initial_brightness);
    
    // Register grind event callback
    grind_controller->set_ui_event_callback(GrindingUIController::dispatch_event);

    // Show startup screensaver if enabled and image exists
    if (screensaver_controller_ &&
        screensaver_controller_->is_startup_enabled() &&
        screensaver_controller_->has_image()) {
        screensaver_controller_->show();
        // Auto-hide after the configured startup duration
        lv_timer_create([](lv_timer_t* t) {
            auto* sc = static_cast<ScreensaverController*>(lv_timer_get_user_data(t));
            if (sc && sc->is_visible()) {
                sc->hide();
            }
            lv_timer_delete(t);
        }, screensaver_controller_->get_startup_duration_ms(), screensaver_controller_.get());
    }

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
    menu_screen.create(connectivity_manager, grind_controller, &basket_detector_, &grinding_screen, hardware_manager, diagnostics_controller_.get());
    calibration_screen.create();
    confirm_screen.create();
    purge_confirm_screen.create();
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
    menu_screen.hide();
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

    apply_connectivity_settings_changes();

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
            
        case UIState::MENU:
            if (menu_controller_) {
                menu_controller_->update();
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

void UIManager::apply_connectivity_settings_changes() {
    if (!connectivity_manager || !connectivity_manager->consume_settings_changed()) {
        return;
    }

    if (profile_controller) {
        profile_controller->load_profiles();
        current_tab = profile_controller->get_current_profile();
        current_mode = profile_controller->get_grind_mode();
        edit_target = get_current_profile_target(*profile_controller, current_mode);
    }

    if (ready_controller_) {
        ready_controller_->refresh_profiles();
    }
    if (grind_controller) {
        grind_controller->load_coast_ratio();
    }
    grinding_screen.set_mode(current_mode);
    menu_screen.update_brightness_sliders();
    menu_screen.update_connectivity_startup_toggle();
    menu_screen.update_logging_toggle();
    menu_screen.update_grind_mode_toggles();
    menu_screen.update_screensaver_toggles();
    if (screen_timeout_controller_) {
        screen_timeout_controller_->reload_settings();
    }
    refresh_auto_action_settings();

    if (hardware_manager && menu_controller_) {
        hardware_manager->get_display()->set_brightness(menu_controller_->get_normal_brightness());
    }
}

void UIManager::switch_to_state(UIState new_state) {
    state_machine->transition_to(new_state);
    if (screensaver_controller_ && screensaver_controller_->is_visible()) {
        screensaver_controller_->hide();
    }

    // Hide all screens before showing the requested one
    ready_screen.hide();
    edit_screen.hide();
    grinding_screen.hide();
    menu_screen.hide();
    calibration_screen.hide();
    confirm_screen.hide();
    purge_confirm_screen.hide();
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

        case UIState::MENU:
            menu_screen.show();
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

        case UIState::PURGE_CONFIRM: {
            // Calculate and set dynamic message based on elapsed time
            char message_buffer[128];
            if (grind_controller && hardware_manager) {
                if (!grind_controller->get_grinder_purged_since_boot()) {
                    // First grind since boot
                    snprintf(message_buffer, sizeof(message_buffer),
                             "Previous grind time unknown. Remove the purge grinds if desired.");
                } else {
                    // Calculate elapsed time
                    uint64_t current_ms = esp_timer_get_time() / 1000;
                    uint64_t last_purge_ms = grind_controller->get_last_purge_runtime_ms();
                    uint64_t elapsed_ms = current_ms - last_purge_ms;
                    float elapsed_hours = elapsed_ms / 3600000.0f;
                    int hours = (int)elapsed_hours;

                    snprintf(message_buffer, sizeof(message_buffer),
                             "Last grind >%dh ago. Remove the purge grinds if desired.", hours);
                }
            } else {
                // Fallback message
                snprintf(message_buffer, sizeof(message_buffer),
                         "Remove the purge grinds if desired.");
            }

            purge_confirm_screen.set_message(message_buffer);
            purge_confirm_screen.show();
            break;
        }

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
    menu_controller_ = std::make_unique<MenuUIController>(this);
    status_indicator_controller_ = std::make_unique<StatusIndicatorController>(this);
    calibration_controller_ = std::make_unique<CalibrationUIController>(this);
    autotune_controller_ = std::make_unique<AutoTuneUIController>(this);
    confirm_controller_ = std::make_unique<ConfirmUIController>(this);
    ota_data_export_controller_ = std::make_unique<OtaDataExportController>(this);
    screen_timeout_controller_ = std::make_unique<ScreenTimeoutController>(this);
    screensaver_controller_ = std::make_unique<ScreensaverController>();
    jog_adjust_controller_ = std::make_unique<JogAdjustController>(this);
    diagnostics_controller_ = std::make_unique<DiagnosticsController>();

    // Initialize diagnostics controller
    if (diagnostics_controller_) {
        diagnostics_controller_->init(hardware_manager);
    }

    // Wire screensaver controller into screen timeout controller
    if (screen_timeout_controller_ && screensaver_controller_) {
        screen_timeout_controller_->set_screensaver_controller(screensaver_controller_.get());
    }
}

void UIManager::register_controller_events() {
    EventBridgeLVGL::set_ui_manager(this);
    if (ready_controller_) ready_controller_->register_events();
    if (edit_controller_) edit_controller_->register_events();
    if (grinding_controller_) grinding_controller_->register_events();
    if (menu_controller_) menu_controller_->register_events();
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
    cancel_pending_basket_auto_start();
    auto_actions_.basket_placement_latched = false;

    Preferences prefs;
    prefs.begin("autogrind", true);
    auto_actions_.auto_start_enabled = prefs.getBool("auto_start", false);
    auto_actions_.auto_return_enabled = prefs.getBool("auto_return", false);
    prefs.end();

    uint32_t now = millis();
    auto_actions_.last_auto_start_ms = now;
    auto_actions_.last_auto_return_ms = now;
}

void UIManager::schedule_basket_auto_start(int profile_index, const char* status_text) {
    cancel_pending_basket_auto_start();

    if (!profile_controller || !ready_controller_ || !grinding_controller_) {
        return;
    }

    profile_controller->set_current_profile(profile_index);
    current_tab = profile_index;
    edit_target = get_current_profile_target(*profile_controller, current_mode);
    ready_screen.set_active_tab(profile_index);
    ready_controller_->refresh_profiles();
    grinding_controller_->update_grind_button_icon();
    ready_screen.show_transient_status(status_text, USER_BASKET_DETECTION_CONFIRM_MS);

    auto_actions_.basket_start_pending = true;
    auto_actions_.pending_basket_profile = profile_index;
    auto_actions_.basket_start_timer = lv_timer_create(basket_auto_start_timer_cb,
                                                       USER_BASKET_DETECTION_CONFIRM_MS,
                                                       this);
    if (auto_actions_.basket_start_timer) {
        lv_timer_set_repeat_count(auto_actions_.basket_start_timer, 1);
    }
}

void UIManager::complete_pending_basket_auto_start() {
    auto_actions_.basket_start_timer = nullptr;

    const int profile_index = auto_actions_.pending_basket_profile;
    auto_actions_.basket_start_pending = false;
    auto_actions_.pending_basket_profile = -1;

    if (!state_machine || !grinding_controller_ || !hardware_manager || profile_index < 0) {
        return;
    }

    auto* sensor = hardware_manager->get_weight_sensor();
    const bool grinder_active = (grind_controller && grind_controller->is_active());
    if (!sensor || !state_machine->is_state(UIState::READY) || grinder_active || current_tab != profile_index) {
        return;
    }

    const float live_weight = sensor->get_weight_low_latency();
    if (std::fabs(live_weight) <= USER_BASKET_DETECTION_REMOVAL_THRESHOLD_G) {
        ready_screen.clear_status();
        return;
    }

    grinding_controller_->handle_grind_button();
}

void UIManager::cancel_pending_basket_auto_start() {
    if (auto_actions_.basket_start_timer) {
        lv_timer_del(auto_actions_.basket_start_timer);
        auto_actions_.basket_start_timer = nullptr;
    }
    auto_actions_.basket_start_pending = false;
    auto_actions_.pending_basket_profile = -1;
}

void UIManager::basket_auto_start_timer_cb(lv_timer_t* timer) {
    auto* ui = static_cast<UIManager*>(lv_timer_get_user_data(timer));
    if (ui) {
        ui->complete_pending_basket_auto_start();
    }
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
    const float live_weight = sensor->get_weight_low_latency();

    if (std::fabs(live_weight) <= USER_BASKET_DETECTION_REMOVAL_THRESHOLD_G) {
        if (auto_actions_.basket_placement_latched || auto_actions_.basket_start_pending) {
            cancel_pending_basket_auto_start();
            ready_screen.clear_status();
        }
        auto_actions_.basket_placement_latched = false;
    }

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
                            LOG_BLE("[AUTO ACTION] Trigger confirmed: %.1fg over %lums with settled weight\n",
                                    static_cast<double>(delta_g),
                                    static_cast<unsigned long>(span_ms));

                            bool basket_detection_handled = false;
                            if (basket_detector_.is_enabled() && basket_detector_.is_configured()) {
                                basket_detection_handled = true;
                                if (!auto_actions_.basket_placement_latched && !auto_actions_.basket_start_pending) {
                                    const float settled_weight = sensor->get_weight_high_latency();
                                    const BasketDetectionResult result = basket_detector_.classify(settled_weight);
                                    auto_actions_.last_auto_start_ms = now;
                                    auto_actions_.basket_placement_latched = true;

                                    switch (result) {
                                        case BasketDetectionResult::SINGLE:
                                            LOG_BLE("[AUTO ACTION] Detected SINGLE basket at %.1fg\n",
                                                    static_cast<double>(settled_weight));
                                            schedule_basket_auto_start(0, "Detected SINGLE basket");
                                            break;
                                        case BasketDetectionResult::DOUBLE:
                                            LOG_BLE("[AUTO ACTION] Detected DOUBLE basket at %.1fg\n",
                                                    static_cast<double>(settled_weight));
                                            schedule_basket_auto_start(1, "Detected DOUBLE basket");
                                            break;
                                        case BasketDetectionResult::AMBIGUOUS:
                                            LOG_BLE("[AUTO ACTION] Basket detection ambiguous at %.1fg\n",
                                                    static_cast<double>(settled_weight));
                                            ready_screen.show_transient_status("Basket weights too close",
                                                                               USER_BASKET_DETECTION_STATUS_MS);
                                            break;
                                        case BasketDetectionResult::NO_MATCH:
                                            LOG_BLE("[AUTO ACTION] No basket match at %.1fg\n",
                                                    static_cast<double>(settled_weight));
                                            ready_screen.show_transient_status("No basket match",
                                                                               USER_BASKET_DETECTION_STATUS_MS);
                                            break;
                                        case BasketDetectionResult::UNCONFIGURED:
                                            basket_detection_handled = false;
                                            auto_actions_.basket_placement_latched = false;
                                            break;
                                    }
                                }
                            }

                            if (!basket_detection_handled) {
                                auto_actions_.last_auto_start_ms = now;
                                grinding_controller_->handle_grind_button();
                            }
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
        const bool rearm_ready =
            (now - auto_actions_.last_auto_return_ms) >= USER_AUTO_GRIND_REARM_DELAY_MS;

        GrindController::GrindSessionResult session_result =
            grind_controller ? grind_controller->get_last_session_result()
                             : GrindController::GrindSessionResult::UNKNOWN;
        const bool successful_result =
            session_result == GrindController::GrindSessionResult::SUCCESS;

        if (successful_result && live_weight <= kCompleteExitThresholdG && rearm_ready) {
            LOG_BLE("[AUTO ACTION] Detected near-empty scale - returning to ready screen\n");
            auto_actions_.last_auto_return_ms = now;
            if (grind_controller) {
                grind_controller->return_to_idle();
            }
        }
    }
}
