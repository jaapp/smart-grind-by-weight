#include "calibration_controller.h"

#include <Arduino.h>
#include <limits>
#include "../../config/constants.h"
#include "../../system/diagnostics_controller.h"
#include "../components/ui_operations.h"
#include "../ui_manager.h"

CalibrationUIController::CalibrationUIController(UIManager* manager)
    : ui_manager_(manager) {}

void CalibrationUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    auto ok_btn = ui_manager_->calibration_screen.get_ok_button();
    if (ok_btn) {
        lv_obj_add_event_cb(ok_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            static_cast<CalibrationUIController*>(lv_event_get_user_data(e))->handle_ok();
        }, LV_EVENT_CLICKED, this);
    }

    auto cancel_btn = ui_manager_->calibration_screen.get_cancel_button();
    if (cancel_btn) {
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            static_cast<CalibrationUIController*>(lv_event_get_user_data(e))->handle_cancel();
        }, LV_EVENT_CLICKED, this);
    }

    auto plus_btn = ui_manager_->calibration_screen.get_plus_btn();
    if (plus_btn) {
        lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
            static_cast<CalibrationUIController*>(lv_event_get_user_data(e))->handle_plus(lv_event_get_code(e));
        }, LV_EVENT_ALL, this);
    }

    auto minus_btn = ui_manager_->calibration_screen.get_minus_btn();
    if (minus_btn) {
        lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
            static_cast<CalibrationUIController*>(lv_event_get_user_data(e))->handle_minus(lv_event_get_code(e));
        }, LV_EVENT_ALL, this);
    }
}

void CalibrationUIController::update() {
    if (!ui_manager_) {
        return;
    }

    // Continuously reset noise diagnostic during entire calibration sequence
    if (ui_manager_->diagnostics_controller_) {
        auto active_diagnostics = ui_manager_->diagnostics_controller_->get_active_diagnostics();
        for (const auto& diagnostic : active_diagnostics) {
            if (diagnostic.code == DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED) {
                ui_manager_->diagnostics_controller_->reset_diagnostic(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED);
                ui_manager_->diagnostics_controller_->reset_noise_tracking();
                break;
            }
        }
    }

    CalibrationStep current_step = ui_manager_->calibration_screen.get_step();
    if (current_step != CAL_STEP_NOISE_CHECK && noise_check_active_) {
        reset_noise_check_state();
    }

    if (current_step == CAL_STEP_NOISE_CHECK) {
        update_noise_check();
        return;
    }

    if (current_step == CAL_STEP_COMPLETE) {
        float weight = ui_manager_->get_hardware_manager()->get_weight_sensor()->get_display_weight();
        ui_manager_->calibration_screen.update_current_weight(weight);
    } else {
        int32_t raw_reading = ui_manager_->get_hardware_manager()->get_weight_sensor()->get_raw_adc_instant();
        ui_manager_->calibration_screen.update_current_weight(static_cast<float>(raw_reading));

        // In weight step, verify user has placed weight on scale
        if (current_step == CAL_STEP_WEIGHT) {
#if DEBUG_ENABLE_LOADCELL_MOCK
            // Always show OK button when using mock driver
            ui_manager_->calibration_screen.set_ok_button_enabled(true);
#else
            int32_t adc_delta = abs(raw_reading - baseline_adc_value_);
            bool weight_detected = adc_delta >= HW_LOADCELL_CAL_MIN_ADC_VALUE;
            ui_manager_->calibration_screen.set_ok_button_enabled(weight_detected);
#endif
        }
    }
}

void CalibrationUIController::handle_ok() {
    if (!ui_manager_) return;

    CalibrationStep step = ui_manager_->calibration_screen.get_step();
    switch (step) {
        case CAL_STEP_EMPTY:
            UIOperations::execute_tare(ui_manager_->get_hardware_manager(), [this]() {
                // Capture baseline ADC value after taring
                baseline_adc_value_ = ui_manager_->get_hardware_manager()->get_weight_sensor()->get_raw_adc_instant();
                ui_manager_->calibration_screen.set_step(CAL_STEP_WEIGHT);
            });
            break;
        case CAL_STEP_WEIGHT: {
            float cal_weight = ui_manager_->calibration_screen.get_calibration_weight();
            UIOperations::execute_calibration(ui_manager_->get_hardware_manager(), cal_weight, [this]() {
                ui_manager_->calibration_screen.set_step(CAL_STEP_NOISE_CHECK);
                start_noise_check();
            });
            break;
        }
        case CAL_STEP_NOISE_CHECK:
            if (noise_check_passed_) {
                complete_calibration();
            }
            break;
        case CAL_STEP_COMPLETE:
            ui_manager_->set_current_tab(3);
            ui_manager_->switch_to_state(UIState::SETTINGS);
            break;
    }
}

void CalibrationUIController::handle_cancel() {
    if (!ui_manager_) return;

    reset_noise_check_state();
    baseline_adc_value_ = 0;
    ui_manager_->set_current_tab(3);
    ui_manager_->switch_to_state(UIState::SETTINGS);
}

void CalibrationUIController::handle_plus(lv_event_code_t code) {
    if (!ui_manager_) return;

    if (code == LV_EVENT_CLICKED) {
        float cal_weight = ui_manager_->calibration_screen.get_calibration_weight();
        cal_weight = ui_manager_->get_profile_controller()->clamp_weight(cal_weight + USER_FINE_WEIGHT_ADJUSTMENT_G);
        ui_manager_->calibration_screen.update_calibration_weight(cal_weight);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->start(1);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->stop();
        }
    }
}

