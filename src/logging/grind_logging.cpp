#include "grind_logging.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../config/constants.h"

namespace {

GrindTerminationReason classify_termination_reason(const char* final_result) {
    if (!final_result) {
        return GrindTerminationReason::UNKNOWN;
    }

    if (strcmp(final_result, "TIMEOUT") == 0) {
        return GrindTerminationReason::TIMEOUT;
    }
    if (strcmp(final_result, "OVERSHOOT") == 0) {
        return GrindTerminationReason::OVERSHOOT;
    }
    if (strcmp(final_result, "COMPLETE - MAX PULSES") == 0) {
        return GrindTerminationReason::MAX_PULSES;
    }
    if (strcmp(final_result, "COMPLETE") == 0) {
        return GrindTerminationReason::COMPLETED;
    }

    return GrindTerminationReason::UNKNOWN;
}

}

GrindLogger grind_logger;

bool GrindLogger::init(Preferences* prefs) {
    _preferences = prefs;
    current_session = (GrindSession*)heap_caps_malloc(sizeof(GrindSession), MALLOC_CAP_SPIRAM);
    if (!current_session) {
        LOG_BLE("ERROR: Failed to allocate PSRAM for grind session\n");
        return false;
    }
    
    event_buffer = (GrindEvent*)heap_caps_malloc(sizeof(GrindEvent) * EVENT_TEMP_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!event_buffer) {
        LOG_BLE("ERROR: Failed to allocate PSRAM for grind events\n");
        heap_caps_free(current_session);
        return false;
    }
    
    measurement_buffer = (GrindMeasurement*)heap_caps_malloc(sizeof(GrindMeasurement) * MEASUREMENT_TEMP_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!measurement_buffer) {
        LOG_BLE("ERROR: Failed to allocate PSRAM for grind measurements\n");
        heap_caps_free(current_session);
        heap_caps_free(event_buffer);
        return false;
    }
    
    event_count = 0;
    measurement_count = 0;
    event_sequence_counter = 0;
    logging_active = false;
    
    // Load the next session ID from preferences
    _next_session_id = _preferences->getUInt("next_session_id", 1);
    
    LOG_BLE("Time-series Logger initialized:\n");
    LOG_BLE("  - Event Buffer: %lu KB (%d events)\n", (unsigned long)((sizeof(GrindEvent) * EVENT_TEMP_BUFFER_SIZE) / 1024), (int)EVENT_TEMP_BUFFER_SIZE);
    LOG_BLE("  - Measurement Buffer: %lu KB (%d measurements)\n", (unsigned long)((sizeof(GrindMeasurement) * MEASUREMENT_TEMP_BUFFER_SIZE) / 1024), (int)MEASUREMENT_TEMP_BUFFER_SIZE);
    LOG_BLE("  - Next session ID: %lu\n", _next_session_id);
    
    return true;
}

void GrindLogger::cleanup() {
    if (current_session) heap_caps_free(current_session);
    if (event_buffer) heap_caps_free(event_buffer);
    if (measurement_buffer) heap_caps_free(measurement_buffer);
}

void GrindLogger::start_grind_session(const GrindSessionDescriptor& descriptor, float start_weight) {
    if (!current_session || !event_buffer || !measurement_buffer) {
        return;
    }

    clear_buffers();
    memset(current_session, 0, sizeof(GrindSession));

    current_session->session_id = _next_session_id;
    _next_session_id++;
    _preferences->putUInt("next_session_id", _next_session_id);

    current_session->session_timestamp = millis() / 1000;
    current_session->profile_id = descriptor.profile_id;
    current_session->target_weight = descriptor.target_weight;
    current_session->tolerance = descriptor.tolerance;
    current_session->grind_mode = static_cast<uint8_t>(descriptor.mode);
    current_session->target_time_ms = descriptor.target_time_ms;
    current_session->start_weight = start_weight;
    current_session->max_pulse_attempts = GRIND_MAX_PULSE_ATTEMPTS;
    current_session->time_error_ms = 0;
    current_session->total_time_ms = 0;
    current_session->total_motor_on_time_ms = 0;
    current_session->termination_reason = static_cast<uint8_t>(GrindTerminationReason::UNKNOWN);

    initialize_session_config();

    logging_active = true;
    session_start_time = millis();

    // Initialize motor time tracking
    last_motor_state = false;
    motor_start_time = 0;
    total_motor_time_ms = 0;

    const char* mode_name = (descriptor.mode == GrindMode::TIME) ? "TIME" : "WEIGHT";
    if (descriptor.mode == GrindMode::TIME) {
        LOG_BLE("Started time-series session %lu: mode=%s, target_time=%lums, profile=%d\n",
                current_session->session_id,
                mode_name,
                static_cast<unsigned long>(descriptor.target_time_ms),
                descriptor.profile_id);
    } else {
        LOG_BLE("Started time-series session %lu: mode=%s, target=%.1fg, profile=%d\n",
                current_session->session_id,
                mode_name,
                descriptor.target_weight,
                descriptor.profile_id);
    }
}


