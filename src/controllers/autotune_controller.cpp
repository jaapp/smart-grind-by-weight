#include "autotune_controller.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <cstdarg>

#if defined(DEBUG_ENABLE_LOADCELL_MOCK) && (DEBUG_ENABLE_LOADCELL_MOCK != 0)
#include "../hardware/mock_hx711_driver.h"
#endif

AutoTuneController::AutoTuneController()
    : weight_sensor(nullptr)
    , grinder(nullptr)
    , grind_controller(nullptr)
    , current_phase(AutoTunePhase::IDLE)
    , sub_phase(AutoTuneSubPhase::IDLE)
    , is_running(false)
    , cancel_requested(false)
    , current_pulse_ms(0.0f)
    , active_pulse_ms(0.0f)
    , last_executed_pulse_ms(0.0f)
    , step_size(0.0f)
    , last_success_ms(0.0f)
    , direction(DOWN)
    , found_lower_bound(false)
    , iteration(0)
    , verification_round(0)
    , verification_pulse_count(0)
    , verification_success_count(0)
    , candidate_ms(0.0f)
    , last_settled_weight(0.0f)
    , pre_pulse_weight(0.0f)
    , phase_start_time(0)
    , settling_start_time(0)
{
    result.success = false;
    result.latency_ms = 0.0f;
    result.error_message = nullptr;

    memset(&progress, 0, sizeof(progress));
    progress.phase = AutoTunePhase::IDLE;
    progress.has_new_message = false;
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

    LOG_BLE("=== Starting Motor Response Latency Auto-Tune (Non-Blocking) ===\n");

    memset(&progress, 0, sizeof(progress));
    progress.phase = AutoTunePhase::IDLE;
    progress.has_new_message = false;

    is_running = true;
    cancel_requested = false;

    // Initialize binary search state
    current_pulse_ms = GRIND_AUTOTUNE_LATENCY_MAX_MS;
    step_size = GRIND_AUTOTUNE_LATENCY_MAX_MS - GRIND_AUTOTUNE_LATENCY_MIN_MS;
    last_success_ms = 0.0f;
    direction = DOWN;
    found_lower_bound = false;
    iteration = 0;
    active_pulse_ms = 0.0f;
    last_executed_pulse_ms = 0.0f;

    // Initialize verification state
    verification_round = 0;
    verification_pulse_count = 0;
    verification_success_count = 0;
    candidate_ms = 0.0f;

    // Store previous latency for comparison
    progress.previous_latency_ms = grind_controller->get_motor_response_latency();

    // Initialize autotune log file
    LittleFS.remove("/autotune.log");
    autotune_log_file = LittleFS.open("/autotune.log", "w");
    if (autotune_log_file) {
        autotune_log_file.println("=== Autotune Started ===");
        autotune_log_file.printf("Timestamp: %lums\n", millis());
        autotune_log_file.printf("Previous Latency: %.1fms\n", progress.previous_latency_ms);
        autotune_log_file.println();
        autotune_log_file.flush();
        LOG_BLE("AutoTune: Log file created at /autotune.log\n");
    } else {
        LOG_BLE("WARNING: AutoTune could not create log file (filesystem unavailable)\n");
    }

    // Start with priming phase
    switch_phase(AutoTunePhase::PRIMING);

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
            update_priming_phase();
            break;

        case AutoTunePhase::BINARY_SEARCH:
            update_binary_search_phase();
            break;

        case AutoTunePhase::VERIFICATION:
            update_verification_phase();
            break;

        case AutoTunePhase::COMPLETE_SUCCESS:
        case AutoTunePhase::COMPLETE_FAILURE:
        case AutoTunePhase::IDLE:
            // Terminal states - do nothing
            break;
    }
}

//==============================================================================
// Phase State Machines (Non-Blocking)
//==============================================================================

