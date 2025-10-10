#pragma once

#include "../config/constants.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "grind_controller.h"

// Auto-tune phases for UI display
enum class AutoTunePhase {
    IDLE,                   // Not running
    PRIMING,                // Chute priming phase
    BINARY_SEARCH,          // Binary search phase
    VERIFICATION,           // Statistical verification phase
    COMPLETE_SUCCESS,       // Successfully completed
    COMPLETE_FAILURE        // Failed to find reliable value
};

// Auto-tune result structure
struct AutoTuneResult {
    bool success;
    float latency_ms;
    const char* error_message;
};

// Auto-tune progress data for UI updates
struct AutoTuneProgress {
    AutoTunePhase phase;
    int iteration;
    float current_pulse_ms;
    float step_size_ms;
    bool last_pulse_success;
    int verification_round;
    int verification_success_count;
    float final_latency_ms;
    float previous_latency_ms;
};

class AutoTuneController {
private:
    WeightSensor* weight_sensor;
    Grinder* grinder;
    GrindController* grind_controller;

    AutoTunePhase current_phase;
    bool is_running;
    bool cancel_requested;

    // Binary search state
    float current_pulse_ms;
    float step_size;
    float last_success_ms;
    enum SearchDirection { UP, DOWN };
    SearchDirection direction;
    bool found_lower_bound;
    int iteration;

    // Verification state
    int verification_round;
    float candidate_ms;

    // Last weight for delta calculation
    float last_settled_weight;

    // Result tracking
    AutoTuneResult result;
    AutoTuneProgress progress;

public:
    AutoTuneController();

    void init(WeightSensor* ws, Grinder* gr, GrindController* gc);

    // Main control methods
    bool start();  // Returns false if preconditions not met
    void cancel();
    void update(); // Call from main loop to process state machine

    // Status methods
    bool is_active() const { return is_running; }
    AutoTunePhase get_phase() const { return current_phase; }
    const AutoTuneProgress& get_progress() const { return progress; }
    const AutoTuneResult& get_result() const { return result; }

private:
    // Phase implementation methods
    void run_priming_phase();
    void run_binary_search_phase();
    void run_verification_phase();
    void complete_with_success(float final_latency_ms);
    void complete_with_failure(const char* error_msg);

    // Helper methods
    bool execute_pulse_and_measure(float pulse_duration_ms, float* weight_delta_out);
    bool wait_for_settling(float* settled_weight_out);
    void update_progress();
    const char* get_phase_name(AutoTunePhase phase) const;
};