void GrindLogger::end_grind_session(const char* final_result, float final_weight, uint8_t pulse_count) {
    if (!current_session || !logging_active) {
        return;
    }

    current_session->final_weight = final_weight;
    current_session->error_grams = current_session->target_weight - final_weight;
    current_session->total_time_ms = millis() - session_start_time;
    current_session->pulse_count = pulse_count;
    strncpy(current_session->result_status, final_result, sizeof(current_session->result_status) - 1);

    // Finalize motor time tracking - if motor is still on, count the final period
    uint32_t now = millis();
    if (last_motor_state && motor_start_time > 0) {
        total_motor_time_ms += (now - motor_start_time);
    }
    current_session->total_motor_on_time_ms = total_motor_time_ms;

    GrindMode mode = static_cast<GrindMode>(current_session->grind_mode);
    if (mode == GrindMode::TIME) {
        current_session->time_error_ms = static_cast<int32_t>(current_session->total_motor_on_time_ms) -
                                         static_cast<int32_t>(current_session->target_time_ms);
        // Weight error is not meaningful for time-based grinds
        current_session->error_grams = 0.0f;
    } else {
        current_session->time_error_ms = 0;
    }

    GrindTerminationReason termination_reason = classify_termination_reason(final_result);
    current_session->termination_reason = static_cast<uint8_t>(termination_reason);

    // Don't save sessions that were ended abnormally (e.g., cancelled or timed out).
    bool is_abnormal_termination = (strcmp(final_result, "STOPPED_BY_USER") == 0) ||
                                   (termination_reason == GrindTerminationReason::TIMEOUT);

    // Check if logging is enabled before saving to flash
    Preferences logging_prefs;
    logging_prefs.begin("logging", true); // read-only
    bool logging_enabled = logging_prefs.getBool("enabled", false);
    logging_prefs.end();

    const char* mode_name = (mode == GrindMode::TIME) ? "TIME" : "WEIGHT";

    if (!is_abnormal_termination && logging_enabled) {
        flush_session_to_flash();
        if (mode == GrindMode::TIME) {
            LOG_BLE("Ended session %lu: mode=%s, final=%.1fg, time_error=%+ldms, %s (saved)\n",
                    current_session->session_id,
                    mode_name,
                    final_weight,
                    static_cast<long>(current_session->time_error_ms),
                    final_result);
        } else {
            LOG_BLE("Ended session %lu: mode=%s, final=%.1fg, error=%+.2fg, %s (saved)\n",
                    current_session->session_id,
                    mode_name,
                    final_weight,
                    current_session->error_grams,
                    final_result);
        }
    } else if (!is_abnormal_termination && !logging_enabled) {
        if (mode == GrindMode::TIME) {
            LOG_BLE("Ended session %lu: mode=%s, final=%.1fg, time_error=%+ldms, %s (not saved - logging disabled)\n",
                    current_session->session_id,
                    mode_name,
                    final_weight,
                    static_cast<long>(current_session->time_error_ms),
                    final_result);
        } else {
            LOG_BLE("Ended session %lu: mode=%s, final=%.1fg, error=%+.2fg, %s (not saved - logging disabled)\n",
                    current_session->session_id,
                    mode_name,
                    final_weight,
                    current_session->error_grams,
                    final_result);
        }
    } else {
        if (mode == GrindMode::TIME) {
            LOG_BLE("Ended session %lu: mode=%s, final=%.1fg, time_error=%+ldms, %s (not saved - abnormal termination)\n",
                    current_session->session_id,
                    mode_name,
                    final_weight,
                    static_cast<long>(current_session->time_error_ms),
                    final_result);
        } else {
            LOG_BLE("Ended session %lu: mode=%s, final=%.1fg, error=%+.2fg, %s (not saved - abnormal termination)\n",
                    current_session->session_id,
                    mode_name,
                    final_weight,
                    current_session->error_grams,
                    final_result);
        }
    }

    // Clear buffers to ensure clean state for next session
    clear_buffers();

    logging_active = false;
}

void GrindLogger::discard_current_session() {
    if (!current_session || !logging_active) return;
    
    LOG_BLE("Discarded session %lu: target=%.1fg (not saved - cancelled)\n",
                  current_session->session_id, current_session->target_weight);
    
    // Clear buffers to ensure clean state for next session
    clear_buffers();
    
    logging_active = false;
}

void GrindLogger::log_event(GrindEvent& event) {
    if (!logging_active || event_count >= EVENT_TEMP_BUFFER_SIZE) {
        return;
    }
    // **FIX**: Assign a unique, sequential ID to the event before logging
    if (current_session) {
        GrindMode mode = static_cast<GrindMode>(current_session->grind_mode);
        if (mode == GrindMode::TIME) {
            event.event_flags |= GRIND_EVENT_FLAG_TIME_MODE;
        }
    }
    event.event_sequence_id = event_sequence_counter++;
    event_buffer[event_count++] = event;
}

void GrindLogger::log_continuous_measurement(uint32_t timestamp_ms, float weight_grams, float weight_delta, 
                                            float flow_rate_g_per_s, uint8_t motor_is_on, uint8_t phase_id, 
                                            float motor_stop_target_weight) {
    if (!logging_active || measurement_count >= MEASUREMENT_TEMP_BUFFER_SIZE) {
        return;
    }
    
    // Pure data recording - no calculations (all values pre-calculated by GrindController)
    GrindMeasurement measurement;
    measurement.timestamp_ms = timestamp_ms;
    measurement.weight_grams = weight_grams;
    measurement.weight_delta = weight_delta;
    measurement.flow_rate_g_per_s = flow_rate_g_per_s;
    measurement.motor_stop_target_weight = motor_stop_target_weight;
    measurement.sequence_id = measurement_sequence_counter++;
    measurement.motor_is_on = motor_is_on;
    measurement.phase_id = phase_id;
    
    // Track motor time changes for session summary
    bool current_motor_state = (motor_is_on == 1);
    if (current_motor_state && !last_motor_state) {
        // Motor just turned ON
        motor_start_time = millis();
    } else if (!current_motor_state && last_motor_state && motor_start_time > 0) {
        // Motor just turned OFF, accumulate the time
        total_motor_time_ms += (millis() - motor_start_time);
    }
    last_motor_state = current_motor_state;
    
    measurement_buffer[measurement_count++] = measurement;
}

bool GrindLogger::flush_session_to_flash() {
    if (!current_session || !event_buffer || !measurement_buffer) {
        return false;
    }
    
    // Use new individual session file approach
    if (!ensure_sessions_directory_exists()) {
        LOG_BLE("ERROR: Failed to create sessions directory\n");
        return false;
    }
    
    // Write individual session file
    bool success = write_individual_session_file(current_session->session_id, *current_session, event_buffer, measurement_buffer);
    
    if (success) {
        // Clean up old session files to maintain the limit
        cleanup_old_session_files();
        
        LOG_BLE("Session %lu flushed to individual file\n", current_session->session_id);
    } else {
        LOG_BLE("ERROR: Failed to flush session %lu to file\n", current_session->session_id);
    }
    
    return success;
}

uint32_t GrindLogger::count_sessions_in_flash() const {
    uint32_t count = 0;
    
    // Count individual session files (new approach)
    if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
        File dir = LittleFS.open(GRIND_SESSIONS_DIR);
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            
            while (file) {
                String filename = file.name();
                // Handle both base names and full paths
                bool is_session_file = (filename.startsWith("session_") || filename.indexOf("/session_") != -1)
                                       && filename.endsWith(".bin");
                if (is_session_file) {
                    count++;
                }
                file = dir.openNextFile();
            }
            
            dir.close();
        }
    }
    
    return count;
}

uint32_t GrindLogger::count_total_events_in_flash() const {
    uint32_t total_events = 0;
    
    // Count events from individual session files
    if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
        File dir = LittleFS.open(GRIND_SESSIONS_DIR);
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            
            while (file) {
                String filename = file.name();
                bool is_session_file = (filename.startsWith("session_") || filename.indexOf("/session_") != -1)
                                       && filename.endsWith(".bin");
                if (is_session_file) {
                    String full_path = filename.startsWith("/") ? filename : (String(GRIND_SESSIONS_DIR) + "/" + filename);
                    File sessionFile = LittleFS.open(full_path.c_str(), "r");
                    if (sessionFile) {
                        TimeSeriesSessionHeader header;
                        if (sessionFile.read((uint8_t*)&header, sizeof(header)) == sizeof(header)) {
                            total_events += header.event_count;
                        }
                        sessionFile.close();
                    }
                }
                file = dir.openNextFile();
            }
            
            dir.close();
        }
    }
    
    return total_events;
}

