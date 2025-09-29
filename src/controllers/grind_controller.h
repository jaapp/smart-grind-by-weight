#pragma once
#include "../config/constants.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../logging/grind_logging.h"
#include "grind_mode.h"
#include "grind_session.h"
#include "grind_strategy.h"
#include "weight_grind_strategy.h"
#include "time_grind_strategy.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Forward declaration to avoid circular dependency
struct GrindEventData;


// Flash operation request structure for Core 0 → Core 1 communication
struct FlashOpRequest {
    enum Type {
        START_GRIND_SESSION,
        END_GRIND_SESSION
    };
    
    Type operation_type;
    GrindSessionDescriptor descriptor; // For START_GRIND_SESSION
    char result_string[32];  // "COMPLETE", "TIMEOUT", "OVERSHOOT", etc. (for END_GRIND_SESSION)
    float start_weight;      // For START_GRIND_SESSION (pre-tare snapshot)
    float final_weight;      // For END_GRIND_SESSION
    uint8_t pulse_count;     // For END_GRIND_SESSION
};

// Log message structure for Core 0 → Core 1 communication
struct LogMessage {
    char message[128];  // Pre-formatted log message
};

// Calculated values for a single update cycle - passed to methods to avoid redundant calculations
struct GrindLoopData {
    float current_weight;       // For control logic (low_latency)
    float display_weight;       // For UI display (always calculated)
    uint32_t timestamp_ms;
    float weight_delta;
    float flow_rate;
    uint8_t motor_is_on;
    uint8_t phase_id;
    unsigned long now;
};

// Grind controller state machine phases
enum class GrindPhase {
    IDLE,               // Not grinding
    INITIALIZING,       // Pre-initialization - emit UI event and prepare for grind
    SETUP,              // Initialization - file system operations, logger setup
    TARING,             // Performing tare operation
    TARE_CONFIRM,       // Confirming tare completed
    PREDICTIVE,         // Main grinding with flow prediction
    PULSE_DECISION,     // Deciding if pulse correction needed
    PULSE_EXECUTE,      // Executing precision pulse
    PULSE_SETTLING,     // Waiting for weight to settle after pulse
    FINAL_SETTLING,     // Waiting for weight to settle
    TIME_GRINDING,      // Time-based grinding phase
    TIME_ADDITIONAL_PULSE, // Additional pulse in time mode after completion
    COMPLETED,          // Grind completed (success, overshoot, or max pulses)
    TIMEOUT             // Grind timed out
};


struct PulseReport {
    float start_weight;
    float end_weight;
    float duration_ms;
};



// Controls the grinding process with predictive weight stopping and precision pulse corrections
class GrindController {
private:
    friend class WeightGrindStrategy;
    friend class TimeGrindStrategy;

    WeightSensor* weight_sensor;
    Grinder* grinder;
    Preferences* preferences;
    float target_weight;
    uint32_t target_time_ms;
    GrindPhase phase;
    unsigned long start_time;
    unsigned long phase_start_time;
    unsigned long time_grind_start_ms;
    
    float tolerance;
    GrindMode mode;
    
    // Timeout tracking
    GrindPhase timeout_phase;   // Phase when timeout occurred
    
    int pulse_attempts;
    unsigned long pulse_start_time;
    float current_pulse_duration_ms;
    
    
    
    float predictive_end_weight;
    volatile float grind_latency_ms;        // Thread-safe for Core 0 access
    PulseReport pulse_history[GRIND_MAX_PULSE_ATTEMPTS];
    volatile float motor_stop_target_weight; // Thread-safe for Core 0 access
    float final_weight; // Stores the final settled weight from final_measurement()

    // Flow detection confirmation variables
    bool flow_start_confirmed;
    // Dynamic pulse algorithm variables
    volatile float pulse_flow_rate;    // Thread-safe for Core 0 access
    
    // Loop counter variables for performance tracking
    volatile uint16_t current_phase_loop_count;  // Thread-safe for Core 0 access

    // Logging support
    uint8_t current_profile_id;
    GrindEvent event_in_progress; // Used to build data for the current phase event
    
    // State tracking for measurement calculations (eliminates calculations in logger)
    float last_logged_weight;       // Previous weight for delta calculation
    unsigned long last_logged_time; // Previous timestamp for relative timing
    bool force_measurement_log;     // Flag to force measurement logging on next update cycle

    // UI event system - thread-safe Core 0 → Core 1 communication
    QueueHandle_t ui_event_queue;
    
