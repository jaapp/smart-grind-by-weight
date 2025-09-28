#include "weight_sampling_task.h"
#include "../hardware/WeightSensor.h"
#include "../logging/grind_logging.h"
#include "../config/constants.h"
#include <Arduino.h>
#include <esp_task_wdt.h>

// Global instance
WeightSamplingTask weight_sampling_task;

// Static instance pointer for task callback
WeightSamplingTask* WeightSamplingTask::instance = nullptr;

WeightSamplingTask::WeightSamplingTask() {
    weight_sensor = nullptr;
    logger = nullptr;
    task_handle = nullptr;
    task_running = false;
    
    // Initialize performance metrics
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
    last_heartbeat_time = 0;
    
    // Initialize hardware state
    hardware_initialized = false;
    hardware_validation_passed = false;
    
    instance = this;
}

WeightSamplingTask::~WeightSamplingTask() {
    stop_task();
    
    if (instance == this) {
        instance = nullptr;
    }
}

void WeightSamplingTask::init(WeightSensor* ws, GrindLogger* log) {
    weight_sensor = ws;
    logger = log;
    
    BLE_LOG("WeightSamplingTask: Initialized with hardware interfaces\n");
}

bool WeightSamplingTask::start_task() {
    if (task_running) {
        BLE_LOG("WARNING: WeightSamplingTask already running\n");
        return false;
    }
    
    BLE_LOG("WeightSamplingTask: Validating hardware interfaces...\n");
    if (!validate_hardware_ready()) {
        BLE_LOG("ERROR: Hardware not ready for weight sampling task\n");
        return false;
    }
    
    BLE_LOG("WeightSamplingTask: Starting task on Core 0...\n");
    task_running = true;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        task_wrapper,
        "WeightSampling",
        SYS_TASK_WEIGHT_SAMPLING_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_WEIGHT_SAMPLING,
        &task_handle,
        0  // Pin to Core 0
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create WeightSamplingTask!\n");
        task_running = false;
        task_handle = nullptr;
        return false;
    }
    
    BLE_LOG("✅ WeightSamplingTask created successfully (Core 0, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_WEIGHT_SAMPLING, 1000 / SYS_TASK_WEIGHT_SAMPLING_INTERVAL_MS);
    return true;
}

void WeightSamplingTask::stop_task() {
    if (!task_running) {
        return;
    }
    
    BLE_LOG("WeightSamplingTask: Stopping task...\n");
    task_running = false;
    
    // Wait for task to complete (with timeout)
    if (task_handle) {
        uint32_t timeout_start = millis();
        while (eTaskGetState(task_handle) != eDeleted && millis() - timeout_start < 1000) {
            delay(10);
        }
        task_handle = nullptr;
    }
    
    BLE_LOG("WeightSamplingTask: Task stopped\n");
}

void WeightSamplingTask::task_wrapper(void* parameter) {
    if (instance) {
        instance->task_impl();
    }
    vTaskDelete(nullptr);
}

void WeightSamplingTask::task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_WEIGHT_SAMPLING_INTERVAL_MS);
    
    BLE_LOG("WeightSamplingTask started on Core %d at %dHz\n", 
            xPortGetCoreID(), 1000 / SYS_TASK_WEIGHT_SAMPLING_INTERVAL_MS);
    
    // Initialize HX711 hardware on Core 0 before starting sampling loop
    if (!initialize_hx711_hardware()) {
        BLE_LOG("ERROR: Failed to initialize HX711 hardware on Core 0\n");
        task_running = false;
        return;
    }
    
    // When invoked via TaskManager wrapper, start_task() isn't used.
    // Ensure the internal run flag is set so the loop executes.
    task_running = true;
    BLE_LOG("WeightSamplingTask: Hardware initialization complete, starting sampling loop\n");
    
    // Add current task to watchdog monitoring
    esp_task_wdt_add(nullptr);
    
    // Reset performance metrics
    reset_performance_metrics();
    
    // Main sampling loop
    while (task_running) {
        uint32_t cycle_start_time = millis();
        
        // Core sampling operations (extracted from RealtimeController)
        sample_and_feed_weight_sensor();
        
        // Weight sensor state management (non-blocking operations only)
        if (weight_sensor) {
            weight_sensor->update();  // Coordinate tare state management
        }
        
        // Feed watchdog to prevent timeout
        esp_task_wdt_reset();
        
        uint32_t cycle_end_time = millis();
        
        // Record performance metrics
        record_timing(cycle_start_time, cycle_end_time);
        
        // Use vTaskDelayUntil for predictable timing (eliminates busy-wait)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    
    // Mark hardware as no longer initialized
    task_running = false;
    hardware_initialized = false;
    hardware_validation_passed = false;
    
    // Remove task from watchdog monitoring before exit
    esp_task_wdt_delete(nullptr);
    
    BLE_LOG("WeightSamplingTask: Sampling loop stopped\n");
}