uint32_t GrindLogger::count_total_measurements_in_flash() const {
    uint32_t total_measurements = 0;
    
    // Count measurements from individual session files
    if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
        File dir = LittleFS.open(GRIND_SESSIONS_DIR);
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            
            while (file) {
                String filename = file.name();
                bool is_session_file = (filename.startsWith("session_") || filename.indexOf("/session_") != -1)
                                       && filename.endsWith(".bin");
                if (is_session_file) {
                    String full_path = filename.startsWith("/") ? filename : (String(GRIND_SESSIONS_DIR) + "/" + filename);
                    File sessionFile = LittleFS.open(full_path.c_str(), "r");
                    if (sessionFile) {
                        TimeSeriesSessionHeader header;
                        if (sessionFile.read((uint8_t*)&header, sizeof(header)) == sizeof(header)) {
                            total_measurements += header.measurement_count;
                        }
                        sessionFile.close();
                    }
                }
                file = dir.openNextFile();
            }
            
            dir.close();
        }
    }
    
    return total_measurements;
}

void GrindLogger::send_current_session_via_serial() {
    if (!current_session || !logging_active) {
        LOG_BLE("No active session to display\n");
        return;
    }
    
    LOG_BLE("\n=== Current Grind Session %lu ===\n", current_session->session_id);
    LOG_BLE("Target: %.1fg, Profile: %d\n", current_session->target_weight, current_session->profile_id);
    LOG_BLE("Events: %u/%d, Measurements: %u/%d\n", event_count, (int)EVENT_TEMP_BUFFER_SIZE, measurement_count, (int)MEASUREMENT_TEMP_BUFFER_SIZE);
    LOG_BLE("=====================================\n");
}

uint32_t GrindLogger::get_total_flash_sessions() const {
    return count_sessions_in_flash();
}

void GrindLogger::clear_buffers() {
    event_count = 0;
    measurement_count = 0;
    event_sequence_counter = 0;
    measurement_sequence_counter = 0;
    if (event_buffer) memset(event_buffer, 0, sizeof(GrindEvent) * EVENT_TEMP_BUFFER_SIZE);
    if (measurement_buffer) memset(measurement_buffer, 0, sizeof(GrindMeasurement) * MEASUREMENT_TEMP_BUFFER_SIZE);
}

void GrindLogger::initialize_session_config() {
    if (!current_session) return;
    current_session->initial_motor_stop_offset = GRIND_UNDERSHOOT_TARGET_G;
    current_session->max_pulse_attempts = GRIND_MAX_PULSE_ATTEMPTS;
    current_session->latency_to_coast_ratio = GRIND_LATENCY_TO_COAST_RATIO;
    current_session->flow_rate_threshold = GRIND_FLOW_DETECTION_THRESHOLD_GPS;
    current_session->pulse_duration_large = HW_PULSE_LARGE_ERROR_MS;
    current_session->pulse_duration_medium = HW_PULSE_MEDIUM_ERROR_MS;
    current_session->pulse_duration_small = HW_PULSE_SMALL_ERROR_MS;
    current_session->pulse_duration_fine = HW_PULSE_FINE_ERROR_MS;
    current_session->large_error_threshold = SYS_GRIND_ERROR_LARGE_THRESHOLD_G;
    current_session->medium_error_threshold = SYS_GRIND_ERROR_MEDIUM_THRESHOLD_G;
    current_session->small_error_threshold = SYS_GRIND_ERROR_SMALL_THRESHOLD_G;
}


bool GrindLogger::write_time_series_session_to_flash(const GrindSession& session, const GrindEvent* events, const GrindMeasurement* measurements) {
    File file = LittleFS.open(GRIND_LOG_FILE, "a");
    if (!file) {
        LOG_BLE("Failed to open log file for writing\n");
        return false;
    }
    
    TimeSeriesSessionHeader header;
    header.session_id = session.session_id;
    header.session_timestamp = session.session_timestamp;
    header.session_size = sizeof(GrindSession) + (sizeof(GrindEvent) * event_count) + (sizeof(GrindMeasurement) * measurement_count);
    header.checksum = calculate_checksum((const uint8_t*)&session, header.session_size);
    header.event_count = event_count;
    header.measurement_count = measurement_count;
    header.schema_version = GRIND_LOG_SCHEMA_VERSION;
    header.reserved = 0;

    size_t written = 0;
    written += file.write((uint8_t*)&header, sizeof(TimeSeriesSessionHeader));
    written += file.write((uint8_t*)&session, sizeof(GrindSession));
    written += file.write((uint8_t*)events, sizeof(GrindEvent) * event_count);
    written += file.write((uint8_t*)measurements, sizeof(GrindMeasurement) * measurement_count);
    
    file.close();
    
    size_t expected = sizeof(TimeSeriesSessionHeader) + header.session_size;
    if (written == expected) {
        LOG_BLE("Wrote session %lu (%d events, %d measurements) to flash (%d bytes)\n", 
                     session.session_id, event_count, measurement_count, written);
        
        return true;
    } else {
        LOG_BLE("Flash write error: wrote %d/%d bytes\n", written, expected);
        return false;
    }
}



static void write_uint32_le(uint8_t*& buffer, uint32_t value) {
    *buffer++ = value & 0xFF;
    *buffer++ = (value >> 8) & 0xFF;
    *buffer++ = (value >> 16) & 0xFF;
    *buffer++ = (value >> 24) & 0xFF;
}

static void write_uint16_le(uint8_t*& buffer, uint16_t value) {
    *buffer++ = value & 0xFF;
    *buffer++ = (value >> 8) & 0xFF;
}


