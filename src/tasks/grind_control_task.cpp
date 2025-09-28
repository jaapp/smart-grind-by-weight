#include "grind_control_task.h"
#include "../controllers/grind_controller.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../logging/grind_logging.h"
#include "../config/constants.h"
#include <Arduino.h>
#include <esp_task_wdt.h>

// Global instance
GrindControlTask grind_control_task;

// Static instance pointer for task callback
GrindControlTask* GrindControlTask::instance = nullptr;

GrindControlTask::GrindControlTask() {
    grind_controller = nullptr;
    weight_sensor = nullptr;
    grinder = nullptr;
    logger = nullptr;
    
    task_handle = nullptr;
    task_running = false;
    
    // Initialize performance metrics
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
    last_heartbeat_time = 0;
    
    // Initialize grind state
    grind_active = false;
    grind_start_time = 0;
    last_grind_update_time = 0;
    
    instance = this;
}

GrindControlTask::~GrindControlTask() {
    stop_task();
    
    if (instance == this) {
        instance = nullptr;
    }
}

void GrindControlTask::init(GrindController* gc, WeightSensor* ws, Grinder* gr, GrindLogger* log) {
    grind_controller = gc;
    weight_sensor = ws;
    grinder = gr;
    logger = log;
    
    BLE_LOG("GrindControlTask: Initialized with hardware interfaces\n");
}

bool GrindControlTask::start_task() {
    if (task_running) {
        BLE_LOG("WARNING: GrindControlTask already running\n");
        return false;
    }
    
    BLE_LOG("GrindControlTask: Validating hardware interfaces...\n");
    if (!validate_hardware_ready()) {
        BLE_LOG("ERROR: Hardware not ready for grind control task\n");
        return false;
    }
    
    BLE_LOG("GrindControlTask: Starting task on Core 0...\n");
    task_running = true;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        task_wrapper,
        "GrindControl",
        SYS_TASK_GRIND_CONTROL_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_GRIND_CONTROL,
        &task_handle,
        0  // Pin to Core 0
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create GrindControlTask!\n");
        task_running = false;
        task_handle = nullptr;
        return false;
    }
    
    BLE_LOG("✅ GrindControlTask created successfully (Core 0, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_GRIND_CONTROL, 1000 / SYS_TASK_GRIND_CONTROL_INTERVAL_MS);
    return true;
}

void GrindControlTask::stop_task() {
    if (!task_running) {
        return;
    }
    
    BLE_LOG("GrindControlTask: Stopping task...\n");
    task_running = false;
    
    // Wait for task to complete (with timeout)
    if (task_handle) {
        uint32_t timeout_start = millis();
        while (eTaskGetState(task_handle) != eDeleted && millis() - timeout_start < 1000) {
            delay(10);
        }
        task_handle = nullptr;
    }
    
    BLE_LOG("GrindControlTask: Task stopped\n");
}

void GrindControlTask::task_wrapper(void* parameter) {
    if (instance) {
        instance->task_impl();
    }
    vTaskDelete(nullptr);
}

void GrindControlTask::task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_GRIND_CONTROL_INTERVAL_MS);
    
    BLE_LOG("GrindControlTask started on Core %d at %dHz\n", 
            xPortGetCoreID(), 1000 / SYS_TASK_GRIND_CONTROL_INTERVAL_MS);
    
    // When invoked via TaskManager wrapper, start_task() isn't used.
    // Ensure the internal run flag is set so the loop executes.
    task_running = true;

    // Add current task to watchdog monitoring
    esp_task_wdt_add(nullptr);
    
    // Reset performance metrics
    reset_performance_metrics();
    
    // Main grind control loop
    while (task_running) {
        uint32_t cycle_start_time = millis();
        
        // Update grind control logic
        update_grind_control();
        
        // Monitor grind state changes
        monitor_grind_state();
        
        // Feed watchdog to prevent timeout
        esp_task_wdt_reset();
        
        uint32_t cycle_end_time = millis();
        
        // Record performance metrics
        record_timing(cycle_start_time, cycle_end_time);
        
        // Use vTaskDelayUntil for predictable timing (eliminates busy-wait)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    
    // Remove task from watchdog monitoring before exit
    task_running = false;
    esp_task_wdt_delete(nullptr);
    
    BLE_LOG("GrindControlTask: Control loop stopped\n");
}