void AutoTuneController::update_priming_phase() {
    switch (sub_phase) {
        case AutoTuneSubPhase::IDLE:
            // Capture pre-prime weight for verification
            pre_pulse_weight = weight_sensor->get_weight_high_latency();
            LOG_BLE("AutoTune Phase 0: Priming chute with %dms pulse (pre-weight: %.3fg)\n",
                    GRIND_AUTOTUNE_PRIMING_PULSE_MS, pre_pulse_weight);
            log_message("Priming...");
            start_pulse(GRIND_AUTOTUNE_PRIMING_PULSE_MS);
            break;

        case AutoTuneSubPhase::PULSE_EXECUTE:
            update_pulse_execute();
            break;

        case AutoTuneSubPhase::MOTOR_SETTLING:
            update_motor_settling();
            break;

        case AutoTuneSubPhase::COLLECTION_DELAY:
            update_collection_delay();
            break;

        case AutoTuneSubPhase::SCALE_SETTLING:
            update_scale_settling();
            break;

        case AutoTuneSubPhase::MEASURE_COMPLETE: {
            // Verify that priming produced grounds
            float settled_weight = last_settled_weight;
            float weight_delta = settled_weight - pre_pulse_weight;

            LOG_BLE("AutoTune: Priming weight delta = %.3fg (threshold: %.3fg)\n",
                    weight_delta, GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G);

            if (weight_delta <= GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G) {
                LOG_BLE("ERROR: Priming failed - no weight increase detected\n");
                log_message("\nPrime failed");
                log_message("Check:");
                log_message("- Beans loaded");
                log_message("- Power on");
                log_message("- Cup placed");
                complete_with_failure("Priming failed - no grounds detected");
                return;
            }

            // Priming successful, now tare
            LOG_BLE("AutoTune: Priming complete (%.3fg added), taring scale\n", weight_delta);
            start_tare();
            break;
        }

        case AutoTuneSubPhase::TARING:
            update_tare();
            break;
    }
}