void GrindLogger::export_sessions_binary_chunk(uint8_t* buffer, size_t buffer_size,  
                                               uint32_t start_pos, uint32_t* next_pos, size_t* actual_size) {
    static File export_file;
    static uint32_t total_sessions = 0;
    static uint32_t session_idx = 0;
    static uint32_t current_session_id = 0;
    static uint32_t* session_list = nullptr;
    static bool initialized = false;
    static uint32_t event_idx = 0;
    static uint32_t measurement_idx = 0;
    static TimeSeriesSessionHeader current_header;
    static GrindSession current_session_data;
    static bool session_header_sent = false;

    // Handle explicit cleanup call (buffer == nullptr or buffer_size == 0)
    if (buffer == nullptr || buffer_size == 0) {
        if (export_file) {
            export_file.close();
            LOG_DEBUG_PRINTLN("Forced closure of export file handle");
        }
        if (session_list) {
            heap_caps_free(session_list);
            session_list = nullptr;
        }
        initialized = false;
        *actual_size = 0;
        *next_pos = 0;
        return;
    }

    // Initialize session list on first call
    if (start_pos == 0 || !initialized) {
        if (export_file) export_file.close();
        if (session_list) {
            heap_caps_free(session_list);
            session_list = nullptr;
        }
        
        total_sessions = count_sessions_in_flash();
        session_idx = 0;
        current_session_id = 0;
        event_idx = 0;
        measurement_idx = 0;
        session_header_sent = false;
        initialized = false;
        
        if (total_sessions > 0) {
            // Create list of available session IDs
            session_list = (uint32_t*)heap_caps_malloc(total_sessions * sizeof(uint32_t), MALLOC_CAP_8BIT);
            if (!session_list) {
                LOG_BLE("ERROR: Failed to allocate session list memory\n");
                *actual_size = 0;
                *next_pos = 0;
                return;
            }
            
            // Collect valid session IDs from directory
            uint32_t list_count = 0;
            
            // Try individual session files first (new approach)
            if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
                File dir = LittleFS.open(GRIND_SESSIONS_DIR);
                if (dir && dir.isDirectory()) {
                    File file = dir.openNextFile();
                    while (file && list_count < total_sessions) {
                        String filename = file.name();
                        if (filename.startsWith("session_") && filename.endsWith(".bin")) {
                            // Extract session ID from filename
                            int start_pos = filename.indexOf('_') + 1;
                            int end_pos = filename.lastIndexOf('.');
                            if (start_pos > 0 && end_pos > start_pos) {
                                uint32_t session_id = filename.substring(start_pos, end_pos).toInt();
                                if (session_id > 0 && validate_session_file(session_id)) {
                                    session_list[list_count++] = session_id;
                                }
                            }
                        }
                        file = dir.openNextFile();
                    }
                    dir.close();
                }
            }
            
            // Sort session IDs for consistent order
            for (uint32_t i = 0; i < list_count - 1; i++) {
                for (uint32_t j = i + 1; j < list_count; j++) {
                    if (session_list[i] > session_list[j]) {
                        uint32_t temp = session_list[i];
                        session_list[i] = session_list[j];
                        session_list[j] = temp;
                    }
                }
            }
            
            total_sessions = list_count;
            LOG_BLE("Export: Found %lu valid session files\n", total_sessions);
        }
        
        initialized = true;
        print_session_data_table();
        LOG_GRIND_DEBUG("Starting export for Python data analysis\n");
        print_struct_layout_debug();  // Print struct layout at start of export
    }

    if (session_idx >= total_sessions) {
        if(export_file) export_file.close();
        *actual_size = 0;
        *next_pos = 0; // Signal completion
        return;
    }
    
    // Open current session file if not already open
    if (!export_file && session_idx < total_sessions) {
        current_session_id = session_list[session_idx];
        char filename[64];
        snprintf(filename, sizeof(filename), SESSION_FILE_FORMAT, current_session_id);
        export_file = LittleFS.open(filename, "r");
        if (!export_file) {
            LOG_BLE("ERROR: Failed to open session file: %s\n", filename);
            session_idx++; // Skip this session
            *actual_size = 0;
            *next_pos = start_pos + 1;
            return;
        }
        LOG_BLE("Export: Opened session file %lu\n", current_session_id);
    }
    
    uint8_t* p = buffer;
    size_t remaining_size = buffer_size;
    
    if (start_pos == 0) {
        write_uint32_le(p, total_sessions);
        remaining_size -= 4;
    }
    
    while (remaining_size > 0 && session_idx < total_sessions) {
        if (!session_header_sent) {
            if (export_file.read((uint8_t*)&current_header, sizeof(current_header)) != sizeof(current_header) ||
                export_file.read((uint8_t*)&current_session_data, sizeof(current_session_data)) != sizeof(current_session_data)) {
                break; // Error reading file
            }
            event_idx = 0;
            measurement_idx = 0;

            // Send full GrindSession struct + event/measurement counts from header
            const size_t session_data_size = sizeof(GrindSession) + 4; // +4 for event/measurement counts
            if (remaining_size < session_data_size) break;

            // First send the full GrindSession struct
            memcpy(p, &current_session_data, sizeof(GrindSession));
            p += sizeof(GrindSession);
            
            // Then append event_count and measurement_count from header
            write_uint16_le(p, current_header.event_count);
            write_uint16_le(p, current_header.measurement_count);
            
            remaining_size -= session_data_size;
            session_header_sent = true;
        }

        // Write events
        while(event_idx < current_header.event_count && remaining_size >= sizeof(GrindEvent)) {
            export_file.read(p, sizeof(GrindEvent));
            p += sizeof(GrindEvent);
            remaining_size -= sizeof(GrindEvent);
            event_idx++;
        }

        // Write measurements
        while(measurement_idx < current_header.measurement_count && remaining_size >= sizeof(GrindMeasurement)) {
            export_file.read(p, sizeof(GrindMeasurement));
            p += sizeof(GrindMeasurement);
            remaining_size -= sizeof(GrindMeasurement);
            measurement_idx++;
        }

        // If session is fully sent, move to the next one
        if (event_idx >= current_header.event_count && measurement_idx >= current_header.measurement_count) {
            session_idx++;
            session_header_sent = false;
            event_idx = 0;
            measurement_idx = 0;
            // Close current session file
            if (export_file) {
                export_file.close();
                LOG_BLE("Export: Completed session %lu\n", current_session_id);
            }
        } else {
            // Buffer is full, break to send chunk
            break;
        }
    }

    *actual_size = p - buffer;
    *next_pos = (session_idx >= total_sessions) ? 0 : start_pos + 1;
}


#if ENABLE_GRIND_DEBUG
void GrindLogger::print_session_data_table() {
    // Check individual session files first (new approach)
    uint32_t session_count = count_sessions_in_flash();
    if (session_count == 0) {
        LOG_BLE("No session data to display\n");
        return;
    }
    
    LOG_BLE("\n=== SESSION DATA TABLE ===\n");
    LOG_BLE("ID | Target  | Final   | Error  | Time | Events | Measurements\n");
    LOG_BLE("---|---------|---------|--------|------|--------|--------------\n");
    
    // Display data from individual session files
    if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
        File dir = LittleFS.open(GRIND_SESSIONS_DIR);
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            int displayed = 0;
            const int MAX_DISPLAY = 10; // Limit display for readability
            
            while (file && displayed < MAX_DISPLAY) {
                String filename = file.name();
                if (filename.startsWith("session_") && filename.endsWith(".bin")) {
                    // Check if filename has proper format
                    int start_pos = filename.indexOf('_') + 1;
                    int end_pos = filename.lastIndexOf('.');
                    if (start_pos > 0 && end_pos > start_pos) {
                        
                        // Read session file
                        String full_path = String(GRIND_SESSIONS_DIR) + "/" + filename;
                        File sessionFile = LittleFS.open(full_path.c_str(), "r");
                        if (sessionFile) {
                            TimeSeriesSessionHeader header;
                            GrindSession session_data;
                            
                            if (sessionFile.read((uint8_t*)&header, sizeof(header)) == sizeof(header) &&
                                sessionFile.read((uint8_t*)&session_data, sizeof(session_data)) == sizeof(session_data)) {
                                
                                LOG_BLE("%2lu | %6.1fg | %6.1fg | %5.1fg | %4lus | %6u | %12u\n",
                                    session_data.session_id,
                                    session_data.target_weight,
                                    session_data.final_weight,
                                    session_data.error_grams,
                                    session_data.total_time_ms / 1000,
                                    header.event_count,
                                    header.measurement_count);
                                displayed++;
                            }
                            sessionFile.close();
                        }
                    }
                }
                file = dir.openNextFile();
            }
            dir.close();
            
            if (displayed >= MAX_DISPLAY && session_count > MAX_DISPLAY) {
                LOG_BLE("... (showing %d of %lu sessions)\n", MAX_DISPLAY, session_count);
            }
            return;
        }
    }
    
    LOG_BLE("No individual session files found\n");
}
#endif // ENABLE_GRIND_DEBUG