    // Flash operation queue - thread-safe Core 0 → Core 1 communication
    QueueHandle_t flash_op_queue;
    static const int FLASH_OP_QUEUE_SIZE = 5;
    
    // Log message queue - thread-safe Core 0 → Core 1 communication
    QueueHandle_t log_queue;
    static const int LOG_QUEUE_SIZE = 20;
    
    // Time mode pulse tracking
    int additional_pulse_count;
    uint32_t pulse_duration_ms;
    
    void (*ui_event_callback)(const GrindEventData&) = nullptr;
    bool ui_ready_for_setup = false; // Flag to track UI acknowledgment of INITIALIZING phase
    
    // Flag to prevent repeated flash operations for terminal phases (COMPLETED/TIMEOUT)
    bool session_end_flash_queued = false;
    char last_error_message[32];

    GrindSessionDescriptor session_descriptor;
    GrindStrategyContext strategy_context;
    IGrindStrategy* active_strategy = nullptr;
    WeightGrindStrategy weight_strategy;
    TimeGrindStrategy time_strategy;

public:
    void init(WeightSensor* lc, Grinder* gr, Preferences* prefs);
    void start_grind(float target_weight, uint32_t target_time_ms, GrindMode grind_mode);
    void user_tare_request();
    void return_to_idle(); // Called by UI to acknowledge completion/timeout
    void stop_grind();
    void update(); // Core 0 main control method - runs at fixed RTOS interval
    
    // Time mode pulse functionality
    void start_additional_pulse(); // Start an additional 100ms pulse in time mode
    bool can_pulse() const; // Check if additional pulses are allowed
    int get_additional_pulse_count() const { return additional_pulse_count; }
    
    // UI event system
    void set_ui_event_callback(void (*callback)(const GrindEventData&));
    void ui_acknowledge_phase_transition(); // Called by UI to confirm phase transition
    void process_queued_ui_events(); // Core 1: Process events from Core 0 queue
    QueueHandle_t get_ui_event_queue() const { return ui_event_queue; }
    
    // Flash operation system
    void process_queued_flash_operations(); // Core 1: Process flash ops from Core 0 queue
    void queue_flash_operation(const FlashOpRequest& request); // Core 0: Queue flash operation
    
    // Log message system
    void process_queued_log_messages(); // Core 1: Process log messages from Core 0 queue
    void queue_log_message(const char* format, ...); // Core 0: Queue formatted log message
    
    bool is_active() const;
    float get_target_weight() const { return target_weight; }
    uint32_t get_target_time_ms() const { return target_time_ms; }
    GrindMode get_mode() const { return mode; }
    const GrindSessionDescriptor& get_session_descriptor() const { return session_descriptor; }
    
    // Grind logging functions
    void set_grind_profile_id(uint8_t profile_id) { current_profile_id = profile_id; session_descriptor.profile_id = profile_id; }
    void send_measurements_data();           // Send structured measurement data via serial
    
    // Getter methods for logger (to eliminate calculations in logger)
    float get_current_flow_rate() const;
    float get_motor_stop_target_weight() const { return motor_stop_target_weight; }
    float get_grind_latency_ms() const { return grind_latency_ms; }
    float get_last_logged_weight() const { return last_logged_weight; }
    void set_last_logged_weight(float weight) { last_logged_weight = weight; } // Thread-safe setter
    
    // Removed - predictive logic now inline in update_realtime()
    
    
private:
    void switch_phase(GrindPhase new_phase, const GrindLoopData& loop_data = {});
    void final_measurement(const GrindLoopData& loop_data);

    bool check_timeout() const;
    uint8_t get_current_phase_id() const;
    
    // UI event emission - thread-safe for Core 0
    void emit_ui_event(const GrindEventData& data);
    
    // Core 0 control methods  
    bool should_log_measurements() const;
    
    // Internal state methods (moved from public to prevent polling)
    bool show_taring_text() const { return phase == GrindPhase::INITIALIZING || phase == GrindPhase::SETUP || phase == GrindPhase::TARING || phase == GrindPhase::TARE_CONFIRM; }
    bool is_completed() const { return phase == GrindPhase::COMPLETED; }
    bool is_timeout() const { return phase == GrindPhase::TIMEOUT; } 
    int get_progress_percent() const;
    float get_grind_time() const;
    GrindPhase get_phase() const { return phase; }
    GrindPhase get_timeout_phase() const { return timeout_phase; }
    const char* get_phase_name(GrindPhase p = static_cast<GrindPhase>(-1)) const;

    void set_error_message(const char* message);
};
