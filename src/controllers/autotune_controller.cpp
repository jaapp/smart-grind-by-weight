#include "autotune_controller.h"
#include <Arduino.h>
#include <cmath>

AutoTuneController::AutoTuneController()
    : weight_sensor(nullptr)
    , grinder(nullptr)
    , grind_controller(nullptr)
    , current_phase(AutoTunePhase::IDLE)
    , is_running(false)
    , cancel_requested(false)
    , current_pulse_ms(0.0f)
    , step_size(0.0f)
    , last_success_ms(0.0f)
    , direction(DOWN)
    , found_lower_bound(false)
    , iteration(0)
    , verification_round(0)
    , candidate_ms(0.0f)
    , last_settled_weight(0.0f)
{
    result.success = false;
    result.latency_ms = 0.0f;
    result.error_message = nullptr;

    memset(&progress, 0, sizeof(progress));
    progress.phase = AutoTunePhase::IDLE;
}

void AutoTuneController::init(WeightSensor* ws, Grinder* gr, GrindController* gc) {
    weight_sensor = ws;
    grinder = gr;
    grind_controller = gc;
}

bool AutoTuneController::start() {
    if (!weight_sensor || !grinder || !grind_controller) {
        LOG_BLE("ERROR: AutoTune cannot start - missing hardware references\n");
        return false;
    }

    if (is_running) {
        LOG_BLE("WARNING: AutoTune already running\n");
        return false;
    }

    LOG_BLE("=== Starting Motor Response Latency Auto-Tune ===\n");

    is_running = true;
    cancel_requested = false;
    current_phase = AutoTunePhase::PRIMING;

    // Initialize binary search state
    current_pulse_ms = GRIND_MOTOR_RESPONSE_LATENCY_MAX_MS;
    step_size = GRIND_MOTOR_RESPONSE_LATENCY_MAX_MS - GRIND_MOTOR_RESPONSE_LATENCY_MIN_MS;
    last_success_ms = 0.0f;
    direction = DOWN;
    found_lower_bound = false;
    iteration = 0;

    // Initialize verification state
    verification_round = 0;
    candidate_ms = 0.0f;

    // Store previous latency for comparison
    progress.previous_latency_ms = grind_controller->get_motor_response_latency();

    update_progress();

    return true;
}

void AutoTuneController::cancel() {
    LOG_BLE("AutoTune: User cancel requested\n");
    cancel_requested = true;
}

void AutoTuneController::update() {
    if (!is_running) {
        return;
    }

    if (cancel_requested) {
        LOG_BLE("AutoTune: Cancelled by user\n");
        complete_with_failure("Cancelled by user");
        return;
    }

    switch (current_phase) {
        case AutoTunePhase::PRIMING:
            run_priming_phase();
            break;

        case AutoTunePhase::BINARY_SEARCH:
            run_binary_search_phase();
            break;

        case AutoTunePhase::VERIFICATION:
            run_verification_phase();
            break;

        case AutoTunePhase::COMPLETE_SUCCESS:
        case AutoTunePhase::COMPLETE_FAILURE:
            // Terminal states - do nothing
            break;

        default:
            break;
    }
}

void AutoTuneController::run_priming_phase() {
    LOG_BLE("AutoTune Phase 0: Priming chute with %dms pulse\n", GRIND_AUTOTUNE_PRIMING_PULSE_MS);

    // Execute priming pulse
    grinder->start_pulse_rmt(GRIND_AUTOTUNE_PRIMING_PULSE_MS);

    // Wait for pulse to complete
    while (!grinder->is_pulse_complete()) {
        delay(10);
    }

    LOG_BLE("AutoTune: Priming pulse complete, waiting for settling\n");

    // Wait for motor settling
    delay(GRIND_MOTOR_SETTLING_TIME_MS);

    // Wait for scale settling
    float settled_weight = 0.0f;
    if (!wait_for_settling(&settled_weight)) {
        complete_with_failure("Settling timeout during priming");
        return;
    }

    LOG_BLE("AutoTune: Scale settled at %.3fg, taring scale\n", settled_weight);

    // Tare the scale (blocking operation)
    weight_sensor->tare();

    LOG_BLE("AutoTune: Tare complete, starting binary search\n");

    // Move to binary search phase
    current_phase = AutoTunePhase::BINARY_SEARCH;
    last_settled_weight = 0.0f;
    update_progress();
}