// Dummy implementations for functions not part of this refactor
bool GrindLogger::rotate_flash_log_if_needed() { return true; }
bool GrindLogger::clear_all_sessions_from_flash() {
    LOG_BLE("Attempting to purge grind history from directory: %s\n", GRIND_SESSIONS_DIR);
 
    File dir = LittleFS.open(GRIND_SESSIONS_DIR);
    if (!dir) {
        LOG_BLE("Directory does not exist. Nothing to clear.");
        return true; // Not an error, just nothing to do.
    }
 
    if (!dir.isDirectory()) {
        LOG_BLE("Error: %s is not a directory.\n", GRIND_SESSIONS_DIR);
        dir.close();
        return false;
    }
 
    bool overall_result = true;
    int files_removed = 0;
    int files_failed = 0;
 
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            LOG_BLE("Skipping subdirectory: %s\n", file.path());
            file = dir.openNextFile();
        } else {
            String filePath = file.path();
            LOG_BLE(" - Deleting file: %s\n", filePath.c_str());
            
            // Close the file handle before attempting deletion
            file.close();
            
            if (LittleFS.remove(filePath)) {
                files_removed++;
            } else {
                LOG_DEBUG_PRINTLN("   -> FAILED");
                files_failed++;
                overall_result = false;
            }
            
            // Move to the next file
            file = dir.openNextFile();
        }
    }
 
    dir.close();
 
    LOG_DEBUG_PRINTF("Purge complete. Removed: %d, Failed: %d.\n", files_removed, files_failed);
 
    if (overall_result) {
        LOG_DEBUG_PRINTLN("Grind history purge completed successfully.");
    } else {
        LOG_DEBUG_PRINTLN("WARNING: Grind history purge completed with some errors.");
    }
 
    return overall_result;
}

bool GrindLogger::remove_oldest_sessions(uint32_t sessions_to_remove) { return true; }
uint32_t GrindLogger::calculate_checksum(const uint8_t* data, size_t length) { return 0; }

void GrindLogger::reset_export_static_variables() {
    // This function forces reset of static variables in export_sessions_binary_chunk
    // by calling it with dummy parameters to trigger the reset logic
    uint8_t dummy_buffer[1];
    uint32_t dummy_next_pos;
    size_t dummy_size;
    export_sessions_binary_chunk(dummy_buffer, 0, 0, &dummy_next_pos, &dummy_size);
    
    // Also explicitly force close any static file handles by calling with 0 buffer size
    // This ensures the file is closed in the static variable cleanup
    export_sessions_binary_chunk(nullptr, 0, 0, &dummy_next_pos, &dummy_size);
}

#if ENABLE_GRIND_DEBUG
void GrindLogger::print_struct_layout_debug() {
    LOG_GRIND_DEBUG("\n=== GRIND LOGGER STRUCT LAYOUT DEBUG ===\n");
    
    // Struct sizes
    LOG_BLE("TimeSeriesSessionHeader size: %u bytes\n", sizeof(TimeSeriesSessionHeader));
    LOG_BLE("GrindSession size: %u bytes\n", sizeof(GrindSession));
    LOG_BLE("GrindEvent size: %u bytes\n", sizeof(GrindEvent));
    LOG_BLE("GrindMeasurement size: %u bytes\n", sizeof(GrindMeasurement));
    
    // Yield to allow BLE transmission
    vTaskDelay(pdMS_TO_TICKS(10));
    
    LOG_BLE("\n--- TimeSeriesSessionHeader offsets ---\n");
    TimeSeriesSessionHeader* hdr = nullptr;
    LOG_BLE("session_id offset: %zu\n", (size_t)&hdr->session_id);
    LOG_BLE("session_timestamp offset: %zu\n", (size_t)&hdr->session_timestamp);
    LOG_BLE("session_size offset: %zu\n", (size_t)&hdr->session_size);
    LOG_BLE("checksum offset: %zu\n", (size_t)&hdr->checksum);
    LOG_BLE("event_count offset: %zu\n", (size_t)&hdr->event_count);
    LOG_BLE("measurement_count offset: %zu\n", (size_t)&hdr->measurement_count);
    LOG_BLE("schema_version offset: %zu\n", (size_t)&hdr->schema_version);
    LOG_BLE("reserved offset: %zu\n", (size_t)&hdr->reserved);
    
    // Yield to allow BLE transmission
    vTaskDelay(pdMS_TO_TICKS(10));
    
    LOG_BLE("\n--- GrindSession offsets ---\n");
    GrindSession* sess = nullptr;
    LOG_BLE("session_id offset: %zu\n", (size_t)&sess->session_id);
    LOG_BLE("session_timestamp offset: %zu\n", (size_t)&sess->session_timestamp);
    LOG_BLE("target_time_ms offset: %zu\n", (size_t)&sess->target_time_ms);
    LOG_BLE("total_time_ms offset: %zu\n", (size_t)&sess->total_time_ms);
    LOG_BLE("total_motor_on_time_ms offset: %zu\n", (size_t)&sess->total_motor_on_time_ms);
    LOG_BLE("time_error_ms offset: %zu\n", (size_t)&sess->time_error_ms);
    LOG_BLE("target_weight offset: %zu\n", (size_t)&sess->target_weight);
    LOG_BLE("tolerance offset: %zu\n", (size_t)&sess->tolerance);
    LOG_BLE("final_weight offset: %zu\n", (size_t)&sess->final_weight);
    LOG_BLE("error_grams offset: %zu\n", (size_t)&sess->error_grams);
    LOG_BLE("start_weight offset: %zu\n", (size_t)&sess->start_weight);
    LOG_BLE("initial_motor_stop_offset offset: %zu\n", (size_t)&sess->initial_motor_stop_offset);
    LOG_BLE("latency_to_coast_ratio offset: %zu\n", (size_t)&sess->latency_to_coast_ratio);
    LOG_BLE("flow_rate_threshold offset: %zu\n", (size_t)&sess->flow_rate_threshold);
    LOG_BLE("pulse_duration_large offset: %zu\n", (size_t)&sess->pulse_duration_large);
    LOG_BLE("pulse_duration_medium offset: %zu\n", (size_t)&sess->pulse_duration_medium);
    LOG_BLE("pulse_duration_small offset: %zu\n", (size_t)&sess->pulse_duration_small);
    LOG_BLE("pulse_duration_fine offset: %zu\n", (size_t)&sess->pulse_duration_fine);
    LOG_BLE("large_error_threshold offset: %zu\n", (size_t)&sess->large_error_threshold);
    LOG_BLE("medium_error_threshold offset: %zu\n", (size_t)&sess->medium_error_threshold);
    LOG_BLE("small_error_threshold offset: %zu\n", (size_t)&sess->small_error_threshold);
    LOG_BLE("profile_id offset: %zu\n", (size_t)&sess->profile_id);
    LOG_BLE("grind_mode offset: %zu\n", (size_t)&sess->grind_mode);
    LOG_BLE("max_pulse_attempts offset: %zu\n", (size_t)&sess->max_pulse_attempts);
    LOG_BLE("pulse_count offset: %zu\n", (size_t)&sess->pulse_count);
    LOG_BLE("termination_reason offset: %zu\n", (size_t)&sess->termination_reason);
    LOG_BLE("result_status offset: %zu\n", (size_t)&sess->result_status);
    
    // Yield to allow BLE transmission
    vTaskDelay(pdMS_TO_TICKS(10));
    
    LOG_BLE("\n--- GrindEvent offsets ---\n");
    GrindEvent* evt = nullptr;
    LOG_BLE("timestamp_ms offset: %zu\n", (size_t)&evt->timestamp_ms);
    LOG_BLE("duration_ms offset: %zu\n", (size_t)&evt->duration_ms);
    LOG_BLE("grind_latency_ms offset: %zu\n", (size_t)&evt->grind_latency_ms);
    LOG_BLE("settling_duration_ms offset: %zu\n", (size_t)&evt->settling_duration_ms);
    LOG_BLE("start_weight offset: %zu\n", (size_t)&evt->start_weight);
    LOG_BLE("end_weight offset: %zu\n", (size_t)&evt->end_weight);
    LOG_BLE("motor_stop_target_weight offset: %zu\n", (size_t)&evt->motor_stop_target_weight);
    LOG_BLE("pulse_duration_ms offset: %zu\n", (size_t)&evt->pulse_duration_ms);
    LOG_BLE("pulse_flow_rate offset: %zu\n", (size_t)&evt->pulse_flow_rate);
    LOG_BLE("event_sequence_id offset: %zu\n", (size_t)&evt->event_sequence_id);
    LOG_BLE("loop_count offset: %zu\n", (size_t)&evt->loop_count);
    LOG_BLE("phase_id offset: %zu\n", (size_t)&evt->phase_id);
    LOG_BLE("pulse_attempt_number offset: %zu\n", (size_t)&evt->pulse_attempt_number);
    LOG_BLE("event_flags offset: %zu\n", (size_t)&evt->event_flags);
    
    // Yield to allow BLE transmission
    vTaskDelay(pdMS_TO_TICKS(10));
    
    LOG_BLE("\n--- GrindMeasurement offsets ---\n");  
    GrindMeasurement* meas = nullptr;
    LOG_BLE("timestamp_ms offset: %zu\n", (size_t)&meas->timestamp_ms);
    LOG_BLE("weight_grams offset: %zu\n", (size_t)&meas->weight_grams);
    LOG_BLE("weight_delta offset: %zu\n", (size_t)&meas->weight_delta);
    LOG_BLE("flow_rate_g_per_s offset: %zu\n", (size_t)&meas->flow_rate_g_per_s);
    LOG_BLE("motor_stop_target_weight offset: %zu\n", (size_t)&meas->motor_stop_target_weight);
    LOG_BLE("sequence_id offset: %zu\n", (size_t)&meas->sequence_id);
    LOG_BLE("motor_is_on offset: %zu\n", (size_t)&meas->motor_is_on);
    LOG_BLE("phase_id offset: %zu\n", (size_t)&meas->phase_id);
    
    // Yield to allow BLE transmission
    vTaskDelay(pdMS_TO_TICKS(10));
    
    LOG_BLE("\n=== END STRUCT LAYOUT DEBUG ===\n");
}

