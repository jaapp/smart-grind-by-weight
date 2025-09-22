#include "grind_controller.h"
#include "grind_events.h"
#include "../hardware/circular_buffer_math/circular_buffer_math.h"
#include "../config/logging.h"
#include "../config/user_config.h"
#include <Arduino.h>
#include <cstdarg>

// UI event queue size
#define UI_EVENT_QUEUE_SIZE 10

// Flash operation queue size
#define FLASH_OP_QUEUE_SIZE 5

void GrindController::init(WeightSensor* lc, Grinder* gr, Preferences* prefs) {
    weight_sensor = lc;
    grinder = gr;
    preferences = prefs;
    phase = GrindPhase::IDLE;
    tolerance = USER_GRIND_ACCURACY_TOLERANCE_G;
    current_profile_id = 0;
    force_measurement_log = false;
    
    // Set up grinder background indicator callback (if enabled)
    if (grinder) {
#if USER_ENABLE_GRINDER_BACKGROUND_INDICATOR
        grinder->set_ui_event_callback([this](const GrindEventData& event_data) {
            // Forward grinder background events to our existing UI event queue
            if (event_data.event == UIGrindEvent::BACKGROUND_CHANGE) {
                this->emit_ui_event(event_data);
            }
        });
#else
        // Background indicator disabled - set empty callback
        grinder->set_ui_event_callback(nullptr);
#endif
    }
    
    // Initialize the grind logger
    if (!grind_logger.init(preferences)) {
        BLE_LOG("Warning: Grind logging disabled due to initialization failure\n");
    }
    
    // RealtimeController removed - functionality moved to FreeRTOS WeightSamplingTask and GrindControlTask
    
    // Initialize UI event system
    ui_event_callback = nullptr;
    ui_ready_for_setup = false;
    
    // Initialize thread-safe UI event queue
    ui_event_queue = xQueueCreate(UI_EVENT_QUEUE_SIZE, sizeof(GrindEventData));
    if (!ui_event_queue) {
        BLE_LOG("ERROR: Failed to create UI event queue\n");
    } else {
        BLE_LOG("UI event queue created successfully\n");
    }
    
    // Initialize thread-safe flash operation queue
    flash_op_queue = xQueueCreate(FLASH_OP_QUEUE_SIZE, sizeof(FlashOpRequest));
    if (!flash_op_queue) {
        BLE_LOG("ERROR: Failed to create flash operation queue\n");
    } else {
        BLE_LOG("Flash operation queue created successfully\n");
    }
    
    // Initialize thread-safe log message queue
    log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogMessage));
    if (!log_queue) {
        BLE_LOG("ERROR: Failed to create log message queue\n");
    } else {
        BLE_LOG("Log message queue created successfully\n");
    }
}

void GrindController::start_grind(float target) {
    BLE_LOG("[%lums CONTROLLER] start_grind() called with target=%.1fg\n", millis(), target);
    if (!weight_sensor || !grinder) return;
    
    target_weight = target;
    start_time = millis();
    pulse_attempts = 0;
    timeout_phase = GrindPhase::IDLE; // Initialize timeout phase
    // Load cell now runs at constant high speed - no mode switching needed
    
    
    grind_latency_ms = 0;
    coast_time_ms = 0;
    predictive_end_weight = 0;
    final_weight = 0;
    motor_stop_target_weight = USER_GRIND_UNDERSHOOT_TARGET_G; // Start with a safe default

    flow_start_confirmed = false;

    // Initialize dynamic pulse algorithm variables
    pulse_flow_rate = 0.0f;
    
    // Initialize loop counters
    current_phase_loop_count = 0;
    
    // Initialize measurement state tracking for logger
    last_logged_weight = 0.0f;
    last_logged_time = millis();
    force_measurement_log = false;
    
    // Reset UI acknowledgment flag for new grind
    ui_ready_for_setup = false;
    
    // Reset flash operation flag for new grind
    session_end_flash_queued = false;
    
    // Start with INITIALIZING phase - this emits immediate UI event
    // Create minimal loop_data for initial phase transition
    GrindLoopData loop_data = {};
    loop_data.now = millis();
    loop_data.timestamp_ms = loop_data.now - start_time;
    loop_data.current_weight = weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f;
    
    switch_phase(GrindPhase::INITIALIZING, loop_data);
}

