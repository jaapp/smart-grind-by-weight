#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../config/system_config.h"
#include "../config/logging.h"
#include "../controllers/grind_controller.h" // For FlashOpRequest and LogMessage structures

// File I/O operation types
enum class FileIOOperationType {
    FLASH_OPERATION,        // Flash operations (start/end grind session)
    LOG_MESSAGE,           // Log message output
    PREFERENCE_WRITE,      // Preference/settings persistence
    DATA_EXPORT           // Data export operations
};

// Generic file I/O request structure
struct FileIORequest {
    FileIOOperationType operation_type = FileIOOperationType::LOG_MESSAGE;

    FlashOpRequest flash_op{};           // For FLASH_OPERATION
    LogMessage log_msg{};                // For LOG_MESSAGE
    struct {                            // For PREFERENCE_WRITE
        char key[32];
        char value[64];
    } preference{};
    struct {                            // For DATA_EXPORT
        char export_path[64];
        uint32_t start_session_id;
        uint32_t end_session_id;
    } data_export{};
};

/**
 * FileIOTask - Dedicated File I/O and Storage Operations
 * 
 * Extracted from GrindController queues to create a focused, low-priority task
 * that handles ALL file system operations without blocking real-time tasks.
 * 
 * Responsibilities:
 * - Process flash operations (grind session logging)
 * - Handle log message output to serial/BLE
 * - Manage preference/settings persistence
 * - Coordinate data export operations
 * - File system maintenance and cleanup
 * 
 * Architecture:
 * - Runs on Core 1 at low priority (1)
 * - Uses vTaskDelayUntil for predictable timing
 * - Receives requests via FreeRTOS queue from any task
 * - All blocking LittleFS operations isolated here
 */
class FileIOTask {
private:
    // Task management
    TaskHandle_t task_handle;
    volatile bool task_running;
    
    // Inter-task communication
    QueueHandle_t file_io_queue;
    
    // File I/O state
    bool filesystem_available;
    uint32_t total_operations_processed;
    uint32_t failed_operations_count;
    uint32_t last_filesystem_check_time;
    
    // Performance monitoring
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;
    
    // Operation statistics
    uint32_t flash_operations_processed;
    uint32_t log_messages_processed;
    uint32_t preference_operations_processed;
    uint32_t data_export_operations_processed;
    
    // Static instance for task callback
    static FileIOTask* instance;
    
public:
    FileIOTask();
    ~FileIOTask();
    
    // Initialization
    void init(QueueHandle_t io_queue);
    
    // Task lifecycle
    bool start_task();
    void stop_task();
    bool is_running() const { return task_running; }
    
    // File system status
    bool is_filesystem_available() const { return filesystem_available; }
    uint32_t get_total_operations() const { return total_operations_processed; }
    uint32_t get_failed_operations() const { return failed_operations_count; }
    
    // Performance monitoring
    uint32_t get_cycle_count() const { return cycle_count; }
    void print_performance_stats() const;
    void print_operation_stats() const;
    
    // Static task wrapper
    static void task_wrapper(void* parameter);
    
    // Task implementation (public for TaskManager access)
    void task_impl();
    
private:
    
    // File I/O operation processing
    void process_file_io_operations();
    void process_flash_operation(const FlashOpRequest& request);
    void process_log_message(const LogMessage& message);
    void process_preference_write(const char* key, const char* value);
    void process_data_export(const char* export_path, uint32_t start_id, uint32_t end_id);
    
    // File system management
    void check_filesystem_health();
    bool validate_filesystem_access();
    void perform_filesystem_maintenance();
    
    // Performance tracking
    void record_timing(uint32_t start_time, uint32_t end_time);
    void print_heartbeat() const;
    void reset_performance_metrics();
    
    // Error handling
    void handle_filesystem_error();
    bool attempt_filesystem_recovery();
    void log_operation_failure(FileIOOperationType type, const char* details);
};

// Global instance
extern FileIOTask file_io_task;