void AutoTuneController::update_binary_search_phase() {
    switch (sub_phase) {
        case AutoTuneSubPhase::IDLE:
            // Safety check - max iterations
            if (iteration >= GRIND_AUTOTUNE_MAX_ITERATIONS) {
                LOG_BLE("AutoTune: Max iterations (%d) reached\n", GRIND_AUTOTUNE_MAX_ITERATIONS);
                complete_with_failure("Max iterations reached");
                return;
            }

            // Log phase header on first iteration
            if (iteration == 0) {
                log_message("\nBinary Search:");
            }

            LOG_BLE("AutoTune Iteration %d: Testing pulse %.1fms (step: %.1fms, dir: %s)\n",
                    iteration, current_pulse_ms, step_size, direction == UP ? "UP" : "DOWN");
            log_message("Test %.0fms", current_pulse_ms);

            // Capture pre-pulse weight
            pre_pulse_weight = weight_sensor->get_weight_high_latency();

            // Start test pulse
            start_pulse(current_pulse_ms);
            break;

        case AutoTuneSubPhase::PULSE_EXECUTE:
            update_pulse_execute();
            break;

        case AutoTuneSubPhase::MOTOR_SETTLING:
            update_motor_settling();
            break;

        case AutoTuneSubPhase::COLLECTION_DELAY:
            update_collection_delay();
            break;

        case AutoTuneSubPhase::SCALE_SETTLING:
            update_scale_settling();
            break;

        case AutoTuneSubPhase::MEASURE_COMPLETE: {
            // Get settled weight and calculate delta
            float settled_weight = last_settled_weight;
            float weight_delta = settled_weight - pre_pulse_weight;

            last_executed_pulse_ms = active_pulse_ms;

            LOG_BLE("AutoTune: Weight delta = %.3fg → %s\n",
                    weight_delta, weight_delta > GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G ? "GROUNDS" : "NO GROUNDS");

            bool pulse_produced_grounds = (weight_delta > GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G);
            progress.last_pulse_success = pulse_produced_grounds;

            // Log result message and yield to UI for one cycle before checking termination
            log_message("  -> %.2fg %s", weight_delta,
                       pulse_produced_grounds ? "[OK]" : "[X]");

            switch_sub_phase(AutoTuneSubPhase::RESULT_LOGGED);
            break;
        }

        case AutoTuneSubPhase::RESULT_LOGGED: {
            // UI has had a chance to display the result message
            // Now check termination conditions and prepare next iteration

            bool pulse_produced_grounds = progress.last_pulse_success;

            if (pulse_produced_grounds) {
                // Pulse successful - grounds were produced
                last_success_ms = current_pulse_ms;

                // Check termination condition
                if (found_lower_bound && step_size <= GRIND_AUTOTUNE_TARGET_ACCURACY_MS) {
                    LOG_BLE("AutoTune: Binary search complete - found boundary at %.1fms\n", last_success_ms);
                    candidate_ms = ceil(last_success_ms / 10.0f) * 10.0f;
                    LOG_BLE("AutoTune: Candidate rounded to %.1fms\n", candidate_ms);
                    log_message("\nFound %.0fms", candidate_ms);
                    switch_phase(AutoTunePhase::VERIFICATION);
                    return;
                }

                // Check for direction reversal
                if (direction == UP) {
                    step_size = step_size / 2.0f;
                    direction = DOWN;
                }

                // Move DOWN to find lower boundary
                current_pulse_ms = current_pulse_ms - step_size;

            } else {
                // Pulse failed - no grounds produced
                found_lower_bound = true;

                // Check termination condition
                if (step_size <= GRIND_AUTOTUNE_TARGET_ACCURACY_MS) {
                    if (last_success_ms == 0.0f) {
                        log_message("\nNo grounds\nCheck:");
                        log_message("- Beans loaded");
                        log_message("- Power on");
                        log_message("- Cup placed");
                        complete_with_failure("No successful pulse found");
                        return;
                    }

                    LOG_BLE("AutoTune: Binary search complete - accuracy target reached\n");
                    candidate_ms = ceil(last_success_ms / 10.0f) * 10.0f;
                    LOG_BLE("AutoTune: Candidate rounded to %.1fms\n", candidate_ms);
                    log_message("\nFound %.0fms", candidate_ms);
                    switch_phase(AutoTunePhase::VERIFICATION);
                    return;
                }

                // Check for direction reversal
                if (direction == DOWN) {
                    step_size = step_size / 2.0f;
                    direction = UP;
                }

                // Move UP to find working pulse
                current_pulse_ms = current_pulse_ms + step_size;
            }

            // Bounds checking
            current_pulse_ms = constrain(current_pulse_ms,
                                          GRIND_AUTOTUNE_LATENCY_MIN_MS,
                                          GRIND_AUTOTUNE_LATENCY_MAX_MS);

            // Check if we hit lower bound
            if (current_pulse_ms <= GRIND_AUTOTUNE_LATENCY_MIN_MS) {
                const bool min_success_confirmed = (last_success_ms > 0.0f) &&
                                                   (last_success_ms <= GRIND_AUTOTUNE_LATENCY_MIN_MS + 0.0001f);
                const bool needs_more_resolution = found_lower_bound && (step_size > GRIND_AUTOTUNE_TARGET_ACCURACY_MS);
                const bool needs_min_confirmation = !found_lower_bound && !min_success_confirmed;

                if (!needs_more_resolution && !needs_min_confirmation) {
                    if (last_success_ms == 0.0f) {
                        complete_with_failure("Hit lower bound - no successful pulse");
                        return;
                    }

                    LOG_BLE("AutoTune: Hit lower search bound\n");
                    candidate_ms = ceil(last_success_ms / 10.0f) * 10.0f;
                    LOG_BLE("AutoTune: Candidate rounded to %.1fms\n", candidate_ms);
                    log_message("\nFound %.0fms", candidate_ms);
                    switch_phase(AutoTunePhase::VERIFICATION);
                    return;
                }
            }

            iteration++;
            update_progress();

            // Ready for next iteration
            switch_sub_phase(AutoTuneSubPhase::IDLE);
            break;
        }

        default:
            break;
    }
}

