#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../config/constants.h"

// Forward declarations
class HardwareManager;
class StateMachine; 
class ProfileController;
class GrindController;
class BluetoothManager;
class UIManager;

// Task handle storage for all FreeRTOS tasks
struct TaskHandles {
    TaskHandle_t weight_sampling_task;
    TaskHandle_t grind_control_task;
    TaskHandle_t ui_render_task;
    TaskHandle_t bluetooth_task;
    TaskHandle_t file_io_task;
};

// Inter-task communication queues
struct TaskQueues {
    QueueHandle_t ui_to_grind_queue;        // UI events → Grind Controller
    QueueHandle_t file_io_queue;            // Any task → File I/O
};

// Task timing metrics for monitoring
struct TaskMetrics {
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;
    
    TaskMetrics() : cycle_count(0), cycle_time_sum_ms(0), 
                   cycle_time_min_ms(UINT32_MAX), cycle_time_max_ms(0), 
                   last_heartbeat_time(0) {}
};

/**
 * TaskManager - Centralized FreeRTOS Task Management
 * 
 * Responsibilities:
 * - Create and manage all FreeRTOS tasks
 * - Setup inter-task communication queues
 * - Monitor task health and performance
 * - Handle task suspend/resume for OTA operations
 * - Provide task-based heartbeat reporting
 * 
 * Architecture:
 * - 5 specialized FreeRTOS tasks with core pinning
 * - Predictable timing using vTaskDelayUntil
 * - Thread-safe inter-task communication via queues
 * - Performance monitoring per task
 */
class TaskManager {
private:
    // Task handles and queues
    TaskHandles task_handles;
    TaskQueues task_queues;
    
    // Hardware and system references
    HardwareManager* hardware_manager;
    StateMachine* state_machine;
    ProfileController* profile_controller; 
    GrindController* grind_controller;
    BluetoothManager* bluetooth_manager;
    UIManager* ui_manager;
    
    // Task monitoring
    TaskMetrics task_metrics[5]; // One for each task
    bool tasks_initialized;
    bool ota_suspended;
    
    // Static instance for task callbacks
    static TaskManager* instance;
    
public:
    TaskManager();
    ~TaskManager();
    
    // Initialization
    bool init(HardwareManager* hw_mgr, StateMachine* sm, ProfileController* pc,
              GrindController* gc, BluetoothManager* bluetooth, UIManager* ui);
    
    // Task lifecycle management
    bool create_all_tasks();
    void suspend_hardware_tasks();  // For OTA operations
    void resume_hardware_tasks();   // After OTA operations
    void delete_all_tasks();
    
    // Queue access
    QueueHandle_t get_ui_to_grind_queue() const { return task_queues.ui_to_grind_queue; }
    QueueHandle_t get_file_io_queue() const { return task_queues.file_io_queue; }
    
    // Task monitoring
    bool are_tasks_healthy() const;
    void print_task_status() const;
    
    // Static task function wrappers
    static void weight_sampling_task_wrapper(void* parameter);
    static void grind_control_task_wrapper(void* parameter);
    static void ui_render_task_wrapper(void* parameter);
    static void bluetooth_task_wrapper(void* parameter);
    static void file_io_task_wrapper(void* parameter);
    
private:
    // Task creation helpers
    bool create_weight_sampling_task();
    bool create_grind_control_task();
    bool create_ui_render_task();
    bool create_bluetooth_task();
    bool create_file_io_task();
    
    // Queue creation
    bool create_inter_task_queues();
    void cleanup_queues();
    
    // Task implementation methods
    void weight_sampling_task_impl();
    void grind_control_task_impl();
    void ui_render_task_impl();
    void bluetooth_task_impl();
    void file_io_task_impl();
    
    // Performance monitoring
    void record_task_timing(int task_index, uint32_t start_time, uint32_t end_time);
    void print_task_heartbeat(int task_index, const char* task_name) const;
    
    // Task validation
    bool validate_hardware_ready() const;
};

// Global task manager instance
extern TaskManager task_manager;