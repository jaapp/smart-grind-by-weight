#pragma once

#include <vector>
#include <Arduino.h>

// Forward declarations
class HardwareManager;
class GrindController;
class WeightSensor;

// Diagnostic codes representing different system issues
enum class DiagnosticCode {
    NONE = 0,                       // No diagnostics active
    HX711_NOT_CONNECTED,            // HX711 board missing or not responding at boot
    HX711_NO_DATA,                  // HX711 responding but no valid data
    LOAD_CELL_NOT_CALIBRATED,       // Load cell hasn't been calibrated yet
    LOAD_CELL_NOISY_SUSTAINED,      // Sustained excessive noise (60s+) - Phase 5
    MECHANICAL_INSTABILITY          // Mechanical issues during grinding - Phase 6
};

// State for a single diagnostic condition
struct DiagnosticState {
    DiagnosticCode code;
    unsigned long first_detected_ms;
    unsigned long last_seen_ms;
    bool user_acknowledged;
    uint32_t occurrence_count;
};

// Central diagnostic state manager for the system
// Monitors multiple diagnostic conditions and provides unified interface for UI
class DiagnosticsController {
public:
    DiagnosticsController();

    // Initialize diagnostic controller
    void init(HardwareManager* hw_mgr);

    // Update all diagnostic checks (called from UIManager main loop)
    void update(HardwareManager* hw_mgr, GrindController* grind_ctrl, uint32_t uptime_ms);

    // Query diagnostic state
    DiagnosticCode get_highest_priority_warning() const;
    std::vector<DiagnosticState> get_active_diagnostics() const;
    bool has_active_diagnostics() const;

    // User interactions
    void acknowledge_diagnostic(DiagnosticCode code);
    void reset_diagnostic(DiagnosticCode code);
    void reset_all_transient_diagnostics();

    // Helper to get human-readable diagnostic message
    const char* get_diagnostic_message(DiagnosticCode code) const;

    // Helpers for other systems
    void reset_noise_tracking();

private:
    // Individual diagnostic checkers
    void check_load_cell_calibration(WeightSensor* sensor);
    void check_load_cell_boot_fault(WeightSensor* sensor);
    void check_load_cell_noise(WeightSensor* sensor, uint32_t uptime_ms);
    void check_mechanical_stability(GrindController* grind_ctrl);

    // State management helpers
    void set_diagnostic_active(DiagnosticCode code);
    void clear_diagnostic(DiagnosticCode code);
    DiagnosticState* find_diagnostic(DiagnosticCode code);
    const DiagnosticState* find_diagnostic(DiagnosticCode code) const;

    // Active diagnostics
    std::vector<DiagnosticState> active_diagnostics_;

    // Hardware manager reference
    HardwareManager* hardware_manager_;

    // Phase 5 sustained noise tracking
    uint32_t noise_high_start_ms_ = 0;
    uint32_t noise_recovery_start_ms_ = 0;
    bool noise_high_timer_running_ = false;
    bool noise_recovery_timer_running_ = false;
};