void AutoTuneController::update_verification_phase() {
    switch (sub_phase) {
        case AutoTuneSubPhase::IDLE:
            // Check if we've completed all pulses in this round
            if (verification_pulse_count >= GRIND_AUTOTUNE_VERIFICATION_PULSES) {
                // Evaluate round results
                float success_rate = (float)verification_success_count / (float)GRIND_AUTOTUNE_VERIFICATION_PULSES;
                LOG_BLE("AutoTune: Verification round %d result: %d/%d (%.0f%%)\n",
                        verification_round + 1, verification_success_count, GRIND_AUTOTUNE_VERIFICATION_PULSES, success_rate * 100.0f);

                // Log pass/fail rate
                log_message("%.0f%% %s", success_rate * 100.0f,
                           success_rate >= GRIND_AUTOTUNE_SUCCESS_RATE ? "Pass [OK]" : "Fail [X]");

                if (success_rate >= GRIND_AUTOTUNE_SUCCESS_RATE) {
                    // SUCCESS
                    log_message("\nComplete!");
                    complete_with_success(candidate_ms);
                    return;
                }

                // Insufficient reliability - increase by 10ms and try again
                verification_round++;

                if (verification_round >= 5) {
                    LOG_BLE("AutoTune: Verification failed after 5 rounds\n");
                    log_message("\nFailed 5 rounds");
                    log_message("Default %.0fms", (float)GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS);
                    complete_with_failure("Failed verification after 5 rounds");
                    return;
                }

                candidate_ms = candidate_ms + GRIND_AUTOTUNE_TARGET_ACCURACY_MS;
                current_pulse_ms = candidate_ms;
                LOG_BLE("AutoTune: Increasing candidate to %.1fms for next round\n", candidate_ms);
                log_message("Retry %.0fms", candidate_ms);

                verification_pulse_count = 0;
                verification_success_count = 0;
                update_progress();
            }

            // Log verification header on first pulse
            if (verification_pulse_count == 0) {
                log_message("\nVerify %.0fms (%d/%d):", candidate_ms, verification_round + 1, 5);
            }

            // Start next verification pulse
            LOG_BLE("AutoTune: Verification round %d, pulse %d/%d (%.1fms)\n",
                    verification_round + 1, verification_pulse_count + 1,
                    GRIND_AUTOTUNE_VERIFICATION_PULSES, candidate_ms);

            current_pulse_ms = candidate_ms;
            pre_pulse_weight = weight_sensor->get_weight_high_latency();
            start_pulse(candidate_ms);
            break;

        case AutoTuneSubPhase::PULSE_EXECUTE:
            update_pulse_execute();
            break;

        case AutoTuneSubPhase::MOTOR_SETTLING:
            update_motor_settling();
            break;

        case AutoTuneSubPhase::COLLECTION_DELAY:
            update_collection_delay();
            break;

        case AutoTuneSubPhase::SCALE_SETTLING:
            update_scale_settling();
            break;

        case AutoTuneSubPhase::MEASURE_COMPLETE: {
            float settled_weight = last_settled_weight;
            float weight_delta = settled_weight - pre_pulse_weight;

            last_executed_pulse_ms = active_pulse_ms;

            bool pulse_success = (weight_delta > GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G);
            if (pulse_success) {
                verification_success_count++;
            }

            // Log verification pulse result
            log_message("%d/%d %s", verification_pulse_count + 1,
                       GRIND_AUTOTUNE_VERIFICATION_PULSES,
                       pulse_success ? "[OK]" : "[X]");

            verification_pulse_count++;
            progress.verification_success_count = verification_success_count;
            progress.last_pulse_success = pulse_success;
            update_progress();

            // Ready for next pulse
            switch_sub_phase(AutoTuneSubPhase::IDLE);
            break;
        }

        default:
            break;
    }
}

//==============================================================================
// Sub-Phase Execution (Matches GrindController Pattern)
//==============================================================================

void AutoTuneController::start_pulse(float pulse_duration_ms) {
    LOG_BLE("AutoTune: Starting pulse %.1fms\n", pulse_duration_ms);

    active_pulse_ms = pulse_duration_ms;

    grinder->start_pulse_rmt(static_cast<uint32_t>(pulse_duration_ms));

    // Notify mock driver for weight simulation
#if defined(DEBUG_ENABLE_LOADCELL_MOCK) && (DEBUG_ENABLE_LOADCELL_MOCK != 0)
    MockHX711Driver::notify_pulse(static_cast<uint32_t>(pulse_duration_ms));
#endif

    switch_sub_phase(AutoTuneSubPhase::PULSE_EXECUTE);
}

void AutoTuneController::update_pulse_execute() {
    // Non-blocking poll - returns to main loop if not complete
    if (!grinder->is_pulse_complete()) {
        return;
    }

    LOG_BLE("AutoTune: Pulse complete, motor settling\n");
    switch_sub_phase(AutoTuneSubPhase::MOTOR_SETTLING);
}

void AutoTuneController::update_motor_settling() {
    unsigned long elapsed = millis() - settling_start_time;

    if (elapsed < GRIND_MOTOR_SETTLING_TIME_MS) {
        return;  // Still settling, return to main loop
    }

    LOG_BLE("AutoTune: Motor settled, waiting for grounds collection\n");
    switch_sub_phase(AutoTuneSubPhase::COLLECTION_DELAY);
}

void AutoTuneController::update_collection_delay() {
    unsigned long elapsed = millis() - settling_start_time;

    if (elapsed < GRIND_AUTOTUNE_COLLECTION_DELAY_MS) {
        return;
    }

    LOG_BLE("AutoTune: Grounds collection wait complete, scale settling\n");
    switch_sub_phase(AutoTuneSubPhase::SCALE_SETTLING);
}

