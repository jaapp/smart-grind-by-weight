#include "diagnostics_controller.h"
#include "../hardware/hardware_manager.h"
#include "../hardware/WeightSensor.h"
#include "../controllers/grind_controller.h"
#include "../config/constants.h"

DiagnosticsController::DiagnosticsController()
    : hardware_manager_(nullptr) {
}

void DiagnosticsController::init(HardwareManager* hw_mgr) {
    hardware_manager_ = hw_mgr;
    active_diagnostics_.clear();
}

void DiagnosticsController::update(HardwareManager* hw_mgr, GrindController* grind_ctrl, uint32_t uptime_ms) {
    if (!hw_mgr) return;

    WeightSensor* sensor = hw_mgr->get_weight_sensor();
    if (!sensor) return;

    // Phase 1: Calibration flag and boot diagnostics
    check_load_cell_calibration(sensor);
    check_load_cell_boot_fault(sensor);

    // Phase 5: Add noise monitoring
    check_load_cell_noise(sensor, uptime_ms);

    // Phase 6: Add mechanical instability
    check_mechanical_stability(grind_ctrl);
}

void DiagnosticsController::check_load_cell_calibration(WeightSensor* sensor) {
    if (!sensor) return;

    bool is_calibrated = sensor->is_calibrated();

    if (!is_calibrated) {
        set_diagnostic_active(DiagnosticCode::LOAD_CELL_NOT_CALIBRATED);
    } else {
        clear_diagnostic(DiagnosticCode::LOAD_CELL_NOT_CALIBRATED);
    }
}

void DiagnosticsController::check_load_cell_boot_fault(WeightSensor* sensor) {
    if (!sensor) return;

    using HardwareFault = WeightSensor::HardwareFault;

    HardwareFault fault = sensor->get_hardware_fault();
    switch (fault) {
        case HardwareFault::NONE:
            clear_diagnostic(DiagnosticCode::HX711_NOT_CONNECTED);
            clear_diagnostic(DiagnosticCode::HX711_SAMPLE_RATE_INVALID);
            break;
        case HardwareFault::NOT_CONNECTED:
            set_diagnostic_active(DiagnosticCode::HX711_NOT_CONNECTED);
            clear_diagnostic(DiagnosticCode::HX711_SAMPLE_RATE_INVALID);
            break;
        case HardwareFault::NO_DATA:
            clear_diagnostic(DiagnosticCode::HX711_NOT_CONNECTED);
            clear_diagnostic(DiagnosticCode::HX711_SAMPLE_RATE_INVALID);
            break;
        case HardwareFault::INVALID_SAMPLE_RATE:
            set_diagnostic_active(DiagnosticCode::HX711_SAMPLE_RATE_INVALID);
            clear_diagnostic(DiagnosticCode::HX711_NOT_CONNECTED);
            break;
    }
}

void DiagnosticsController::check_load_cell_noise(WeightSensor* sensor, uint32_t uptime_ms) {
    if (!sensor) {
        return;
    }

    constexpr uint32_t kNoiseHighThresholdMs = 60'000;  // 60 seconds
    constexpr uint32_t kNoiseRecoveryThresholdMs = 120'000;  // 120 seconds

    bool noise_ok = sensor->noise_level_diagnostic();
    auto* active_noise_diag = find_diagnostic(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED);

    if (!noise_ok) {
        // Reset recovery timer while noise is high
        noise_recovery_timer_running_ = false;

        if (!noise_high_timer_running_) {
            noise_high_timer_running_ = true;
            noise_high_start_ms_ = uptime_ms;
        }

        uint32_t elapsed_high_ms = uptime_ms - noise_high_start_ms_;
        if (elapsed_high_ms >= kNoiseHighThresholdMs) {
            if (active_noise_diag) {
                // Update last seen timestamp to keep diagnostic active
                active_noise_diag->last_seen_ms = millis();
            } else {
                set_diagnostic_active(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED);
            }
        }
    } else {
        noise_high_timer_running_ = false;

        if (active_noise_diag) {
            if (!noise_recovery_timer_running_) {
                noise_recovery_timer_running_ = true;
                noise_recovery_start_ms_ = uptime_ms;
            }

            uint32_t elapsed_recovery_ms = uptime_ms - noise_recovery_start_ms_;
            if (elapsed_recovery_ms >= kNoiseRecoveryThresholdMs) {
                clear_diagnostic(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED);
                noise_recovery_timer_running_ = false;
            }
        } else {
            noise_recovery_timer_running_ = false;
        }
    }
}

void DiagnosticsController::check_mechanical_stability(GrindController* grind_ctrl) {
    if (!grind_ctrl) {
        return;
    }

    int anomaly_count = grind_ctrl->get_mechanical_anomaly_count();
    if (anomaly_count >= GRIND_MECHANICAL_EVENT_REQUIRED_COUNT) {
        set_diagnostic_active(DiagnosticCode::MECHANICAL_INSTABILITY);
    }
}

