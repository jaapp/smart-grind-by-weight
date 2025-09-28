#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../config/constants.h"

// Forward declarations
class GrindController;
class WeightSensor;
class Grinder;
class GrindLogger;

/**
 * GrindControlTask - Dedicated Grind Control Processing
 * 
 * Extracted from RealtimeController to create a focused, high-priority task
 * that handles ONLY grind control operations and algorithms.
 * 
 * Responsibilities:
 * - Execute GrindController logic at 50Hz on Core 0
 * - Handle predictive grinding algorithms
 * - Manage grind state machine transitions
 * - Process pulse correction logic
 * - Coordinate with WeightSamplingTask for real-time data
 * 
 * Architecture:
 * - Runs on Core 0 at high priority (3)
 * - Uses vTaskDelayUntil for predictable timing
 * - Thread-safe coordination with weight sampling
 * - Real-time grind control without file I/O blocking
 */
class GrindControlTask {
private:
    // Hardware and controller interfaces
    GrindController* grind_controller;
    WeightSensor* weight_sensor;
    Grinder* grinder;
    GrindLogger* logger;
    
    // Task management
    TaskHandle_t task_handle;
    volatile bool task_running;
    
    // Performance monitoring
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;
    
    // Grind control state
    bool grind_active;
    uint32_t grind_start_time;
    uint32_t last_grind_update_time;
    
    // Static instance for task callback
    static GrindControlTask* instance;
    
public:
    GrindControlTask();
    ~GrindControlTask();
    
    // Initialization
    void init(GrindController* gc, WeightSensor* ws, Grinder* gr, GrindLogger* log);
    
    // Task lifecycle
    bool start_task();
    void stop_task();
    bool is_running() const { return task_running; }
    
    // Grind state monitoring
    bool is_grind_active() const { return grind_active; }
    uint32_t get_grind_duration_ms() const;
    
    // Performance monitoring
    uint32_t get_cycle_count() const { return cycle_count; }
    void print_performance_stats() const;
    
    // Static task wrapper
    static void task_wrapper(void* parameter);
    
    // Task implementation (public for TaskManager access)
    void task_impl();
    
    // Hardware validation (public for TaskManager access)
    bool validate_hardware_ready() const;
    
private:
    
    // Grind control coordination
    void update_grind_control();
    void monitor_grind_state();
    
    // Performance tracking
    void record_timing(uint32_t start_time, uint32_t end_time);
    void print_heartbeat() const;
    void reset_performance_metrics();
    
    // Error handling
    void handle_grind_error();
};

// Global instance
extern GrindControlTask grind_control_task;