void GrindController::user_tare_request() {
    // Tare request is now handled automatically when grinding starts
    // This function is kept for compatibility but does nothing
}

void GrindController::return_to_idle() {
    // This is called by the UI to acknowledge a completed or timed-out grind
    // and return the controller to the IDLE state.
    if (phase == GrindPhase::COMPLETED || phase == GrindPhase::TIMEOUT) {
        BLE_LOG("[%lums CONTROLLER] UI acknowledged completion/timeout, returning to IDLE.\n", millis());
        switch_phase(GrindPhase::IDLE);  // No loop_data needed for IDLE transition
    }
    // If already IDLE, do nothing. If in another active state, this method shouldn't be called.
}

void GrindController::stop_grind() {
    if (!grinder) return;
    
    grinder->stop();
    
    // Cancelled grinds just discard PSRAM data and go to IDLE
    grind_logger.discard_current_session();
    
    BLE_LOG("--- GRIND STOPPED BY USER ---\n");
    
    switch_phase(GrindPhase::IDLE);  // No loop_data needed for IDLE transition
}

void GrindController::update() {
    if (!is_active()) return;
    
    // Increment loop counter for current phase performance tracking
    current_phase_loop_count = current_phase_loop_count + 1;
    
    unsigned long now = millis();
    
    // Calculate all measurement values once at the start - pass to methods to avoid redundant calculations
    GrindLoopData loop_data = {};
    
    // Always calculate these values
    loop_data.display_weight = weight_sensor ? weight_sensor->get_display_weight() : 0.0f;
    loop_data.current_weight = weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f;
    loop_data.now = now;
    loop_data.timestamp_ms = now - start_time;  // Relative to session start
    loop_data.motor_is_on = grinder ? (grinder->is_grinding() ? 1 : 0) : 0;
    loop_data.phase_id = get_current_phase_id();
    loop_data.weight_delta = loop_data.current_weight - last_logged_weight;
    loop_data.flow_rate = weight_sensor ? weight_sensor->get_flow_rate() : 0.0f;
    
    switch (phase) {
        case GrindPhase::INITIALIZING:
            // Wait for UI to acknowledge the phase transition before proceeding
            if (ui_ready_for_setup) {
                UI_DEBUG_LOG("UI acknowledged INITIALIZING phase, proceeding to SETUP\n");
                switch_phase(GrindPhase::SETUP, loop_data);
            }
            break;
            
        case GrindPhase::SETUP: {
            // Start logging immediately (synchronous PSRAM setup only)
            grind_logger.start_grind_session(target_weight, current_profile_id, tolerance);
            
            // Initialize logging event for upcoming TARING phase
            memset(&event_in_progress, 0, sizeof(GrindEvent));
            
            switch_phase(GrindPhase::TARING, loop_data);
            break;
        }
            
        case GrindPhase::TARING:
            if (weight_sensor->start_nonblocking_tare()) {
                LOADCELL_DEBUG_LOG("Non-blocking tare started\n");
                switch_phase(GrindPhase::TARE_CONFIRM, loop_data);
            }
            break;
            
        case GrindPhase::TARE_CONFIRM:
            // Check if tare is complete
            if (!weight_sensor->is_tare_in_progress()) {
                // Double confirm weights are settled
                if (weight_sensor->is_settled()) {
                    grinder->start();  // Start motor
                    switch_phase(GrindPhase::PREDICTIVE, loop_data);
                }
            }
            break;

        case GrindPhase::PREDICTIVE:
            // Inline predictive control logic
            if (predictive_control(loop_data)) {
                switch_phase(GrindPhase::PULSE_SETTLING, loop_data);
            }
            break;
            
        case GrindPhase::PULSE_DECISION:
            pulse_control(loop_data);
            break;

        case GrindPhase::PULSE_EXECUTE:
            if (grinder->is_pulse_complete()) {
                switch_phase(GrindPhase::PULSE_SETTLING, loop_data);
            }
            break;

        case GrindPhase::PULSE_SETTLING:
            // Wait minimum grind_latency_ms + HW_MOTOR_SETTLING_TIME_MS before checking weight settling
            if (loop_data.now - phase_start_time >= grind_latency_ms + HW_MOTOR_SETTLING_TIME_MS) {
                // After minimum latency, wait for weight to settle with motor settling window
                if (weight_sensor->check_settling_complete(HW_MOTOR_SETTLING_TIME_MS)) {
                    switch_phase(GrindPhase::PULSE_DECISION, loop_data);
                }
            }
            break;

        case GrindPhase::FINAL_SETTLING:
            // Wait for weight to settle with precision settling window
            if (weight_sensor->check_settling_complete(HW_SCALE_PRECISION_SETTLING_TIME_MS)) {
                final_measurement(loop_data);
            }
            break;
            
        case GrindPhase::COMPLETED:
            if (grind_logger.is_logging_active() && !session_end_flash_queued) {
                float error = final_weight - target_weight;
    
                // Determine result for logging and reporting
                const char* result_string;
                if (error > tolerance) { 
                    result_string = "OVERSHOOT";
                    BLE_LOG("--- RESULT: OVERSHOOT (Error: %+.2fg) ---\n", error);
                } else if (pulse_attempts >= USER_GRIND_MAX_PULSE_ATTEMPTS && abs(error) > tolerance) { // abs() is correct here
                    result_string = "COMPLETE - MAX PULSES";
                    BLE_LOG("--- RESULT: COMPLETE - MAX PULSES (Error: %+.2fg) ---\n", error);
                } else {
                    result_string = "COMPLETE";
                    BLE_LOG("--- RESULT: COMPLETE (Error: %+.2fg) ---\n", error);
                }

                // Queue flash operation for Core 1 processing - no blocking on Core 0
                FlashOpRequest request = {};
                request.operation_type = FlashOpRequest::END_GRIND_SESSION;
                strncpy(request.result_string, result_string, sizeof(request.result_string) - 1);
                request.final_weight = final_weight;
                request.pulse_count = pulse_attempts;
                queue_flash_operation(request);
                
                // Mark flash operation as queued to prevent repeated calls
                session_end_flash_queued = true;
            }
            break;
            
        case GrindPhase::TIMEOUT:
            if (grind_logger.is_logging_active() && !session_end_flash_queued) {
                // Queue flash operation for Core 1 processing - no blocking on Core 0
                FlashOpRequest request = {};
                request.operation_type = FlashOpRequest::END_GRIND_SESSION;
                strncpy(request.result_string, "TIMEOUT", sizeof(request.result_string) - 1);
                request.final_weight = final_weight;
                request.pulse_count = pulse_attempts;
                queue_flash_operation(request);
                
                // Mark flash operation as queued to prevent repeated calls
                session_end_flash_queued = true;
            }
            break;
            
        default:
            break;
    }
    
    // Unified continuous logging for ALL active phases at the control loop rate
    if (should_log_measurements()) {
        grind_logger.log_continuous_measurement(loop_data.timestamp_ms, loop_data.current_weight, loop_data.weight_delta, 
                                               loop_data.flow_rate, loop_data.motor_is_on, loop_data.phase_id, motor_stop_target_weight);
        
        // Update tracking variables for next measurement
        last_logged_weight = loop_data.current_weight;
        last_logged_time = loop_data.now;
        force_measurement_log = false;
    }
    
    // Emit progress update events every cycle for responsive UI
    GrindEventData progress_event = {};
    progress_event.event = UIGrindEvent::PROGRESS_UPDATED;
    progress_event.phase = phase;
    progress_event.current_weight = (phase == GrindPhase::COMPLETED || phase == GrindPhase::TIMEOUT) 
                                    ? final_weight 
                                    : loop_data.display_weight;
    progress_event.progress_percent = get_progress_percent();
    progress_event.phase_display_text = get_phase_name();
    progress_event.show_taring_text = show_taring_text();
    progress_event.flow_rate = loop_data.flow_rate;
    emit_ui_event(progress_event);

    // Check for negative weight failsafe after TARE_CONFIRM phase during active grinding
    if (phase != GrindPhase::COMPLETED && phase != GrindPhase::TIMEOUT && 
        phase != GrindPhase::IDLE && phase != GrindPhase::INITIALIZING &&
        phase != GrindPhase::SETUP && phase != GrindPhase::TARING && 
        phase != GrindPhase::TARE_CONFIRM && loop_data.current_weight < -1.0f) {
        timeout_phase = phase;
        grinder->stop();
        
        queue_log_message("--- NEGATIVE WEIGHT FAILSAFE TRIGGERED: %.2fg in phase %s ---\n", 
                         loop_data.current_weight, get_phase_name(timeout_phase));
        switch_phase(GrindPhase::TIMEOUT, loop_data);
    }
    // Only check timeout during active grinding phases, not during completion states
    else if (phase != GrindPhase::COMPLETED && phase != GrindPhase::TIMEOUT && check_timeout()) {
        timeout_phase = phase;
        grinder->stop();
        
        queue_log_message("--- GRIND TIMEOUT in phase %s ---\n", get_phase_name(timeout_phase));
        switch_phase(GrindPhase::TIMEOUT, loop_data);
    }
}