DiagnosticCode DiagnosticsController::get_highest_priority_warning() const {
    // Priority order (highest to lowest):
    // 1. HX711_NOT_CONNECTED - load cell hardware missing
    // 2. HX711_SAMPLE_RATE_INVALID - incorrect RATE pin configuration
    // 3. MECHANICAL_INSTABILITY - immediate safety concern
    // 4. LOAD_CELL_NOISY_SUSTAINED - affects grind quality
    // 5. LOAD_CELL_NOT_CALIBRATED - initial setup issue

    if (find_diagnostic(DiagnosticCode::HX711_NOT_CONNECTED)) {
        return DiagnosticCode::HX711_NOT_CONNECTED;
    }
    if (find_diagnostic(DiagnosticCode::HX711_SAMPLE_RATE_INVALID)) {
        return DiagnosticCode::HX711_SAMPLE_RATE_INVALID;
    }
    if (find_diagnostic(DiagnosticCode::MECHANICAL_INSTABILITY)) {
        return DiagnosticCode::MECHANICAL_INSTABILITY;
    }
    if (find_diagnostic(DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED)) {
        return DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED;
    }
    if (find_diagnostic(DiagnosticCode::LOAD_CELL_NOT_CALIBRATED)) {
        return DiagnosticCode::LOAD_CELL_NOT_CALIBRATED;
    }

    return DiagnosticCode::NONE;
}

std::vector<DiagnosticState> DiagnosticsController::get_active_diagnostics() const {
    return active_diagnostics_;
}

bool DiagnosticsController::has_active_diagnostics() const {
    return !active_diagnostics_.empty();
}

void DiagnosticsController::acknowledge_diagnostic(DiagnosticCode code) {
    DiagnosticState* diag = find_diagnostic(code);
    if (diag) {
        diag->user_acknowledged = true;
    }
}

void DiagnosticsController::reset_diagnostic(DiagnosticCode code) {
    clear_diagnostic(code);
}

void DiagnosticsController::reset_all_transient_diagnostics() {
    // Clear all diagnostics that have been acknowledged
    auto it = active_diagnostics_.begin();
    while (it != active_diagnostics_.end()) {
        if (it->user_acknowledged) {
            it = active_diagnostics_.erase(it);
        } else {
            ++it;
        }
    }
}

const char* DiagnosticsController::get_diagnostic_message(DiagnosticCode code) const {
    switch (code) {
        case DiagnosticCode::HX711_NOT_CONNECTED:
            return "HX711 sensor not connected. Check wiring and restart.";
        case DiagnosticCode::HX711_SAMPLE_RATE_INVALID:
            return "HX711 sample rate invalid. Ensure RATE pin is wired for 10 SPS.";
        case DiagnosticCode::LOAD_CELL_NOT_CALIBRATED:
            return "Load cell not calibrated. Go to Tools → Calibrate";
        case DiagnosticCode::LOAD_CELL_NOISY_SUSTAINED:
            return "Sustained sensor noise detected. Check connections and environment.";
        case DiagnosticCode::MECHANICAL_INSTABILITY:
            return "Mechanical instability detected. Check grinder mounting and connections.";
        case DiagnosticCode::NONE:
        default:
            return "";
    }
}

void DiagnosticsController::set_diagnostic_active(DiagnosticCode code) {
    if (code == DiagnosticCode::NONE) return;

    DiagnosticState* existing = find_diagnostic(code);
    if (existing) {
        // Update existing diagnostic
        existing->last_seen_ms = millis();
        existing->occurrence_count++;
    } else {
        // Add new diagnostic
        DiagnosticState new_diag;
        new_diag.code = code;
        new_diag.first_detected_ms = millis();
        new_diag.last_seen_ms = millis();
        new_diag.user_acknowledged = false;
        new_diag.occurrence_count = 1;
        active_diagnostics_.push_back(new_diag);
    }
}

void DiagnosticsController::clear_diagnostic(DiagnosticCode code) {
    auto it = active_diagnostics_.begin();
    while (it != active_diagnostics_.end()) {
        if (it->code == code) {
            it = active_diagnostics_.erase(it);
        } else {
            ++it;
        }
    }
}

void DiagnosticsController::reset_noise_tracking() {
    noise_high_timer_running_ = false;
    noise_recovery_timer_running_ = false;
    noise_high_start_ms_ = 0;
    noise_recovery_start_ms_ = 0;
}

DiagnosticState* DiagnosticsController::find_diagnostic(DiagnosticCode code) {
    for (auto& diag : active_diagnostics_) {
        if (diag.code == code) {
            return &diag;
        }
    }
    return nullptr;
}

const DiagnosticState* DiagnosticsController::find_diagnostic(DiagnosticCode code) const {
    for (const auto& diag : active_diagnostics_) {
        if (diag.code == code) {
            return &diag;
        }
    }
    return nullptr;
}
