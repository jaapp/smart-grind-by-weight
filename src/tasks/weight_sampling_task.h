#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../config/constants.h"

// Forward declarations
class WeightSensor;
class GrindLogger;

/**
 * WeightSamplingTask - Dedicated Weight Sensor Sampling
 * 
 * Extracted from RealtimeController to create a focused, high-priority task
 * that handles ONLY weight sensor sampling operations.
 * 
 * Responsibilities:
 * - Non-blocking HX711 sampling (polling at fixed rate; sensor delivers at HW rate)
 * - Feed data to CircularBufferMath filters
 * - Hardware initialization on Core 0
 * - SPS performance monitoring
 * - Hardware validation and error recovery
 * 
 * Architecture:
 * - Runs on Core 0 at highest priority (4)
 * - Uses vTaskDelayUntil for predictable timing
 * - Thread-safe access to weight sensor hardware
 * - No file I/O or blocking operations
 */
class WeightSamplingTask {
private:
    // Hardware interface
    WeightSensor* weight_sensor;
    GrindLogger* logger;
    
    // Task timing and performance
    TaskHandle_t task_handle;
    volatile bool task_running;
    
    // Performance monitoring
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;
    
    // Hardware state
    bool hardware_initialized;
    bool hardware_validation_passed;
    
    // Static instance for task callback
    static WeightSamplingTask* instance;
    
public:
    WeightSamplingTask();
    ~WeightSamplingTask();
    
    // Initialization
    void init(WeightSensor* ws, GrindLogger* log);
    
    // Task lifecycle
    bool start_task();
    void stop_task();
    bool is_running() const { return task_running; }
    bool is_hardware_ready() const { return hardware_initialized && hardware_validation_passed; }
    
    // Performance monitoring
    float get_current_sps() const;
    uint32_t get_cycle_count() const { return cycle_count; }
    void print_performance_stats() const;
    
    // Static task wrapper
    static void task_wrapper(void* parameter);
    
    // Task implementation (public for TaskManager access)
    void task_impl();
    
    // Hardware validation (public for TaskManager access)
    bool validate_hardware_ready() const;
    
private:
    
    // Hardware management (extracted from RealtimeController)
    bool initialize_hx711_hardware();
    void sample_and_feed_weight_sensor();
    
    // Performance tracking
    void record_timing(uint32_t start_time, uint32_t end_time);
    void print_heartbeat() const;
    void reset_performance_metrics();
    
    // Error handling
    void handle_hardware_error();
    bool attempt_hardware_recovery();
};

// Global instance
extern WeightSamplingTask weight_sampling_task;