// OLD predictive_grind method removed - logic now inline in update()

void GrindController::pulse_control(const GrindLoopData& loop_data) {
    // Use non-blocking precision settling check instead of blocking call
    float settled_weight;
    if (!weight_sensor->check_settling_complete(HW_SCALE_PRECISION_SETTLING_TIME_MS, &settled_weight)) {
        // Still settling, wait for next cycle
        return;
    }
    
    // Weight has settled with precision window, proceed with pulse decision
    float current_weight = settled_weight;
    // Use conservative target that biases toward undershoot (since overshoot is unrecoverable)
    float conservative_target = target_weight - USER_GRIND_ACCURACY_TOLERANCE_G;
    float error = conservative_target - current_weight;
    
    if (coast_time_ms == 0) { 
        coast_time_ms = SYS_TIMEOUT_MEDIUM_MS;
    }
    
    if (target_weight - current_weight < USER_GRIND_ACCURACY_TOLERANCE_G || pulse_attempts >= USER_GRIND_MAX_PULSE_ATTEMPTS) {
        switch_phase(GrindPhase::FINAL_SETTLING, loop_data);
        return;
    }
    
    // Record pulse history for summary report
    pulse_history[pulse_attempts].start_weight = current_weight;
    pulse_history[pulse_attempts].end_weight = current_weight; // Will be updated after pulse
    pulse_history[pulse_attempts].settle_time_ms = coast_time_ms;
    current_pulse_duration_ms = calculate_pulse_duration_ms(error);
    pulse_history[pulse_attempts].duration_ms = current_pulse_duration_ms;
    
    // SWITCH PHASE FIRST to prevent re-entry
    switch_phase(GrindPhase::PULSE_EXECUTE, loop_data);
    grinder->start_pulse_rmt((uint32_t)current_pulse_duration_ms);
    
#if HW_LOADCELL_USE_MOCK_DRIVER
    // Simulate pulse in mock driver for testing
    if (weight_sensor && weight_sensor->get_adc_driver()) {
        weight_sensor->get_adc_driver()->simulate_pulse((uint32_t)current_pulse_duration_ms);
    }
#endif
    
    pulse_attempts++;
}