void GrindLogger::print_comprehensive_debug() {
    LOG_BLE("\n=== COMPREHENSIVE DEBUG: ACTUAL FLASH DATA & MEMORY LAYOUTS ===\n");
    
    // First, create sample structs and dump their raw memory to verify our debug info
    LOG_BLE("--- VERIFYING STRUCT DEBUG INFO WITH ACTUAL MEMORY ---\n");
    
    // Create a sample TimeSeriesSessionHeader
    TimeSeriesSessionHeader test_header;
    memset(&test_header, 0, sizeof(test_header));
    test_header.session_id = 0x12345678;
    test_header.event_count = 0xABCD;
    test_header.measurement_count = 0xEF01;
    test_header.session_timestamp = 0x87654321;
    test_header.session_size = 0x11223344;
    test_header.checksum = 0x55667788;
    
    LOG_BLE("Sample TimeSeriesSessionHeader (%d bytes):\n", sizeof(test_header));
    LOG_BLE("  Expected session_id at offset 0: 0x%08lX\n", test_header.session_id);
    LOG_BLE("  Expected event_count at offset 4: 0x%04X\n", test_header.event_count);
    LOG_BLE("  Expected measurement_count at offset 6: 0x%04X\n", test_header.measurement_count);
    LOG_BLE("  Raw memory dump:\n    ");
    uint8_t* hdr_bytes = (uint8_t*)&test_header;
    for (int i = 0; i < sizeof(test_header); i++) {
        LOG_BLE("%02X ", hdr_bytes[i]);
        if ((i + 1) % 8 == 0) LOG_BLE("\n    ");
    }
    LOG_BLE("\n");
    
    // Create a sample GrindEvent
    GrindEvent test_event;
    memset(&test_event, 0, sizeof(test_event));
    test_event.timestamp_ms = 0x12345678;
    test_event.phase_id = 0xAB;
    test_event.pulse_attempt_number = 0xCD;
    test_event.event_sequence_id = 0xEF01;
    test_event.duration_ms = 0x23456789;
    test_event.start_weight = 12.34f;
    test_event.end_weight = 56.78f;
    
    LOG_BLE("Sample GrindEvent (%d bytes):\n", sizeof(test_event));
    LOG_BLE("  Expected timestamp_ms at offset 0: 0x%08lX\n", test_event.timestamp_ms);
    LOG_BLE("  Expected phase_id at offset 4: 0x%02X\n", test_event.phase_id);
    LOG_BLE("  Expected pulse_attempt_number at offset 5: 0x%02X\n", test_event.pulse_attempt_number);
    LOG_BLE("  Expected event_sequence_id at offset 6: 0x%04X\n", test_event.event_sequence_id);
    LOG_BLE("  Expected duration_ms at offset 8: 0x%08lX\n", test_event.duration_ms);
    LOG_BLE("  Raw memory dump:\n    ");
    uint8_t* evt_bytes = (uint8_t*)&test_event;
    for (int i = 0; i < sizeof(test_event); i++) {
        LOG_BLE("%02X ", evt_bytes[i]);
        if ((i + 1) % 16 == 0) LOG_BLE("\n    ");
    }
    LOG_BLE("\n");
    
    // Create a sample GrindMeasurement
    GrindMeasurement test_meas;
    memset(&test_meas, 0, sizeof(test_meas));
    test_meas.timestamp_ms = 0x87654321;
    test_meas.weight_grams = 23.45f;
    test_meas.weight_delta = 0.67f;
    test_meas.flow_rate_g_per_s = 1.23f;
    test_meas.motor_is_on = 0xAA;
    test_meas.phase_id = 0xBB;
    
    LOG_BLE("Sample GrindMeasurement (%d bytes):\n", sizeof(test_meas));
    LOG_BLE("  Expected timestamp_ms at offset 0: 0x%08lX\n", test_meas.timestamp_ms);
    LOG_BLE("  Expected weight_grams at offset 4: %.3f\n", test_meas.weight_grams);
    LOG_BLE("  Expected motor_is_on at offset 16: 0x%02X\n", test_meas.motor_is_on);
    LOG_BLE("  Expected phase_id at offset 17: 0x%02X\n", test_meas.phase_id);
    LOG_BLE("  Raw memory dump:\n    ");
    uint8_t* meas_bytes = (uint8_t*)&test_meas;
    for (int i = 0; i < sizeof(test_meas); i++) {
        LOG_BLE("%02X ", meas_bytes[i]);
        if ((i + 1) % 8 == 0) LOG_BLE("\n    ");
    }
    LOG_BLE("\n");
    
    // Now read actual flash data from individual session files
    LOG_BLE("\n--- READING ACTUAL FLASH DATA ---\n");
    
    // Try individual session files first
    if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
        File dir = LittleFS.open(GRIND_SESSIONS_DIR);
        if (dir && dir.isDirectory()) {
            File dirFile = dir.openNextFile();
            bool found_session = false;
            
            while (dirFile && !found_session) {
                String filename = dirFile.name();
                if (filename.startsWith("session_") && filename.endsWith(".bin")) {
                    String full_path = String(GRIND_SESSIONS_DIR) + "/" + filename;
                    File file = LittleFS.open(full_path.c_str(), "r");
                    if (file) {
                        LOG_BLE("Reading from: %s\n", filename.c_str());
                        found_session = true;
                        // Continue with existing logic but using this file
                        
                        TimeSeriesSessionHeader header;
                        GrindSession session;
                        int session_count = 0;
                        const int MAX_SESSIONS_TO_DUMP = 2;
                        const int MAX_EVENTS_PER_SESSION = 10;
                        const int MAX_MEASUREMENTS_PER_SESSION = 10;
                        
                        while (file.available() >= sizeof(TimeSeriesSessionHeader) && session_count < MAX_SESSIONS_TO_DUMP) {
                            // Read session header
                            if (file.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) break;
                            
                            LOG_BLE("\n=== FLASH SESSION %d ===\n", session_count + 1);
        LOG_BLE("TimeSeriesSessionHeader:\n");
        LOG_BLE("  session_id: %lu\n", header.session_id);
        LOG_BLE("  event_count: %u\n", header.event_count);
        LOG_BLE("  measurement_count: %u\n", header.measurement_count);
        LOG_BLE("  session_timestamp: %lu\n", header.session_timestamp);
        LOG_BLE("  session_size: %lu\n", header.session_size);
        LOG_BLE("  checksum: %lu\n", header.checksum);
        
        // Read session data
        if (file.read((uint8_t*)&session, sizeof(session)) != sizeof(session)) break;
        
        LOG_BLE("GrindSession:\n");
        LOG_BLE("  session_id: %lu\n", session.session_id);
        LOG_BLE("  session_timestamp: %lu\n", session.session_timestamp);
        LOG_BLE("  profile_id: %u\n", session.profile_id);
        LOG_BLE("  target_weight: %.3f\n", session.target_weight);
        LOG_BLE("  tolerance: %.3f\n", session.tolerance);
        LOG_BLE("  final_weight: %.3f\n", session.final_weight);
        LOG_BLE("  error_grams: %.3f\n", session.error_grams);
        LOG_BLE("  total_time_ms: %lu\n", session.total_time_ms);
        LOG_BLE("  pulse_count: %u\n", session.pulse_count);
        LOG_BLE("  result_status: '%.16s'\n", session.result_status);
        LOG_BLE("  total_motor_on_time_ms: %lu\n", session.total_motor_on_time_ms);
        
        // Raw memory dump of actual session
        LOG_BLE("GrindSession raw memory (%d bytes):\n  ", sizeof(session));
        uint8_t* session_bytes = (uint8_t*)&session;
        for (int i = 0; i < sizeof(session); i++) {
            LOG_BLE("%02X ", session_bytes[i]);
            if ((i + 1) % 16 == 0) LOG_BLE("\n  ");
        }
        LOG_BLE("\n");
        
        // Read and dump events with full raw data
        LOG_BLE("Events (showing first %d of %d):\n", min(MAX_EVENTS_PER_SESSION, (int)header.event_count), header.event_count);
        for (int i = 0; i < min(MAX_EVENTS_PER_SESSION, (int)header.event_count); i++) {
            GrindEvent event;
            if (file.read((uint8_t*)&event, sizeof(event)) != sizeof(event)) break;
            
            LOG_BLE("  Event %d:\n", i);
            LOG_BLE("    timestamp_ms: %lu, phase_id: %u, pulse_attempt: %u\n", 
                         event.timestamp_ms, event.phase_id, event.pulse_attempt_number);
            LOG_BLE("    event_sequence_id: %u, duration_ms: %lu\n", 
                         event.event_sequence_id, event.duration_ms);
            LOG_BLE("    start_weight: %.3f, end_weight: %.3f\n", 
                         event.start_weight, event.end_weight);
            
            // Full raw dump of this event
            LOG_BLE("    Raw bytes (%d total): ", sizeof(event));
            uint8_t* event_bytes = (uint8_t*)&event;
            for (int j = 0; j < sizeof(event); j++) {
                LOG_BLE("%02X ", event_bytes[j]);
            }
            LOG_BLE("\n");
        }
        
        // Skip remaining events
        int remaining_events = header.event_count - min(MAX_EVENTS_PER_SESSION, (int)header.event_count);
        file.seek(file.position() + remaining_events * sizeof(GrindEvent));
        
        // Read and dump measurements with raw data
        LOG_BLE("Measurements (showing first %d of %d):\n", min(MAX_MEASUREMENTS_PER_SESSION, (int)header.measurement_count), header.measurement_count);
        for (int i = 0; i < min(MAX_MEASUREMENTS_PER_SESSION, (int)header.measurement_count); i++) {
            GrindMeasurement meas;
            if (file.read((uint8_t*)&meas, sizeof(meas)) != sizeof(meas)) break;
            
            LOG_BLE("  Measurement %d:\n", i);
            LOG_BLE("    timestamp_ms: %lu, weight: %.3f, delta: %.3f\n", 
                         meas.timestamp_ms, meas.weight_grams, meas.weight_delta);
            LOG_BLE("    flow_rate: %.3f, motor_on: %u, phase_id: %u\n", 
                         meas.flow_rate_g_per_s, meas.motor_is_on, meas.phase_id);
            
            // Raw dump of this measurement
            LOG_BLE("    Raw bytes (%d total): ", sizeof(meas));
            uint8_t* meas_bytes = (uint8_t*)&meas;
            for (int j = 0; j < sizeof(meas); j++) {
                LOG_BLE("%02X ", meas_bytes[j]);
            }
            LOG_BLE("\n");
        }
        
        // Skip remaining measurements
        int remaining_measurements = header.measurement_count - min(MAX_MEASUREMENTS_PER_SESSION, (int)header.measurement_count);
        file.seek(file.position() + remaining_measurements * sizeof(GrindMeasurement));
        
                            session_count++;
                        }
                        file.close();
                        break; // Found and processed one session file
                    }
                }
                dirFile = dir.openNextFile();
            }
            dir.close();
            
            if (!found_session) {
                LOG_BLE("No valid session files found in directory\n");
            }
        }
    } else {
        LOG_BLE("No individual session files directory found\n");
    }
    
    LOG_BLE("\n=== END COMPREHENSIVE DEBUG ===\n");
}
#endif // ENABLE_GRIND_DEBUG