void CalibrationUIController::handle_minus(lv_event_code_t code) {
    if (!ui_manager_) return;

    if (code == LV_EVENT_CLICKED) {
        float cal_weight = ui_manager_->calibration_screen.get_calibration_weight();
        cal_weight = ui_manager_->get_profile_controller()->clamp_weight(cal_weight - USER_FINE_WEIGHT_ADJUSTMENT_G);
        ui_manager_->calibration_screen.update_calibration_weight(cal_weight);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->start(-1);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (ui_manager_->jog_adjust_controller_) {
            ui_manager_->jog_adjust_controller_->stop();
        }
    }
}

void CalibrationUIController::start_noise_check() {
    noise_step_enter_ms_ = millis();
    noise_ok_since_ms_ = 0;
    noise_check_passed_ = false;
    noise_check_active_ = true;
    noise_check_forced_pass_ = false;

    if (!ui_manager_) {
        return;
    }

    auto& screen = ui_manager_->calibration_screen;
    screen.set_ok_button_enabled(false);
    screen.update_noise_status("Status: Checking...", lv_color_hex(THEME_COLOR_TEXT_SECONDARY));
    screen.update_noise_metric(std::numeric_limits<float>::quiet_NaN());
}

void CalibrationUIController::reset_noise_check_state() {
    noise_check_active_ = false;
    noise_check_passed_ = false;
    noise_step_enter_ms_ = 0;
    noise_ok_since_ms_ = 0;
    noise_check_forced_pass_ = false;
}

void CalibrationUIController::update_noise_check() {
    if (!ui_manager_ || !noise_check_active_) {
        return;
    }

    if (ui_manager_->calibration_screen.get_step() != CAL_STEP_NOISE_CHECK) {
        return;
    }

    auto* hardware = ui_manager_->get_hardware_manager();
    if (!hardware) {
        return;
    }

    auto* weight_sensor = hardware->get_weight_sensor();
    if (!weight_sensor) {
        return;
    }

    constexpr unsigned long kMinWaitMs = 3000;
    constexpr unsigned long kStableWaitMs = 5000;
    constexpr unsigned long kForceEnableMs = 15000;

    unsigned long now = millis();
    float std_dev = weight_sensor->get_standard_deviation_g(GRIND_SCALE_PRECISION_SETTLING_TIME_MS);
    ui_manager_->calibration_screen.update_noise_metric(std_dev);

    bool noise_ok = weight_sensor->noise_level_diagnostic();

    if (!noise_check_passed_) {
        if (now - noise_step_enter_ms_ >= kForceEnableMs) {
            noise_check_passed_ = true;
            noise_check_forced_pass_ = true;
            ui_manager_->calibration_screen.update_noise_status("Status: Too noisy",
                                                                lv_color_hex(THEME_COLOR_WARNING));
            ui_manager_->calibration_screen.set_ok_button_enabled(true);
            return;
        }

        if (now - noise_step_enter_ms_ < kMinWaitMs) {
            ui_manager_->calibration_screen.update_noise_status("Status: Checking...",
                                                                lv_color_hex(THEME_COLOR_TEXT_SECONDARY));
            ui_manager_->calibration_screen.set_ok_button_enabled(false);
            noise_ok_since_ms_ = 0;
            return;
        }

        if (noise_ok) {
            if (noise_ok_since_ms_ == 0) {
                noise_ok_since_ms_ = now;
            }

            unsigned long stable_ms = now - noise_ok_since_ms_;
            if (stable_ms >= kStableWaitMs) {
                noise_check_passed_ = true;
                noise_check_forced_pass_ = false;
                ui_manager_->calibration_screen.update_noise_status("Status: OK",
                                                                    lv_color_hex(THEME_COLOR_SUCCESS));
                ui_manager_->calibration_screen.set_ok_button_enabled(true);
            } else {
                unsigned long remaining_ms = kStableWaitMs - stable_ms;
                unsigned int remaining_sec = static_cast<unsigned int>((remaining_ms + 999) / 1000);

                char status_text[48];
                snprintf(status_text, sizeof(status_text), "Status: Stable (%us)", remaining_sec);
                ui_manager_->calibration_screen.update_noise_status(status_text,
                                                                    lv_color_hex(THEME_COLOR_TEXT_PRIMARY));
                ui_manager_->calibration_screen.set_ok_button_enabled(false);
            }
        } else {
            noise_ok_since_ms_ = 0;
            ui_manager_->calibration_screen.update_noise_status("Status: Too noisy",
                                                                lv_color_hex(THEME_COLOR_ERROR));
            ui_manager_->calibration_screen.set_ok_button_enabled(false);
        }
        return;
    }

    // Keep UI consistent once passed
    if (noise_check_forced_pass_) {
        ui_manager_->calibration_screen.update_noise_status("Status: Too noisy",
                                                            lv_color_hex(THEME_COLOR_WARNING));
    } else {
        ui_manager_->calibration_screen.update_noise_status("Status: OK", lv_color_hex(THEME_COLOR_SUCCESS));
    }
    ui_manager_->calibration_screen.set_ok_button_enabled(true);
}

void CalibrationUIController::complete_calibration() {
    if (!ui_manager_) {
        return;
    }

    auto* hardware = ui_manager_->get_hardware_manager();
    auto* weight_sensor = hardware ? hardware->get_weight_sensor() : nullptr;

    if (weight_sensor) {
        weight_sensor->set_calibrated(true);
    }

    reset_noise_check_state();
    baseline_adc_value_ = 0;
    ui_manager_->calibration_screen.set_step(CAL_STEP_COMPLETE);

    if (weight_sensor) {
        ui_manager_->calibration_screen.update_current_weight(weight_sensor->get_display_weight());
    }
}