void GrindController::final_measurement(const GrindLoopData& loop_data) {
    final_weight = weight_sensor->get_weight_high_latency();

    // Switch to COMPLETED. The state machine will then transition to IDLE on the next tick.
    switch_phase(GrindPhase::COMPLETED, loop_data);
}


float GrindController::calculate_pulse_duration_ms(float error_grams) {
    // Get effective flow rate with safety checks
    float effective_flow_rate = get_effective_flow_rate();
    
    // Apply error magnitude factor (be gentler for small errors)
    float error_factor = (error_grams < 0.05f) ? SYS_GRIND_SMALL_ERROR_FACTOR : 1.0f;
#if ENABLE_GRIND_DEBUG
    queue_log_message("[DYNAMIC_DEBUG] EffectiveFlowRate: %.3fg/s, ErrorFactor: %.2f (small_error: %s)\n", 
            effective_flow_rate, error_factor, (error_grams < 0.1f) ? "YES" : "NO");
#endif
    
    // Calculate base duration from flow rate
    float base_duration = (error_grams * error_factor / effective_flow_rate) * 1000.0f;
#if ENABLE_GRIND_DEBUG
    queue_log_message("[DYNAMIC_DEBUG] BaseDuration: %.1fms (%.3fg * %.2f / %.3fg/s * 1000)\n", 
            base_duration, error_grams, error_factor, effective_flow_rate);
#endif
    
    // No pulse effectiveness learning - keep it simple
    
    // Apply safety bounds
    float final_duration = max(HW_MOTOR_MIN_PULSE_DURATION_MS, min(base_duration, HW_MOTOR_MAX_PULSE_DURATION_MS));
#if ENABLE_GRIND_DEBUG
    queue_log_message("[DYNAMIC_DEBUG] FinalDuration: %.1fms (bounded between %.1f-%.1f)\n", 
            final_duration, HW_MOTOR_MIN_PULSE_DURATION_MS, HW_MOTOR_MAX_PULSE_DURATION_MS);
#endif
    
    return final_duration;
}