// ========== Individual Session File Management Functions ==========

bool GrindLogger::ensure_sessions_directory_exists() {
    // Check if directory exists
    if (!LittleFS.exists(GRIND_SESSIONS_DIR)) {
        LOG_BLE("Creating sessions directory...\n");
        if (!LittleFS.mkdir(GRIND_SESSIONS_DIR)) {
            LOG_BLE("ERROR: Failed to create sessions directory\n");
            return false;
        }
        LOG_BLE("Sessions directory created successfully\n");
    }
    return true;
}

bool GrindLogger::write_individual_session_file(uint32_t session_id, const GrindSession& session, const GrindEvent* events, const GrindMeasurement* measurements) {
    char filename[64];
    snprintf(filename, sizeof(filename), SESSION_FILE_FORMAT, session_id);
    
    File file = LittleFS.open(filename, "w");
    if (!file) {
        LOG_BLE("ERROR: Failed to open session file for writing: %s\n", filename);
        return false;
    }
    
    // Calculate sizes
    size_t events_size = event_count * sizeof(GrindEvent);
    size_t measurements_size = measurement_count * sizeof(GrindMeasurement);
    size_t total_data_size = sizeof(GrindSession) + events_size + measurements_size;
    
    // Create and write session header (for compatibility with existing parsing)
    TimeSeriesSessionHeader header;
    header.session_id = session_id;
    header.session_timestamp = session.session_timestamp;
    header.session_size = total_data_size;
    header.checksum = calculate_checksum((const uint8_t*)&session, total_data_size);
    header.event_count = event_count;
    header.measurement_count = measurement_count;
    header.schema_version = GRIND_LOG_SCHEMA_VERSION;
    header.reserved = 0;
    
    // Write header, session, events, and measurements
    if (file.write((uint8_t*)&header, sizeof(header)) != sizeof(header) ||
        file.write((uint8_t*)&session, sizeof(session)) != sizeof(session) ||
        (events_size > 0 && file.write((uint8_t*)events, events_size) != events_size) ||
        (measurements_size > 0 && file.write((uint8_t*)measurements, measurements_size) != measurements_size)) {
        
        file.close();
        LittleFS.remove(filename); // Clean up partial file
        LOG_BLE("ERROR: Failed to write session data to file: %s\n", filename);
        return false;
    }
    
    file.close();
    LOG_BLE("Successfully wrote session %lu to file (%zu bytes)\n", session_id, total_data_size + sizeof(header));
    return true;
}