void AutoTuneController::run_binary_search_phase() {
    // Safety check - max iterations
    if (iteration >= GRIND_AUTOTUNE_MAX_ITERATIONS) {
        LOG_BLE("AutoTune: Max iterations (%d) reached\n", GRIND_AUTOTUNE_MAX_ITERATIONS);
        complete_with_failure("Max iterations reached");
        return;
    }

    LOG_BLE("AutoTune Iteration %d: Testing pulse %.1fms (step: %.1fms, dir: %s)\n",
            iteration, current_pulse_ms, step_size, direction == UP ? "UP" : "DOWN");

    // Execute pulse and measure weight change
    float weight_delta = 0.0f;
    if (!execute_pulse_and_measure(current_pulse_ms, &weight_delta)) {
        complete_with_failure("Pulse execution failed");
        return;
    }

    bool pulse_produced_grounds = (weight_delta > GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G);

    LOG_BLE("AutoTune: Weight delta = %.3fg → %s\n",
            weight_delta, pulse_produced_grounds ? "GROUNDS" : "NO GROUNDS");

    progress.last_pulse_success = pulse_produced_grounds;
    update_progress();

    if (pulse_produced_grounds) {
        // Pulse successful - grounds were produced
        last_success_ms = current_pulse_ms;

        // Check termination condition
        if (found_lower_bound && step_size <= GRIND_AUTOTUNE_TARGET_ACCURACY_MS) {
            LOG_BLE("AutoTune: Binary search complete - found boundary at %.1fms\n", last_success_ms);

            // Round up to nearest 10ms
            candidate_ms = ceil(last_success_ms / 10.0f) * 10.0f;
            LOG_BLE("AutoTune: Candidate rounded to %.1fms\n", candidate_ms);

            // Move to verification phase
            current_phase = AutoTunePhase::VERIFICATION;
            verification_round = 0;
            update_progress();
            return;
        }

        // Check for direction reversal
        if (direction == UP) {
            step_size = step_size / 2.0f;
            direction = DOWN;
            LOG_BLE("AutoTune: Direction reversal - halving step to %.1fms\n", step_size);
        }

        // Move DOWN to find lower boundary
        current_pulse_ms = current_pulse_ms - step_size;

    } else {
        // Pulse failed - no grounds produced
        found_lower_bound = true;

        // Check termination condition
        if (step_size <= GRIND_AUTOTUNE_TARGET_ACCURACY_MS) {
            LOG_BLE("AutoTune: Binary search complete - accuracy target reached\n");

            if (last_success_ms == 0.0f) {
                complete_with_failure("No successful pulse found");
                return;
            }

            // Round up to nearest 10ms
            candidate_ms = ceil(last_success_ms / 10.0f) * 10.0f;
            LOG_BLE("AutoTune: Candidate rounded to %.1fms\n", candidate_ms);

            // Move to verification phase
            current_phase = AutoTunePhase::VERIFICATION;
            verification_round = 0;
            update_progress();
            return;
        }

        // Check for direction reversal
        if (direction == DOWN) {
            step_size = step_size / 2.0f;
            direction = UP;
            LOG_BLE("AutoTune: Direction reversal - halving step to %.1fms\n", step_size);
        }

        // Move UP to find working pulse
        current_pulse_ms = current_pulse_ms + step_size;
    }

    // Bounds checking
    current_pulse_ms = constrain(current_pulse_ms,
                                  GRIND_MOTOR_RESPONSE_LATENCY_MIN_MS,
                                  GRIND_MOTOR_RESPONSE_LATENCY_MAX_MS);

    // Check if we hit lower bound
    if (current_pulse_ms <= GRIND_MOTOR_RESPONSE_LATENCY_MIN_MS && found_lower_bound) {
        LOG_BLE("AutoTune: Hit lower search bound\n");

        if (last_success_ms == 0.0f) {
            complete_with_failure("Hit lower bound - no successful pulse");
            return;
        }

        // Round up to nearest 10ms
        candidate_ms = ceil(last_success_ms / 10.0f) * 10.0f;
        LOG_BLE("AutoTune: Candidate rounded to %.1fms\n", candidate_ms);

        // Move to verification phase
        current_phase = AutoTunePhase::VERIFICATION;
        verification_round = 0;
        update_progress();
        return;
    }

    iteration++;
    update_progress();
}