void GrindControlTask::update_grind_control() {
    if (!grind_controller) return;
    
    // Execute main grind controller logic
    // This calls the existing GrindController::update() method which contains
    // all the grinding algorithms, state machine, and pulse control logic
    grind_controller->update();
    
    // Update timing for performance tracking
    last_grind_update_time = millis();
}

void GrindControlTask::monitor_grind_state() {
    if (!grind_controller) return;
    
    bool current_grind_active = grind_controller->is_active();
    
    // Detect grind start
    if (!grind_active && current_grind_active) {
        grind_active = true;
        grind_start_time = millis();
        BLE_LOG("GrindControlTask: Grind session started\n");
    }
    // Detect grind end
    else if (grind_active && !current_grind_active) {
        grind_active = false;
        uint32_t grind_duration = millis() - grind_start_time;
        BLE_LOG("GrindControlTask: Grind session ended (duration: %lums)\n", grind_duration);
    }
}

bool GrindControlTask::validate_hardware_ready() const {
    bool grind_controller_ready = (grind_controller != nullptr);
    bool weight_sensor_ready = (weight_sensor != nullptr && weight_sensor->is_initialized());
    bool grinder_ready = (grinder != nullptr && grinder->is_initialized());
    bool logger_ready = (logger != nullptr);
    
    BLE_LOG("GrindControlTask hardware validation:\n");
    BLE_LOG("  grind_controller != nullptr: %s\n", grind_controller_ready ? "YES" : "NO");
    BLE_LOG("  weight_sensor ready: %s\n", weight_sensor_ready ? "YES" : "NO");
    BLE_LOG("  grinder ready: %s\n", grinder_ready ? "YES" : "NO");
    BLE_LOG("  logger != nullptr: %s\n", logger_ready ? "YES" : "NO");
    
    return grind_controller_ready && weight_sensor_ready && grinder_ready && logger_ready;
}

uint32_t GrindControlTask::get_grind_duration_ms() const {
    if (!grind_active || grind_start_time == 0) {
        return 0;
    }
    return millis() - grind_start_time;
}

void GrindControlTask::record_timing(uint32_t start_time, uint32_t end_time) {
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

void GrindControlTask::print_heartbeat() const {
#if ENABLE_REALTIME_HEARTBEAT
    uint32_t avg_cycle_time = cycle_count > 0 ? cycle_time_sum_ms / cycle_count : 0;
    float target_weight = grind_controller ? grind_controller->get_target_weight() : 0.0f;
    float current_weight = weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f;
    const char* grind_status = grind_active ? "ACTIVE" : "IDLE";
    
    BLE_LOG("[%lums GRIND_CONTROL_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Status: %s | Target: %.1fg | Current: %.3fg | Build: #%d\n",
           millis(), cycle_count, avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms,
           grind_status, target_weight, current_weight, BUILD_NUMBER);
#endif
}

void GrindControlTask::reset_performance_metrics() {
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
}

void GrindControlTask::print_performance_stats() const {
    BLE_LOG("=== GrindControlTask Performance ===\n");
    BLE_LOG("Task running: %s\n", task_running ? "YES" : "NO");
    BLE_LOG("Grind active: %s\n", grind_active ? "YES" : "NO");
    BLE_LOG("Cycle count: %lu\n", cycle_count);
    
    if (cycle_count > 0) {
        uint32_t avg_cycle_time = cycle_time_sum_ms / cycle_count;
        BLE_LOG("Average cycle time: %lums (%lu-%lums)\n", avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms);
    }
    
    if (grind_active) {
        BLE_LOG("Current grind duration: %lums\n", get_grind_duration_ms());
    }
    
    BLE_LOG("Last grind update: %lums ago\n", millis() - last_grind_update_time);
    BLE_LOG("===================================\n");
}

void GrindControlTask::handle_grind_error() {
    BLE_LOG("GrindControlTask: Grind error detected\n");
    
    // Stop any active grinding
    if (grinder && grinder->is_grinding()) {
        grinder->stop();
        BLE_LOG("GrindControlTask: Emergency grinder stop executed\n");
    }
    
    // Reset grind state
    grind_active = false;
    grind_start_time = 0;
    
    // Could implement more sophisticated error handling here
    // such as notifying the UI or attempting recovery procedures
}