float GrindController::get_effective_flow_rate() const {
    float flow_rate = pulse_flow_rate; // Use ONLY the reliable predictive phase measurement
    
    // Apply safety bounds to prevent extreme calculations
    if (flow_rate < HW_FLOW_RATE_MIN_SANE_GPS) {
        flow_rate = HW_FLOW_RATE_REFERENCE_GPS; // Use reference if too low
    } else if (flow_rate > HW_FLOW_RATE_MAX_SANE_GPS) {
        flow_rate = HW_FLOW_RATE_MAX_SANE_GPS; // Cap if too high
    }
    
    return flow_rate;
}


void GrindController::switch_phase(GrindPhase new_phase, const GrindLoopData& loop_data) {
    if (phase == new_phase) return;

    // Check if we have valid loop_data (non-zero timestamp indicates valid data)
    bool has_loop_data = (loop_data.now > 0);
    unsigned long now = has_loop_data ? loop_data.now : millis();
    
#if ENABLE_GRIND_DEBUG
    // DEBUG: Log phase transition with boot time and proper phase duration
    unsigned long phase_duration = (phase_start_time > 0) ? (now - phase_start_time) : 0;
    queue_log_message("[DEBUG %lums] PHASE_CHANGE: %s -> %s (phase duration: %lums)\n", 
                  now, get_phase_name(), get_phase_name(new_phase), phase_duration);
#endif
    
    // Finalize and log the event for the phase that just ENDED (only when we have loop_data)
    if (has_loop_data && grind_logger.is_logging_active() && phase != GrindPhase::IDLE) {
        event_in_progress.duration_ms = now - phase_start_time;
        event_in_progress.end_weight = loop_data.current_weight;  // Use pre-calculated weight
        
        // Populate context-specific data for the completed event
        if (phase == GrindPhase::PREDICTIVE) {
            event_in_progress.motor_stop_target_weight = motor_stop_target_weight;
            event_in_progress.grind_latency_ms = grind_latency_ms;
            event_in_progress.pulse_flow_rate = pulse_flow_rate;
            event_in_progress.loop_count = current_phase_loop_count;
        } else if (phase == GrindPhase::PULSE_EXECUTE) {
            event_in_progress.pulse_duration_ms = current_pulse_duration_ms;
            event_in_progress.pulse_attempt_number = pulse_attempts; // attempts is 1-based
            event_in_progress.pulse_flow_rate = pulse_flow_rate;
            event_in_progress.loop_count = current_phase_loop_count;
        } else if (phase == GrindPhase::PULSE_DECISION) {
            event_in_progress.pulse_flow_rate = pulse_flow_rate;
            event_in_progress.loop_count = current_phase_loop_count;
        } else if (phase == GrindPhase::PULSE_SETTLING || phase == GrindPhase::FINAL_SETTLING) {
            event_in_progress.settling_duration_ms = event_in_progress.duration_ms;
            event_in_progress.pulse_flow_rate = pulse_flow_rate;
            event_in_progress.loop_count = current_phase_loop_count;
        }
        
        // For all other phases, just log the general loop count
        if (phase != GrindPhase::PREDICTIVE && phase != GrindPhase::PULSE_EXECUTE && 
            phase != GrindPhase::PULSE_DECISION && phase != GrindPhase::PULSE_SETTLING && 
            phase != GrindPhase::FINAL_SETTLING) {
            event_in_progress.loop_count = current_phase_loop_count;
        }

        grind_logger.log_event(event_in_progress);
    }
    
    // Update phase state
    phase = new_phase;
    phase_start_time = now;
    
    // Reset loop counter for new phase
    current_phase_loop_count = 0;

    // Set flag to force measurement logging in next update() cycle to capture exact motor state transition
    force_measurement_log = true;

    // Start a new event for the NEW phase (only when we have loop_data and not going to IDLE)
    if (has_loop_data && new_phase != GrindPhase::IDLE) {
        memset(&event_in_progress, 0, sizeof(GrindEvent));
        event_in_progress.phase_id = (uint8_t)new_phase;
        event_in_progress.timestamp_ms = loop_data.timestamp_ms;  // Use pre-calculated timestamp for perfect alignment
        event_in_progress.start_weight = loop_data.current_weight;  // Use pre-calculated weight
    }
    
    // pulse_start_time no longer needed for RMT-based pulses
    
    // Emit UI event for phase change
    GrindEventData event_data = {};
    event_data.event = UIGrindEvent::PHASE_CHANGED;
    event_data.phase = new_phase;
    event_data.current_weight = weight_sensor ? weight_sensor->get_display_weight() : 0.0f;
    event_data.progress_percent = get_progress_percent();
    event_data.phase_display_text = get_phase_name(new_phase);
    event_data.show_taring_text = show_taring_text();
    
    // Special handling for completion and timeout events
    if (new_phase == GrindPhase::COMPLETED) {
        event_data.event = UIGrindEvent::COMPLETED;
        // Use final_weight if available (from final_measurement), otherwise use high latency weight
        event_data.final_weight = (final_weight > 0) ? final_weight : 
                                 (weight_sensor ? weight_sensor->get_weight_high_latency() : 0.0f);
    } else if (new_phase == GrindPhase::TIMEOUT) {
        event_data.event = UIGrindEvent::TIMEOUT;
        event_data.timeout_phase = get_phase_name(timeout_phase);
        // Use non-blocking high latency weight instead of precision settled weight
        event_data.timeout_weight = weight_sensor ? weight_sensor->get_weight_high_latency() : 0.0f;
        event_data.timeout_progress = get_progress_percent();
    } else if (new_phase == GrindPhase::IDLE) {
        event_data.event = UIGrindEvent::STOPPED;
    }
    
    emit_ui_event(event_data);
}