bool GrindLogger::validate_session_file(uint32_t session_id) {
    char filename[64];
    snprintf(filename, sizeof(filename), SESSION_FILE_FORMAT, session_id);
    
    if (!LittleFS.exists(filename)) {
        return false;
    }
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        return false;
    }
    
    // Check basic file structure
    TimeSeriesSessionHeader header;
    if (file.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        file.close();
        return false;
    }
    
    // Basic validation: session_id should match filename and file size should be reasonable
    if (header.session_id != session_id || header.session_size == 0 || header.session_size > 100000) {
        file.close();
        return false;
    }
    
    file.close();
    return true;
}

bool GrindLogger::remove_session_file(uint32_t session_id) {
    char filename[64];
    snprintf(filename, sizeof(filename), SESSION_FILE_FORMAT, session_id);
    
    if (LittleFS.exists(filename)) {
        bool result = LittleFS.remove(filename);
        if (result) {
            LOG_BLE("Removed old session file: %lu\n", session_id);
        } else {
            LOG_BLE("WARNING: Failed to remove session file: %lu\n", session_id);
        }
        return result;
    }
    return true; // File doesn't exist, so "removal" succeeded
}

void GrindLogger::cleanup_old_session_files() {
    File dir = LittleFS.open(GRIND_SESSIONS_DIR);
    if (!dir || !dir.isDirectory()) {
        LOG_BLE("WARNING: Cannot open sessions directory for cleanup.\n");
        return;
    }

    // Step 1: Count files and collect session IDs
    uint32_t session_count = 0;
    File file = dir.openNextFile();
    while (file) {
        session_count++;
        file = dir.openNextFile();
    }
    dir.close(); // Close after counting

    if (session_count <= MAX_STORED_SESSIONS_FLASH) {
        return; // No cleanup needed
    }

    LOG_BLE("Session count (%lu) exceeds limit (%d). Cleaning up old files...\n", session_count, MAX_STORED_SESSIONS_FLASH);

    // Step 2: Create a list of session IDs
    uint32_t* session_ids = (uint32_t*)malloc(session_count * sizeof(uint32_t));
    if (!session_ids) {
        LOG_BLE("ERROR: Failed to allocate memory for session ID list during cleanup.\n");
        return;
    }

    dir = LittleFS.open(GRIND_SESSIONS_DIR);
    uint32_t list_idx = 0;
    file = dir.openNextFile();
    while (file && list_idx < session_count) {
        String filename = file.name();
        int start_pos = filename.indexOf('_') + 1;
        int end_pos = filename.lastIndexOf('.');
        if (start_pos > 0 && end_pos > start_pos) {
            session_ids[list_idx++] = filename.substring(start_pos, end_pos).toInt();
        }
        file = dir.openNextFile();
    }
    dir.close();

    // Step 3: Sort the session IDs in ascending order
    std::sort(session_ids, session_ids + session_count);

    // Step 4: Remove the oldest files
    uint32_t files_to_remove = session_count - MAX_STORED_SESSIONS_FLASH;
    for (uint32_t i = 0; i < files_to_remove; i++) {
        remove_session_file(session_ids[i]);
    }

    free(session_ids);
    LOG_BLE("Cleanup complete. Removed %lu old session(s).\n", files_to_remove);
}
