#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "../config/constants.h"
#include "../controllers/grind_session.h"

// Forward declarations
class WeightSensor;
class Grinder;

// Buffer settings (PSRAM staging area) - Dynamic calculation based on actual timing
#define MAX_EVENTS_PER_GRIND 50                             // Max discrete events per session (phases, pulses, etc.)

// Core 0 synchronized logging frequency matches the control loop interval
// Example: 30s * 50Hz = 1500 measurements when control interval is 20ms
// Memory impact scales with frequency; keep aligned with SYS_TASK_GRIND_CONTROL_INTERVAL_MS
#define CORE0_CONTROL_FREQUENCY_HZ (1000 / SYS_TASK_GRIND_CONTROL_INTERVAL_MS)
#define CALCULATE_MAX_MEASUREMENTS_PER_GRIND() \
    (GRIND_TIMEOUT_SEC * CORE0_CONTROL_FREQUENCY_HZ / SYS_LOG_EVERY_N_GRIND_LOOPS)

#define MAX_MEASUREMENTS_PER_GRIND CALCULATE_MAX_MEASUREMENTS_PER_GRIND()
#define EVENT_TEMP_BUFFER_SIZE MAX_EVENTS_PER_GRIND  
#define MEASUREMENT_TEMP_BUFFER_SIZE MAX_MEASUREMENTS_PER_GRIND

// Flash storage settings
#define GRIND_SESSIONS_DIR "/sessions"                      // Directory for individual session files
#define SESSION_FILE_FORMAT "/sessions/session_%lu.bin"    // Individual session file naming format
#define GRIND_LOG_FILE "/grind_sessions.bin"                // Legacy single-file storage (deprecated)
#define MAX_STORED_SESSIONS_FLASH 25                        // Maximum sessions to keep in flash (configurable)

#pragma pack(push, 1)

constexpr uint16_t GRIND_LOG_SCHEMA_VERSION = 2;

// Time-series session header for flash file
struct TimeSeriesSessionHeader {
    uint32_t session_id;           // Session identifier
    uint32_t session_timestamp;    // Unix timestamp when session started
    uint32_t session_size;         // Total size of session, events, and measurements in bytes
    uint32_t checksum;             // Checksum of all data
    uint16_t event_count;          // Number of discrete events in this session
    uint16_t measurement_count;    // Number of continuous measurements in this session
    uint16_t schema_version;       // Schema/version so Python tools can adapt
    uint16_t reserved;             // Reserved for future use (alignment + versioned flags)
};

enum GrindEventFlags : uint8_t {
    GRIND_EVENT_FLAG_TIME_MODE   = 1 << 0,  // Event recorded while grinding by time
    GRIND_EVENT_FLAG_MOTOR_ACTIVE = 1 << 1, // Phase kept the motor running
    GRIND_EVENT_FLAG_PULSE_PHASE = 1 << 2   // Phase represents a pulse or settling after a pulse
};

// Discrete, low-frequency events summarizing a phase.
struct GrindEvent {
    uint32_t timestamp_ms;            // Relative to session start
    uint32_t duration_ms;             // How long this phase/event lasted
    uint32_t grind_latency_ms;        // For PREDICTIVE phase
    uint32_t settling_duration_ms;    // For SETTLING phases
    float    start_weight;            // Weight when phase started
    float    end_weight;              // Weight when phase ended
    float    motor_stop_target_weight;// Value at the end of this phase
    float    pulse_duration_ms;       // For PULSE_EXECUTE phase
    float    pulse_flow_rate;         // Flow rate from predictive phase used for pulse calculations
    uint16_t event_sequence_id;       // Unique ID for the event within the session
    uint16_t loop_count;              // Loop iterations while in this phase
    uint8_t  phase_id;                // Numeric phase ID (see GrindPhase enum)
    uint8_t  pulse_attempt_number;    // Which pulse attempt (1-10), 0 if not a pulse
    uint8_t  event_flags;             // Additional event metadata flags
    uint8_t  reserved;                // Alignment + future use

    GrindEvent() {
        memset(this, 0, sizeof(GrindEvent));
    }
};

// Continuous, high-frequency time-series measurements.
struct GrindMeasurement {
    uint32_t timestamp_ms;            // Relative to session start
    float    weight_grams;            // Current weight reading
    float    weight_delta;            // Change since last measurement
    float    flow_rate_g_per_s;       // Continuously calculated flow rate
    float    motor_stop_target_weight;// Current motor stop target weight
    uint16_t sequence_id;             // Sequence for data integrity checks
    uint8_t  motor_is_on;             // Motor state at time of measurement
    uint8_t  phase_id;                // Current grinding phase ID

    GrindMeasurement() {
        memset(this, 0, sizeof(GrindMeasurement));
    }
};

enum class GrindTerminationReason : uint8_t {
    COMPLETED = 0,
    TIMEOUT = 1,
    OVERSHOOT = 2,
    MAX_PULSES = 3,
    UNKNOWN = 255
};

// Session metadata - configuration snapshot and results summary
struct GrindSession {
    // Primary
    uint32_t session_id;              // Auto-increment ID
    uint32_t session_timestamp;       // Unix timestamp (session start)
    uint32_t target_time_ms;          // Requested grind duration for time mode
    uint32_t total_time_ms;           // Session runtime (milliseconds)
    uint32_t total_motor_on_time_ms;  // Motor on time
    int32_t  time_error_ms;           // Signed difference between target and actual motor time

    // Profile & configuration
    float    target_weight;           // Target weight configured for session
    float    tolerance;               // Allowed error tolerance
    float    final_weight;            // Recorded final settled weight
    float    error_grams;             // target - final
    float    start_weight;            // Weight when logging started (pre-tare snapshot)