bool GrindController::check_timeout() const {
    return (millis() - start_time) >= (USER_GRIND_TIMEOUT_SEC * 1000);
}

bool GrindController::is_active() const {
    return phase != GrindPhase::IDLE;
}

int GrindController::get_progress_percent() const {
    if (target_weight <= 0) return 0;
    
    float ground = (phase == GrindPhase::COMPLETED || phase == GrindPhase::TIMEOUT) 
                   ? final_weight 
                   : (weight_sensor ? weight_sensor->get_display_weight() : 0.0f);
    if (ground < 0) ground = 0;
    int progress = (int)((ground / target_weight) * 100);
    return min(progress, 100);
}

float GrindController::get_grind_time() const {
    if (phase == GrindPhase::IDLE || start_time == 0) return 0.0f;
    return (millis() - start_time) / (float)SYS_MS_PER_SECOND;
}

const char* GrindController::get_phase_name(GrindPhase p) const {
    // Use provided phase, or current phase if not specified
    GrindPhase phase_to_check = (p == static_cast<GrindPhase>(-1)) ? phase : p;
    
    switch (phase_to_check) {
        case GrindPhase::IDLE: return "IDLE";
        case GrindPhase::INITIALIZING: return "INITIALIZING";
        case GrindPhase::SETUP: return "SETUP";
        case GrindPhase::TARING: return "TARING";
        case GrindPhase::TARE_CONFIRM: return "TARE_CONFIRM";
        case GrindPhase::PREDICTIVE: return "PREDICTIVE";
        case GrindPhase::PULSE_DECISION: return "PULSE_DECISION";
        case GrindPhase::PULSE_EXECUTE: return "PULSE_EXECUTE";
        case GrindPhase::PULSE_SETTLING: return "PULSE_SETTLING";
        case GrindPhase::FINAL_SETTLING: return "FINAL_SETTLING";
        case GrindPhase::COMPLETED: return "COMPLETED";
        case GrindPhase::TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}



uint8_t GrindController::get_current_phase_id() const {
    return (uint8_t)phase;
}


void GrindController::send_measurements_data() {
    grind_logger.send_current_session_via_serial();
}

float GrindController::get_current_flow_rate() const {
    return weight_sensor->get_flow_rate(); 
}

void GrindController::set_ui_event_callback(void (*callback)(const GrindEventData&)) {
    ui_event_callback = callback;
}

void GrindController::ui_acknowledge_phase_transition() {
    if (phase == GrindPhase::INITIALIZING) {
        ui_ready_for_setup = true;
        UI_DEBUG_LOG("UI acknowledged INITIALIZING phase transition\n");
    }
}

void GrindController::emit_ui_event(const GrindEventData& data) {
    // Thread-safe Core 0 → Core 1 UI event emission using FreeRTOS queue
    if (ui_event_queue) {
        BaseType_t result = xQueueSend(ui_event_queue, &data, 0); // 0 = no wait (non-blocking)
        
        if (result != pdPASS) {
            // Queue full - drop event to prevent Core 0 blocking
            // Only log dropped significant events, not progress updates
            if (data.event != UIGrindEvent::PROGRESS_UPDATED) {
                BLE_LOG("WARNING: UI event queue full, dropped event type %d\n", (int)data.event);
            }
        } else {
            // Only log significant queued events, not every progress update
            if (data.event != UIGrindEvent::PROGRESS_UPDATED) {
                const char* event_name = "UNKNOWN";
                switch(data.event) {
                    case UIGrindEvent::PHASE_CHANGED: event_name = "PHASE_CHANGED"; break;
                    case UIGrindEvent::PROGRESS_UPDATED: event_name = "PROGRESS_UPDATED"; break;
                    case UIGrindEvent::COMPLETED: event_name = "COMPLETED"; break;
                    case UIGrindEvent::TIMEOUT: event_name = "TIMEOUT"; break;
                    case UIGrindEvent::STOPPED: event_name = "STOPPED"; break;
                    case UIGrindEvent::BACKGROUND_CHANGE: event_name = "BACKGROUND_CHANGE"; break;
                }
                BLE_LOG("[%lums UI_EVENT] QUEUED %s: phase=%s, weight=%.2fg, progress=%d%%\n", 
                        millis(), event_name, data.phase_display_text, data.current_weight, data.progress_percent);
            }
        }
    }
}

//==============================================================================
// CORE 0 HELPER METHODS
//==============================================================================


bool GrindController::predictive_control(const GrindLoopData& loop_data) {
    // Inline predictive grind logic (previously in predictive_grind method)
    
    // Step 1: Poll flowrate over a 500ms window to detect flow start.
    if (!flow_start_confirmed) {
        const uint32_t flow_detection_window_ms = 500;
        float current_flow_rate = weight_sensor->get_flow_rate(flow_detection_window_ms);

        if (current_flow_rate >= USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
            // Flow detected, calculate latency from the start of the predictive phase.
            grind_latency_ms = loop_data.now - phase_start_time;
            flow_start_confirmed = true;
            BLE_LOG("[PREDICTIVE] Flow start CONFIRMED! Latency: %lums, Flow: %.2fg/s\n", grind_latency_ms, current_flow_rate);
        }
    }

    // Once latency is known (flow start confirmed), continuously update the dynamic goal based on the current flow rate.
    if (flow_start_confirmed) {
        const uint32_t flow_rate_calc_window_ms = 1500;

        // Wait for stable flow rate calculation window
        if (loop_data.now > (phase_start_time + grind_latency_ms + flow_rate_calc_window_ms)) {
            float current_flow_rate = weight_sensor->get_flow_rate(flow_rate_calc_window_ms);
            
            if (current_flow_rate > USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS) {
                // Also applying USER_LATENCY_TO_COAST_RATIO ratio to account for expected difference between initial latency and final coast
                motor_stop_target_weight = ((grind_latency_ms * USER_LATENCY_TO_COAST_RATIO) / (float)SYS_MS_PER_SECOND) * current_flow_rate;
            }
        }
    }
    
    // Check if motor stop target weight reached - return true to indicate grinder should be stopped
    if (loop_data.current_weight >= (target_weight - motor_stop_target_weight)) {
        grinder->stop();
        predictive_end_weight = loop_data.current_weight;
        
        // Store final 95th percentile flow rate for pulse algorithm - only needed when exiting predictive phase
        pulse_flow_rate = weight_sensor->get_flow_rate_95th_percentile(2500);
        
        return true;  // Signal that predictive phase should end
    }
    
    return false;  // Continue grinding
}



bool GrindController::should_log_measurements() const {
    return SYS_CONTINUOUS_LOGGING_ENABLED
        && phase != GrindPhase::INITIALIZING 
        && phase != GrindPhase::SETUP
        && phase != GrindPhase::COMPLETED
        && phase != GrindPhase::TIMEOUT;
}

void GrindController::process_queued_ui_events() {
    GrindEventData event;
    
    // Process all queued events from Core 0
    while (xQueueReceive(ui_event_queue, &event, 0) == pdPASS) {
        if (ui_event_callback) {
            ui_event_callback(event); // Safe - runs on Core 1
        }
    }
}

void GrindController::queue_flash_operation(const FlashOpRequest& request) {
    // Thread-safe Core 0 → Core 1 flash operation queuing
    if (flash_op_queue) {
        BaseType_t result = xQueueSend(flash_op_queue, &request, 0); // 0 = no wait (non-blocking)
        
        if (result != pdPASS) {
            // Queue full - this shouldn't happen with reasonable queue size
            BLE_LOG("WARNING: Flash operation queue full, dropping request type %d\n", (int)request.operation_type);
        } else {
            const char* op_name = (request.operation_type == FlashOpRequest::END_GRIND_SESSION)
                                   ? "END_GRIND_SESSION" : "START_GRIND_SESSION";
            BLE_LOG("[%lums FLASH_OP] QUEUED %s operation for Core 1 processing\n", millis(), op_name);
        }
    }
}

void GrindController::process_queued_flash_operations() {
    FlashOpRequest request;
    
    // Process all queued flash operations from Core 0
    while (xQueueReceive(flash_op_queue, &request, 0) == pdPASS) {
        switch (request.operation_type) {
            case FlashOpRequest::START_GRIND_SESSION:
                // Perform the blocking flash operation on Core 1
                BLE_LOG("[%lums FLASH_OP] Processing START_GRIND_SESSION on Core 1: target=%.2fg, profile=%d, tolerance=%.3fg\n", 
                        millis(), request.target_weight, request.profile_id, request.tolerance);
                grind_logger.start_grind_session(request.target_weight, request.profile_id, request.tolerance);
                break;
                
            case FlashOpRequest::END_GRIND_SESSION:
                // Perform the blocking flash operation on Core 1
                BLE_LOG("[%lums FLASH_OP] Processing END_GRIND_SESSION on Core 1: %s, %.2fg, %d pulses\n", 
                        millis(), request.result_string, request.final_weight, request.pulse_count);
                grind_logger.end_grind_session(request.result_string, request.final_weight, request.pulse_count);
                break;
                
            default:
                BLE_LOG("WARNING: Unknown flash operation type %d\n", request.operation_type);
                break;
        }
    }
}

void GrindController::queue_log_message(const char* format, ...) {
    // Thread-safe Core 0 → Core 1 log message queuing
    if (log_queue) {
        LogMessage log_msg;
        
        // Format the message using va_list
        va_list args;
        va_start(args, format);
        vsnprintf(log_msg.message, sizeof(log_msg.message), format, args);
        va_end(args);
        
        // Ensure null termination
        log_msg.message[sizeof(log_msg.message) - 1] = '\0';
        
        BaseType_t result = xQueueSend(log_queue, &log_msg, 0); // 0 = no wait (non-blocking)
        
        if (result != pdPASS) {
            // Queue full - silently drop the message to avoid blocking Core 0
            // Don't log this error as it could cause recursion
        }
    }
}

void GrindController::process_queued_log_messages() {
    LogMessage log_msg;
    
    // Process all queued log messages from Core 0
    while (xQueueReceive(log_queue, &log_msg, 0) == pdPASS) {
        // Output the message using BLE_LOG on Core 1
        BLE_LOG("%s", log_msg.message);
    }
}
