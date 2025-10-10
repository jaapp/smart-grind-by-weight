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

// Internal execution sub-phases (matches GrindController pattern)
enum class AutoTuneSubPhase {
    IDLE,
    PULSE_EXECUTE,          // Executing pulse via RMT
    MOTOR_SETTLING,         // Waiting for motor vibrations to settle
    COLLECTION_DELAY,       // Allow grounds to collect in cup
    SCALE_SETTLING,         // Waiting for scale to settle
    MEASURE_COMPLETE,       // Ready to process result
    TARING                  // Performing tare operation
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
    float last_pulse_ms;
    float step_size_ms;
    bool last_pulse_success;
    int verification_round;
    int verification_success_count;
    float final_latency_ms;
    float previous_latency_ms;

    // Console message tracking
    char last_message[256];
    bool has_new_message;
};

class AutoTuneController {
private:
    WeightSensor* weight_sensor;
    Grinder* grinder;
    GrindController* grind_controller;

    AutoTunePhase current_phase;
    AutoTuneSubPhase sub_phase;
    bool is_running;
    bool cancel_requested;

    // Binary search state
    float current_pulse_ms;
    float active_pulse_ms;
    float last_executed_pulse_ms;
    float step_size;
    float last_success_ms;
    enum SearchDirection { UP, DOWN };
    SearchDirection direction;
    bool found_lower_bound;
    int iteration;

    // Verification state
    int verification_round;
    int verification_pulse_count;
    int verification_success_count;
    float candidate_ms;

    // Weight tracking
    float last_settled_weight;
    float pre_pulse_weight;

    // Timing tracking
    unsigned long phase_start_time;
    unsigned long settling_start_time;

    // Result tracking
    AutoTuneResult result;
    AutoTuneProgress progress;

public:
    AutoTuneController();

    void init(WeightSensor* ws, Grinder* gr, GrindController* gc);

    // Main control methods
    bool start();  // Returns false if preconditions not met
    void cancel();
    void update(); // Call from main loop (non-blocking state machine)

    // Status methods
    bool is_active() const { return is_running; }
    AutoTunePhase get_phase() const { return current_phase; }
    const AutoTuneProgress& get_progress() const { return progress; }
    const AutoTuneResult& get_result() const { return result; }
    void clear_message_flag() { progress.has_new_message = false; }

private:
    // Phase state machines (non-blocking)
    void update_priming_phase();
    void update_binary_search_phase();
    void update_verification_phase();

    // Sub-phase execution (matches GrindController pattern)
    void start_pulse(float pulse_duration_ms);
    void update_pulse_execute();
    void update_motor_settling();
    void update_collection_delay();
    void update_scale_settling();
    void process_measurement_result(float weight_delta);

    // Tare handling
    void start_tare();
    void update_tare();

    // Phase transition
    void switch_phase(AutoTunePhase new_phase);
    void switch_sub_phase(AutoTuneSubPhase new_sub_phase);

    // Completion
    void complete_with_success(float final_latency_ms);
    void complete_with_failure(const char* error_msg);

    // Helper methods
    void update_progress();
    void log_message(const char* format, ...);
    const char* get_phase_name(AutoTunePhase phase) const;
};
