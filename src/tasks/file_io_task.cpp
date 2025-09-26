#include "file_io_task.h"
#include "../logging/grind_logging.h"
#include "../config/git_info.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

// Global instance
FileIOTask file_io_task;

// Static instance pointer for task callback
FileIOTask* FileIOTask::instance = nullptr;

FileIOTask::FileIOTask() {
    task_handle = nullptr;
    task_running = false;
    file_io_queue = nullptr;
    
    // Initialize file I/O state
    filesystem_available = false;
    total_operations_processed = 0;
    failed_operations_count = 0;
    last_filesystem_check_time = 0;
    
    // Initialize performance metrics
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
    last_heartbeat_time = 0;
    
    // Initialize operation statistics
    flash_operations_processed = 0;
    log_messages_processed = 0;
    preference_operations_processed = 0;
    data_export_operations_processed = 0;
    
    instance = this;
}

FileIOTask::~FileIOTask() {
    stop_task();
    
    if (instance == this) {
        instance = nullptr;
    }
}

void FileIOTask::init(QueueHandle_t io_queue) {
    file_io_queue = io_queue;
    
    // Check initial filesystem availability
    filesystem_available = LittleFS.begin(true);
    if (filesystem_available) {
        BLE_LOG("FileIOTask: LittleFS filesystem available\n");
    } else {
        BLE_LOG("FileIOTask: LittleFS filesystem unavailable\n");
    }
    
    BLE_LOG("FileIOTask: Initialized with file I/O queue\n");
}

bool FileIOTask::start_task() {
    if (task_running) {
        BLE_LOG("WARNING: FileIOTask already running\n");
        return false;
    }
    
    if (!file_io_queue) {
        BLE_LOG("ERROR: File I/O queue not initialized\n");
        return false;
    }
    
    BLE_LOG("FileIOTask: Starting task on Core 1...\n");
    task_running = true;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        task_wrapper,
        "FileIO",
        SYS_TASK_FILE_IO_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_FILE_IO,
        &task_handle,
        1  // Pin to Core 1
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create FileIOTask!\n");
        task_running = false;
        task_handle = nullptr;
        return false;
    }
    
    BLE_LOG("✅ FileIOTask created successfully (Core 1, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_FILE_IO, 1000 / SYS_TASK_FILE_IO_INTERVAL_MS);
    return true;
}

void FileIOTask::stop_task() {
    if (!task_running) {
        return;
    }
    
    BLE_LOG("FileIOTask: Stopping task...\n");
    task_running = false;
    
    // Wait for task to complete (with timeout)
    if (task_handle) {
        uint32_t timeout_start = millis();
        while (eTaskGetState(task_handle) != eDeleted && millis() - timeout_start < 1000) {
            delay(10);
        }
        task_handle = nullptr;
    }
    
    BLE_LOG("FileIOTask: Task stopped\n");
}

void FileIOTask::task_wrapper(void* parameter) {
    if (instance) {
        instance->task_impl();
    }
    vTaskDelete(nullptr);
}

void FileIOTask::task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_FILE_IO_INTERVAL_MS);
    
    BLE_LOG("FileIOTask started on Core %d at %dHz\n", 
            xPortGetCoreID(), 1000 / SYS_TASK_FILE_IO_INTERVAL_MS);
    
    // When invoked via TaskManager wrapper, start_task() isn't used.
    // Ensure the internal run flag is set so the loop executes.
    task_running = true;
    
    // Reset performance metrics
    reset_performance_metrics();
    
    // Main file I/O processing loop
    while (task_running) {
        uint32_t cycle_start_time = millis();
        
        // Process file I/O operations from queue
        process_file_io_operations();

        // Also drain legacy GrindController queues here so flash operations
        // (start/end session) run on Core 1 in this low-priority task
        extern GrindController grind_controller;
        grind_controller.process_queued_flash_operations();
        grind_controller.process_queued_log_messages();
        
        // Periodic filesystem health check
        if (cycle_start_time - last_filesystem_check_time >= 30000) { // Every 30 seconds
            check_filesystem_health();
            last_filesystem_check_time = cycle_start_time;
        }
        
        uint32_t cycle_end_time = millis();
        
        // Record performance metrics
        record_timing(cycle_start_time, cycle_end_time);
        
        // Use vTaskDelayUntil for predictable timing (eliminates busy-wait)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    
    BLE_LOG("FileIOTask: I/O processing loop stopped\n");
}