    // Grind Controller snapshot
    float    initial_motor_stop_offset;
    float    latency_to_coast_ratio;
    float    flow_rate_threshold;

    // Compact fields and status
    uint8_t  profile_id;              // Active profile index
    uint8_t  grind_mode;              // Grind mode (GrindMode enum value)
    uint8_t  max_pulse_attempts;      // Configured max pulse attempts
    uint8_t  pulse_count;             // Pulses executed
    uint8_t  termination_reason;      // See GrindTerminationReason
    uint8_t  reserved[3];             // Alignment + future expansion
    char     result_status[16];       // Null-terminated status string

    GrindSession() {
        memset(this, 0, sizeof(GrindSession));
    }
};

#pragma pack(pop)

static_assert(sizeof(TimeSeriesSessionHeader) == 24, "Unexpected TimeSeriesSessionHeader size");
static_assert(sizeof(GrindEvent) == 44, "Unexpected GrindEvent size");
static_assert(sizeof(GrindMeasurement) == 24, "Unexpected GrindMeasurement size");
static_assert(sizeof(GrindSession) == 80, "Unexpected GrindSession size");

// Time-series grind logging manager
class GrindLogger {
private:
    // Time-series system
    GrindSession* current_session;           // Current session metadata (PSRAM)
    GrindEvent* event_buffer;                // PSRAM temp buffer for events
    GrindMeasurement* measurement_buffer;    // PSRAM temp buffer for measurements
    uint16_t event_count;                    // Current number of events in buffer
    uint16_t measurement_count;              // Current number of measurements in buffer
    uint16_t event_sequence_counter;         // **NEW**: Counter for unique event IDs
    uint16_t measurement_sequence_counter;   // Sequence counter for continuous measurements
    
    char current_phase_name[16];             // Current grinding phase name
    bool logging_active;                     // Whether a grind session is active
    
    // Motor time tracking
    bool last_motor_state;                   // Previous motor state for change detection
    uint32_t motor_start_time;               // When motor last turned on
    uint32_t total_motor_time_ms;            // Accumulated motor on time for session
    
    // Current session data
    uint32_t session_start_time;
    
    // Session ID management
    Preferences* _preferences;
    uint32_t _next_session_id;
    
public:
    bool init(Preferences* prefs);           // Initialize PSRAM buffer
    void cleanup();                          // Free PSRAM buffer
    
    // Session management
    void start_grind_session(const GrindSessionDescriptor& descriptor, float start_weight);
    void end_grind_session(const char* final_result, float final_weight, uint8_t pulse_count);
    void discard_current_session();         // Discard current session without saving
    
    // Logging methods
    void log_event(GrindEvent& event);       // **MODIFIED**: Takes non-const reference to set sequence ID
    void log_continuous_measurement(uint32_t timestamp_ms, float weight_grams, float weight_delta, 
                                  float flow_rate_g_per_s, uint8_t motor_is_on, uint8_t phase_id, 
                                  float motor_stop_target_weight);
    
    // Flash storage management
    bool flush_session_to_flash();          // Flush current session to flash
    bool rotate_flash_log_if_needed();      // Remove old sessions if limit exceeded
    bool clear_all_sessions_from_flash();   // Purge all stored sessions (for developer purge)
    uint32_t count_sessions_in_flash() const; // Count total sessions in flash file
    uint32_t count_total_events_in_flash() const; // Count total events across all sessions
    uint32_t count_total_measurements_in_flash() const; // Count total measurements across all sessions
    
    // Fixed-length binary export method
    void export_sessions_binary_chunk(uint8_t* buffer, size_t buffer_size,
                                     uint32_t start_pos, uint32_t* next_pos, size_t* actual_size);
    void send_current_session_via_serial();  // Debug output for current session
    
    // Data access
    uint32_t get_total_flash_sessions() const;
    bool is_logging_active() const { return logging_active; }
    
    // Debug output helpers - conditionally compiled based on debug flags (moved to public for BLE access)
#if ENABLE_GRIND_DEBUG
    void print_session_data_table();           // Print sessions in tabular format for debugging
    void print_struct_layout_debug();          // Print struct sizes and offsets for Python parsing
    void print_comprehensive_debug();          // Print full struct contents and raw memory dumps
#else
    // Inline no-op versions when debugging is disabled
    inline void print_session_data_table() {}
    inline void print_struct_layout_debug() {}
    inline void print_comprehensive_debug() {}
#endif
    
private:
    // Time-series system helpers
    void clear_buffers();
    void initialize_session_config();       // Snapshot current config into session
    void reset_export_static_variables();   // Reset static variables used in export function
    
    // Flash storage helpers
    uint32_t calculate_checksum(const uint8_t* data, size_t length); // Simple checksum calculation
    bool write_time_series_session_to_flash(const GrindSession& session, const GrindEvent* events, const GrindMeasurement* measurements);
    bool remove_oldest_sessions(uint32_t sessions_to_remove); // Remove oldest sessions from flash file (legacy)
    
    // Individual session file management
    bool ensure_sessions_directory_exists();    // Create sessions directory if needed
    bool write_individual_session_file(uint32_t session_id, const GrindSession& session, const GrindEvent* events, const GrindMeasurement* measurements);
    bool validate_session_file(uint32_t session_id); // Check if session file is valid/readable
    bool remove_session_file(uint32_t session_id);   // Delete specific session file
    void cleanup_old_session_files(); // Remove old session files to maintain MAX_STORED_SESSIONS_FLASH limit
    
};

// Global logger instance
extern GrindLogger grind_logger;