bool WeightSamplingTask::initialize_hx711_hardware() {
    if (!weight_sensor) {
        BLE_LOG("ERROR: WeightSensor not available for hardware initialization\n");
        return false;
    }
    
    BLE_LOG("WeightSamplingTask: Initializing WeightSensor hardware on Core 0...\n");
    
    // Hardware reset sequence (extracted from RealtimeController)
    weight_sensor->power_down();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Use vTaskDelay instead of delay()
    weight_sensor->power_up();
    vTaskDelay(pdMS_TO_TICKS(500));  // Use vTaskDelay instead of delay()
    
    weight_sensor->begin();
    
    // Apply saved calibration factor
    float saved_cal_factor = weight_sensor->get_saved_calibration_factor();
    weight_sensor->set_calibration_factor(saved_cal_factor);
    
    // Hardware stabilization - wait for hardware to be ready
    BLE_LOG("  Waiting for WeightSensor hardware stabilization...\n");
    uint32_t start_time = millis();
    while (millis() - start_time < 2000) {
        if (weight_sensor->data_waiting_async()) {
            weight_sensor->update_async();
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Use vTaskDelay instead of delay()
    }
    
    // Validate hardware responds
    if (!weight_sensor->validate_hardware()) {
        BLE_LOG("ERROR: WeightSensor hardware validation failed - check wiring!\n");
        return false;
    }
    
    // Verify initialization
    BLE_LOG("  WeightSensor initialization complete:\n");
    BLE_LOG("    Calibration factor: %.2f\n", weight_sensor->get_calibration_factor());
    BLE_LOG("    Tare offset: %ld\n", weight_sensor->get_zero_offset());
    BLE_LOG("    Hardware ready: %s\n", weight_sensor->is_data_ready() ? "TRUE" : "FALSE");
    
    // Mark WeightSensor as hardware-ready
    weight_sensor->set_hardware_initialized();
    hardware_initialized = true;
    
    // Attempt single verification reading (optional since validation already succeeded)
    if (weight_sensor->update_async()) {
        float test_reading = weight_sensor->get_instant_weight();
        BLE_LOG("    Verification reading: %.3fg\n", test_reading);
    } else {
        BLE_LOG("    Verification reading: No sample ready yet (normal for 10 SPS after validation)\n");
    }
    
    // Hardware validation already succeeded, so initialization is successful
    hardware_validation_passed = true;
    
    BLE_LOG("✅ WeightSensor hardware initialization successful on Core 0\n");
    return hardware_validation_passed;
}

void WeightSamplingTask::sample_and_feed_weight_sensor() {
    if (!weight_sensor) return;
    
    // Call WeightSensor's Core 0 sampling method - this performs HX711 sampling
    // and feeds data to CircularBufferMath, updating all weight readings
    // (Extracted from RealtimeController::sample_and_feed_weight_sensor)
    bool sample_taken = weight_sensor->sample_and_feed_filter();
    
#if ENABLE_REALTIME_HEARTBEAT
    // Record timestamp for SPS tracking when a sample was actually taken
    if (sample_taken) {
        weight_sensor->record_sample_timestamp();
    }
#endif
}

bool WeightSamplingTask::validate_hardware_ready() const {
    bool weight_sensor_ready = (weight_sensor != nullptr && weight_sensor->is_initialized());
    
    BLE_LOG("WeightSamplingTask hardware validation:\n");
    BLE_LOG("  weight_sensor != nullptr: %s\n", (weight_sensor != nullptr) ? "YES" : "NO");
    BLE_LOG("  weight_sensor initialized: %s\n", weight_sensor_ready ? "YES" : "NO");
    
    return weight_sensor_ready;
}

void WeightSamplingTask::record_timing(uint32_t start_time, uint32_t end_time) {
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

void WeightSamplingTask::print_heartbeat() const {
#if ENABLE_REALTIME_HEARTBEAT
    uint32_t avg_cycle_time = cycle_count > 0 ? cycle_time_sum_ms / cycle_count : 0;
    float current_sps = weight_sensor ? weight_sensor->get_current_sps() : 0.0f;
    int current_sample_count = weight_sensor ? weight_sensor->get_sample_count() : 0;
    int32_t raw_reading = weight_sensor ? weight_sensor->get_raw_adc_instant() : 0;
    
    BLE_LOG("[%lums WEIGHT_SAMPLING_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Weight: %.3fg | Raw: %ld | SPS: %.1f | Samples: %d | Build: #%d\n",
           millis(), cycle_count, avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms,
           weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f,
           (long)raw_reading, current_sps, current_sample_count, BUILD_NUMBER);
#endif
}

void WeightSamplingTask::reset_performance_metrics() {
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
}

float WeightSamplingTask::get_current_sps() const {
    return weight_sensor ? weight_sensor->get_current_sps() : 0.0f;
}

void WeightSamplingTask::print_performance_stats() const {
    BLE_LOG("=== WeightSamplingTask Performance ===\n");
    BLE_LOG("Task running: %s\n", task_running ? "YES" : "NO");
    BLE_LOG("Hardware initialized: %s\n", hardware_initialized ? "YES" : "NO");
    BLE_LOG("Hardware validation passed: %s\n", hardware_validation_passed ? "YES" : "NO");
    BLE_LOG("Current SPS: %.1f\n", get_current_sps());
    BLE_LOG("Cycle count: %lu\n", cycle_count);
    
    if (cycle_count > 0) {
        uint32_t avg_cycle_time = cycle_time_sum_ms / cycle_count;
        BLE_LOG("Average cycle time: %lums (%lu-%lums)\n", avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms);
    }
    BLE_LOG("====================================\n");
}

void WeightSamplingTask::handle_hardware_error() {
    BLE_LOG("WeightSamplingTask: Hardware error detected\n");
    
    // Attempt hardware recovery
    if (attempt_hardware_recovery()) {
        BLE_LOG("WeightSamplingTask: Hardware recovery successful\n");
    } else {
        BLE_LOG("ERROR: WeightSamplingTask: Hardware recovery failed\n");
        // Could implement more sophisticated error handling here
    }
}

bool WeightSamplingTask::attempt_hardware_recovery() {
    if (!weight_sensor) return false;
    
    BLE_LOG("WeightSamplingTask: Attempting hardware recovery...\n");
    
    // Reset hardware state flags
    hardware_initialized = false;
    hardware_validation_passed = false;
    
    // Attempt to reinitialize hardware
    return initialize_hx711_hardware();
}