void FileIOTask::process_file_io_operations() {
    // If init() has not been called yet by main (queue not set), skip this cycle
    if (!file_io_queue) {
        return;
    }

    FileIORequest request;
    
    // Process all queued file I/O operations (non-blocking)
    while (xQueueReceive(file_io_queue, &request, 0) == pdPASS) {
        total_operations_processed++;
        
        switch (request.operation_type) {
            case FileIOOperationType::FLASH_OPERATION:
                process_flash_operation(request.flash_op);
                flash_operations_processed++;
                break;
                
            case FileIOOperationType::LOG_MESSAGE:
                process_log_message(request.log_msg);
                log_messages_processed++;
                break;
                
            case FileIOOperationType::PREFERENCE_WRITE:
                process_preference_write(request.preference.key, request.preference.value);
                preference_operations_processed++;
                break;
                
            case FileIOOperationType::DATA_EXPORT:
                process_data_export(request.data_export.export_path, 
                                  request.data_export.start_session_id,
                                  request.data_export.end_session_id);
                data_export_operations_processed++;
                break;
                
            default:
                BLE_LOG("WARNING: FileIOTask unknown operation type %d\n", (int)request.operation_type);
                failed_operations_count++;
                break;
        }
    }
}

void FileIOTask::process_flash_operation(const FlashOpRequest& request) {
    // This is the existing flash operation processing extracted from GrindController
    switch (request.operation_type) {
        case FlashOpRequest::START_GRIND_SESSION:
            BLE_LOG("[%lums FLASH_OP] Processing START_GRIND_SESSION: mode=%s, profile=%d\n", 
                    millis(), request.descriptor.mode == GrindMode::TIME ? "TIME" : "WEIGHT",
                    request.descriptor.profile_id);
            grind_logger.start_grind_session(request.descriptor, request.start_weight);
            break;
            
        case FlashOpRequest::END_GRIND_SESSION:
            BLE_LOG("[%lums FLASH_OP] Processing END_GRIND_SESSION: %s, %.2fg, %d pulses\n", 
                    millis(), request.result_string, request.final_weight, request.pulse_count);
            grind_logger.end_grind_session(request.result_string, request.final_weight, request.pulse_count);
            break;
            
        default:
            BLE_LOG("WARNING: FileIOTask unknown flash operation type %d\n", request.operation_type);
            failed_operations_count++;
            break;
    }
}

void FileIOTask::process_log_message(const LogMessage& message) {
    // Output the log message using BLE_LOG (extracted from GrindController)
    BLE_LOG("%s", message.message);
}

void FileIOTask::process_preference_write(const char* key, const char* value) {
    if (!filesystem_available) {
        failed_operations_count++;
        return;
    }
    
    // Write preference/setting to persistent storage
    Preferences prefs;
    if (prefs.begin("grinder", false)) {
        size_t written = prefs.putString(key, value);
        prefs.end();
        
        if (written == 0) {
            BLE_LOG("WARNING: Failed to write preference %s=%s\n", key, value);
            failed_operations_count++;
        } else {
            BLE_LOG("FileIOTask: Preference written %s=%s\n", key, value);
        }
    } else {
        BLE_LOG("ERROR: Failed to open preferences for writing\n");
        failed_operations_count++;
    }
}

void FileIOTask::process_data_export(const char* export_path, uint32_t start_id, uint32_t end_id) {
    if (!filesystem_available) {
        failed_operations_count++;
        return;
    }
    
    BLE_LOG("FileIOTask: Data export requested: %s (sessions %lu-%lu)\n", export_path, start_id, end_id);
    
    // Placeholder for data export implementation
    // This could be extended to export grind session data to files
    // For now, just log the request
    BLE_LOG("FileIOTask: Data export processing not yet implemented\n");
}

void FileIOTask::check_filesystem_health() {
    bool fs_available = validate_filesystem_access();
    
    if (fs_available != filesystem_available) {
        filesystem_available = fs_available;
        BLE_LOG("FileIOTask: Filesystem availability changed to %s\n", fs_available ? "AVAILABLE" : "UNAVAILABLE");
        
        if (!fs_available) {
            handle_filesystem_error();
        }
    }
    
    if (filesystem_available) {
        perform_filesystem_maintenance();
    }
}

bool FileIOTask::validate_filesystem_access() {
    // Try a simple filesystem operation to validate access
    File test_file = LittleFS.open("/test_access", "w");
    if (test_file) {
        test_file.println("test");
        test_file.close();
        LittleFS.remove("/test_access");
        return true;
    }
    return false;
}