void AutoTuneController::update_scale_settling() {
    unsigned long elapsed = millis() - settling_start_time;

    // Check for timeout
    if (elapsed > GRIND_AUTOTUNE_SETTLING_TIMEOUT_MS) {
        LOG_BLE("ERROR: Scale settling timeout\n");
        complete_with_failure("Settling timeout");
        return;
    }

    // Non-blocking settling check
    float settled_weight = 0.0f;
    if (weight_sensor->check_settling_complete(GRIND_SCALE_PRECISION_SETTLING_TIME_MS, &settled_weight)) {
        LOG_BLE("AutoTune: Scale settled at %.3fg\n", settled_weight);
        last_settled_weight = settled_weight;
        switch_sub_phase(AutoTuneSubPhase::MEASURE_COMPLETE);
    }
    // Otherwise return to main loop and check again next update
}

//==============================================================================
// Tare Handling
//==============================================================================

void AutoTuneController::start_tare() {
    LOG_BLE("AutoTune: Starting tare\n");
    weight_sensor->tareNoDelay();  // Non-blocking tare
    switch_sub_phase(AutoTuneSubPhase::TARING);
}

void AutoTuneController::update_tare() {
    // Non-blocking poll
    if (!weight_sensor->getTareStatus()) {
        return;  // Still taring, return to main loop
    }

    LOG_BLE("AutoTune: Tare complete, starting binary search\n");
    last_settled_weight = 0.0f;
    pre_pulse_weight = 0.0f;
    switch_phase(AutoTunePhase::BINARY_SEARCH);
}

//==============================================================================
// Phase Transitions
//==============================================================================

void AutoTuneController::switch_phase(AutoTunePhase new_phase) {
    current_phase = new_phase;
    sub_phase = AutoTuneSubPhase::IDLE;
    phase_start_time = millis();

    if (new_phase == AutoTunePhase::VERIFICATION) {
        current_pulse_ms = candidate_ms;
    }

    LOG_BLE("AutoTune: Phase transition → %s\n", get_phase_name(new_phase));
    update_progress();
}

void AutoTuneController::switch_sub_phase(AutoTuneSubPhase new_sub_phase) {
    sub_phase = new_sub_phase;
    settling_start_time = millis();
}

//==============================================================================
// Completion
//==============================================================================

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

    // Close log file with completion summary
    if (autotune_log_file) {
        autotune_log_file.println();
        autotune_log_file.println("=== Autotune Complete: SUCCESS ===");
        autotune_log_file.printf("Final Latency: %.1fms\n", final_latency_ms);
        autotune_log_file.printf("Previous Latency: %.1fms\n", progress.previous_latency_ms);
        autotune_log_file.close();
        LOG_BLE("AutoTune: Log file closed\n");
    }

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

    // Close log file with failure summary
    if (autotune_log_file) {
        autotune_log_file.println();
        autotune_log_file.println("=== Autotune Complete: FAILURE ===");
        autotune_log_file.printf("Error: %s\n", error_msg ? error_msg : "Unknown");
        autotune_log_file.printf("Using Default Latency: %.1fms\n", GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS);
        autotune_log_file.close();
        LOG_BLE("AutoTune: Log file closed\n");
    }

    is_running = false;
}

//==============================================================================
// Progress Tracking
//==============================================================================

void AutoTuneController::update_progress() {
    progress.phase = current_phase;
    progress.iteration = iteration;
    progress.current_pulse_ms = current_pulse_ms;
    progress.last_pulse_ms = last_executed_pulse_ms;
    progress.step_size_ms = step_size;
    progress.verification_round = verification_round;

    LOG_BLE("AutoTune Progress: Phase=%s, Iteration=%d, Pulse=%.1fms, Step=%.1fms\n",
            get_phase_name(current_phase), iteration, current_pulse_ms, step_size);
}

void AutoTuneController::log_message(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(progress.last_message, sizeof(progress.last_message), format, args);
    va_end(args);
    progress.has_new_message = true;

    // Write to log file
    if (autotune_log_file) {
        autotune_log_file.println(progress.last_message);
        autotune_log_file.flush();
    }

    // Also log to BLE for debugging
    LOG_BLE("AutoTune Console: %s\n", progress.last_message);
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