void AutoTuneController::run_verification_phase() {
    LOG_BLE("AutoTune Phase 2: Verification round %d, testing %.1fms\n",
            verification_round + 1, candidate_ms);

    int success_count = 0;

    // Run 5 verification pulses
    for (int i = 0; i < GRIND_AUTOTUNE_VERIFICATION_PULSES; i++) {
        if (cancel_requested) {
            complete_with_failure("Cancelled during verification");
            return;
        }

        LOG_BLE("AutoTune: Verification pulse %d/%d\n", i + 1, GRIND_AUTOTUNE_VERIFICATION_PULSES);

        float weight_delta = 0.0f;
        if (!execute_pulse_and_measure(candidate_ms, &weight_delta)) {
            complete_with_failure("Verification pulse failed");
            return;
        }

        if (weight_delta > GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G) {
            success_count++;
        }

        // Update progress
        progress.verification_success_count = success_count;
        update_progress();
    }

    float success_rate = (float)success_count / (float)GRIND_AUTOTUNE_VERIFICATION_PULSES;
    LOG_BLE("AutoTune: Verification round %d result: %d/%d (%.0f%%)\n",
            verification_round + 1, success_count, GRIND_AUTOTUNE_VERIFICATION_PULSES, success_rate * 100.0f);

    if (success_rate >= GRIND_AUTOTUNE_SUCCESS_RATE) {
        // SUCCESS - save to NVS
        LOG_BLE("AutoTune: Verification successful - saving %.1fms to NVS\n", candidate_ms);
        complete_with_success(candidate_ms);
        return;
    }

    // Insufficient reliability - increase by 10ms and try again
    verification_round++;

    if (verification_round >= 5) {
        LOG_BLE("AutoTune: Verification failed after 5 rounds\n");
        complete_with_failure("Failed verification after 5 rounds");
        return;
    }

    candidate_ms = candidate_ms + GRIND_AUTOTUNE_TARGET_ACCURACY_MS;
    LOG_BLE("AutoTune: Increasing candidate to %.1fms for next round\n", candidate_ms);

    progress.verification_success_count = 0;
    update_progress();
}

bool AutoTuneController::execute_pulse_and_measure(float pulse_duration_ms, float* weight_delta_out) {
    // Start pulse
    grinder->start_pulse_rmt(pulse_duration_ms);

    // Wait for pulse to complete
    while (!grinder->is_pulse_complete()) {
        delay(10);
    }

    // Wait for motor settling
    delay(GRIND_MOTOR_SETTLING_TIME_MS);

    // Wait for scale settling
    float settled_weight = 0.0f;
    if (!wait_for_settling(&settled_weight)) {
        LOG_BLE("ERROR: Settling timeout during pulse measurement\n");
        return false;
    }

    // Calculate weight delta
    *weight_delta_out = settled_weight - last_settled_weight;
    last_settled_weight = settled_weight;

    return true;
}

bool AutoTuneController::wait_for_settling(float* settled_weight_out) {
    // Use the existing WeightSensor settling method with timeout
    unsigned long start_time = millis();

    while (millis() - start_time < GRIND_AUTOTUNE_SETTLING_TIMEOUT_MS) {
        if (weight_sensor->check_settling_complete(GRIND_SCALE_PRECISION_SETTLING_TIME_MS, settled_weight_out)) {
            return true;
        }
        delay(50);
    }

    return false;  // Timeout
}

void AutoTuneController::complete_with_success(float final_latency_ms) {
    LOG_BLE("=== AutoTune Complete: SUCCESS ===\n");
    LOG_BLE("Final motor latency: %.1fms (previous: %.1fms)\n",
            final_latency_ms, progress.previous_latency_ms);

    // Save to NVS via GrindController
    grind_controller->save_motor_latency(final_latency_ms);

    result.success = true;
    result.latency_ms = final_latency_ms;
    result.error_message = nullptr;

    progress.final_latency_ms = final_latency_ms;
    current_phase = AutoTunePhase::COMPLETE_SUCCESS;
    update_progress();

    is_running = false;
}

void AutoTuneController::complete_with_failure(const char* error_msg) {
    LOG_BLE("=== AutoTune Complete: FAILURE ===\n");
    LOG_BLE("Error: %s\n", error_msg);
    LOG_BLE("Using default latency: %.1fms\n", GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS);

    result.success = false;
    result.latency_ms = GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS;
    result.error_message = error_msg;

    current_phase = AutoTunePhase::COMPLETE_FAILURE;
    update_progress();

    is_running = false;
}

void AutoTuneController::update_progress() {
    progress.phase = current_phase;
    progress.iteration = iteration;
    progress.current_pulse_ms = current_pulse_ms;
    progress.step_size_ms = step_size;
    progress.verification_round = verification_round;

    LOG_BLE("AutoTune Progress: Phase=%s, Iteration=%d, Pulse=%.1fms, Step=%.1fms\n",
            get_phase_name(current_phase), iteration, current_pulse_ms, step_size);
}

const char* AutoTuneController::get_phase_name(AutoTunePhase phase) const {
    switch (phase) {
        case AutoTunePhase::IDLE: return "IDLE";
        case AutoTunePhase::PRIMING: return "PRIMING";
        case AutoTunePhase::BINARY_SEARCH: return "BINARY_SEARCH";
        case AutoTunePhase::VERIFICATION: return "VERIFICATION";
        case AutoTunePhase::COMPLETE_SUCCESS: return "SUCCESS";
        case AutoTunePhase::COMPLETE_FAILURE: return "FAILURE";
        default: return "UNKNOWN";
    }
}