void FileIOTask::perform_filesystem_maintenance() {
    // Placeholder for filesystem maintenance operations
    // Could include:
    // - Log rotation
    // - Old file cleanup
    // - Filesystem defragmentation
    // - Storage usage monitoring
}

void FileIOTask::handle_filesystem_error() {
    BLE_LOG("FileIOTask: Filesystem error detected\n");
    
    // Attempt filesystem recovery
    if (attempt_filesystem_recovery()) {
        BLE_LOG("FileIOTask: Filesystem recovery successful\n");
    } else {
        BLE_LOG("ERROR: FileIOTask: Filesystem recovery failed\n");
    }
}

bool FileIOTask::attempt_filesystem_recovery() {
    BLE_LOG("FileIOTask: Attempting filesystem recovery...\n");
    
    // Try to remount the filesystem
    LittleFS.end();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
    
    bool recovery_success = LittleFS.begin(true);
    if (recovery_success) {
        filesystem_available = true;
    }
    
    return recovery_success;
}

void FileIOTask::log_operation_failure(FileIOOperationType type, const char* details) {
    const char* type_names[] = {"FLASH_OP", "LOG_MSG", "PREF_WRITE", "DATA_EXPORT"};
    const char* type_name = ((int)type < 4) ? type_names[(int)type] : "UNKNOWN";
    
    BLE_LOG("ERROR: FileIOTask operation failed - Type: %s, Details: %s\n", type_name, details);
    failed_operations_count++;
}

void FileIOTask::record_timing(uint32_t start_time, uint32_t end_time) {
    uint32_t cycle_duration = end_time - start_time;
    
    cycle_count++;
    cycle_time_sum_ms += cycle_duration;
    
    if (cycle_duration < cycle_time_min_ms) {
        cycle_time_min_ms = cycle_duration;
    }
    if (cycle_duration > cycle_time_max_ms) {
        cycle_time_max_ms = cycle_duration;
    }
    
#if ENABLE_REALTIME_HEARTBEAT
    // Print task heartbeat every 10 seconds
    if (last_heartbeat_time == 0) {
        last_heartbeat_time = start_time;
    }
    
    if (end_time - last_heartbeat_time >= REALTIME_HEARTBEAT_INTERVAL_MS) {
        print_heartbeat();
        reset_performance_metrics();
        last_heartbeat_time = end_time;
    }
#endif
}

void FileIOTask::print_heartbeat() const {
#if ENABLE_REALTIME_HEARTBEAT
    uint32_t avg_cycle_time = cycle_count > 0 ? cycle_time_sum_ms / cycle_count : 0;
    const char* fs_status = filesystem_available ? "OK" : "ERROR";
    
    BLE_LOG("[%lums FILE_IO_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | FS: %s | Ops: %lu | Failed: %lu | Build: #%d\n",
           millis(), cycle_count, avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms,
           fs_status, total_operations_processed, failed_operations_count, BUILD_NUMBER);
#endif
}

void FileIOTask::reset_performance_metrics() {
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
}

void FileIOTask::print_performance_stats() const {
    BLE_LOG("=== FileIOTask Performance ===\n");
    BLE_LOG("Task running: %s\n", task_running ? "YES" : "NO");
    BLE_LOG("Filesystem available: %s\n", filesystem_available ? "YES" : "NO");
    BLE_LOG("Cycle count: %lu\n", cycle_count);
    
    if (cycle_count > 0) {
        uint32_t avg_cycle_time = cycle_time_sum_ms / cycle_count;
        BLE_LOG("Average cycle time: %lums (%lu-%lums)\n", avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms);
    }
    
    BLE_LOG("Total operations: %lu\n", total_operations_processed);
    BLE_LOG("Failed operations: %lu\n", failed_operations_count);
    BLE_LOG("=============================\n");
}

void FileIOTask::print_operation_stats() const {
    BLE_LOG("=== FileIOTask Operation Statistics ===\n");
    BLE_LOG("Flash operations: %lu\n", flash_operations_processed);
    BLE_LOG("Log messages: %lu\n", log_messages_processed);
    BLE_LOG("Preference writes: %lu\n", preference_operations_processed);
    BLE_LOG("Data exports: %lu\n", data_export_operations_processed);
    BLE_LOG("Total operations: %lu\n", total_operations_processed);
    BLE_LOG("Failed operations: %lu\n", failed_operations_count);
    BLE_LOG("Success rate: %.1f%%\n", 
           total_operations_processed > 0 ? 
           (100.0f * (total_operations_processed - failed_operations_count) / total_operations_processed) : 0.0f);
    BLE_LOG("======================================\n");
}
