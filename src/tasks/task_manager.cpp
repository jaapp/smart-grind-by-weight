#include "task_manager.h"
#include "weight_sampling_task.h"
#include "grind_control_task.h"
#include "file_io_task.h"
#include "../hardware/hardware_manager.h"
#include "../system/state_machine.h"
#include "../controllers/profile_controller.h"
#include "../controllers/grind_controller.h"
#include "../bluetooth/manager.h"
#include "../ui/ui_manager.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../logging/grind_logging.h"
#include "../config/constants.h"
#include <esp_task_wdt.h>
#include <Arduino.h>

// Global instance
TaskManager task_manager;

// Static instance pointer for callbacks
TaskManager* TaskManager::instance = nullptr;

TaskManager::TaskManager() {
    memset(&task_handles, 0, sizeof(TaskHandles));
    memset(&task_queues, 0, sizeof(TaskQueues));
    
    hardware_manager = nullptr;
    state_machine = nullptr;
    profile_controller = nullptr;
    grind_controller = nullptr;
    bluetooth_manager = nullptr;
    ui_manager = nullptr;
    
    tasks_initialized = false;
    ota_suspended = false;
    instance = this;
}

TaskManager::~TaskManager() {
    delete_all_tasks();
    cleanup_queues();
    
    if (instance == this) {
        instance = nullptr;
    }
}

bool TaskManager::init(HardwareManager* hw_mgr, StateMachine* sm, ProfileController* pc,
                      GrindController* gc, BluetoothManager* bluetooth, UIManager* ui) {
    hardware_manager = hw_mgr;
    state_machine = sm;
    profile_controller = pc;
    grind_controller = gc;
    bluetooth_manager = bluetooth;
    ui_manager = ui;
    
    BLE_LOG("TaskManager: Initializing FreeRTOS task architecture...\n");
    
    // Validate hardware is ready
    if (!validate_hardware_ready()) {
        BLE_LOG("ERROR: Hardware not ready for task initialization\n");
        return false;
    }
    
    // Create inter-task communication queues
    if (!create_inter_task_queues()) {
        BLE_LOG("ERROR: Failed to create inter-task communication queues\n");
        return false;
    }
    
    // Create all FreeRTOS tasks
    if (!create_all_tasks()) {
        BLE_LOG("ERROR: Failed to create FreeRTOS tasks\n");
        cleanup_queues();
        return false;
    }
    
    tasks_initialized = true;
    BLE_LOG("TaskManager: All tasks created successfully\n");
    
    return true;
}

bool TaskManager::create_inter_task_queues() {
    // Touch to UI queue
    
    // UI to Grind queue  
    task_queues.ui_to_grind_queue = xQueueCreate(SYS_QUEUE_UI_TO_GRIND_SIZE, sizeof(void*)); // Generic pointer for UI events
    if (!task_queues.ui_to_grind_queue) {
        BLE_LOG("ERROR: Failed to create ui_to_grind_queue\n");
        return false;
    }
    
    // File I/O queue
    task_queues.file_io_queue = xQueueCreate(SYS_QUEUE_FILE_IO_SIZE, sizeof(FileIORequest));
    if (!task_queues.file_io_queue) {
        BLE_LOG("ERROR: Failed to create file_io_queue\n");
        return false;
    }
    
    BLE_LOG("TaskManager: Inter-task communication queues created successfully\n");
    return true;
}

void TaskManager::cleanup_queues() {
    
    if (task_queues.ui_to_grind_queue) {
        vQueueDelete(task_queues.ui_to_grind_queue);
        task_queues.ui_to_grind_queue = nullptr;
    }
    
    if (task_queues.file_io_queue) {
        vQueueDelete(task_queues.file_io_queue);
        task_queues.file_io_queue = nullptr;
    }
}

bool TaskManager::create_all_tasks() {
    // Create tasks in order of priority (highest to lowest)
    
    if (!create_weight_sampling_task()) {
        BLE_LOG("ERROR: Failed to create weight sampling task\n");
        return false;
    }
    
    if (!create_grind_control_task()) {
        BLE_LOG("ERROR: Failed to create grind control task\n");
        return false;
    }
    
    if (!create_ui_render_task()) {
        BLE_LOG("ERROR: Failed to create UI render task\n");
        return false;
    }
    
    
    if (!create_bluetooth_task()) {
        BLE_LOG("ERROR: Failed to create bluetooth task\n");
        return false;
    }
    
    if (!create_file_io_task()) {
        BLE_LOG("ERROR: Failed to create file I/O task\n");
        return false;
    }
    
    return true;
}

bool TaskManager::create_weight_sampling_task() {
    BaseType_t result = xTaskCreatePinnedToCore(
        weight_sampling_task_wrapper,
        "WeightSampling",
        SYS_TASK_WEIGHT_SAMPLING_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_WEIGHT_SAMPLING,
        &task_handles.weight_sampling_task,
        0  // Pin to Core 0
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create weight sampling task\n");
        return false;
    }
    
    BLE_LOG("✅ Weight Sampling Task created (Core 0, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_WEIGHT_SAMPLING, 1000 / SYS_TASK_WEIGHT_SAMPLING_INTERVAL_MS);
    return true;
}

bool TaskManager::create_grind_control_task() {
    BaseType_t result = xTaskCreatePinnedToCore(
        grind_control_task_wrapper,
        "GrindControl",
        SYS_TASK_GRIND_CONTROL_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_GRIND_CONTROL,
        &task_handles.grind_control_task,
        0  // Pin to Core 0
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create grind control task\n");
        return false;
    }
    
    BLE_LOG("✅ Grind Control Task created (Core 0, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_GRIND_CONTROL, 1000 / SYS_TASK_GRIND_CONTROL_INTERVAL_MS);
    return true;
}

bool TaskManager::create_ui_render_task() {
    BaseType_t result = xTaskCreatePinnedToCore(
        ui_render_task_wrapper,
        "UIRender",
        SYS_TASK_UI_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_UI,
        &task_handles.ui_render_task,
        1  // Pin to Core 1
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create UI render task\n");
        return false;
    }
    
    BLE_LOG("✅ UI Render Task created (Core 1, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_UI, 1000 / SYS_TASK_UI_INTERVAL_MS);
    return true;
}


bool TaskManager::create_bluetooth_task() {
    BaseType_t result = xTaskCreatePinnedToCore(
        bluetooth_task_wrapper,
        "Bluetooth",
        SYS_TASK_BLUETOOTH_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_BLUETOOTH,
        &task_handles.bluetooth_task,
        1  // Pin to Core 1
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create bluetooth task\n");
        return false;
    }
    
    BLE_LOG("✅ Bluetooth Task created (Core 1, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_BLUETOOTH, 1000 / SYS_TASK_BLUETOOTH_INTERVAL_MS);
    return true;
}

bool TaskManager::create_file_io_task() {
    BaseType_t result = xTaskCreatePinnedToCore(
        file_io_task_wrapper,
        "FileIO",
        SYS_TASK_FILE_IO_STACK_SIZE,
        nullptr,
        SYS_TASK_PRIORITY_FILE_IO,
        &task_handles.file_io_task,
        1  // Pin to Core 1
    );
    
    if (result != pdPASS) {
        BLE_LOG("ERROR: Failed to create file I/O task\n");
        return false;
    }
    
    BLE_LOG("✅ File I/O Task created (Core 1, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_FILE_IO, 1000 / SYS_TASK_FILE_IO_INTERVAL_MS);
    return true;
}

void TaskManager::suspend_hardware_tasks() {
    if (ota_suspended) return;
    
    BLE_LOG("TaskManager: Suspending hardware tasks for OTA operations\n");
    
    if (task_handles.weight_sampling_task) {
        vTaskSuspend(task_handles.weight_sampling_task);
    }
    
    if (task_handles.grind_control_task) {
        vTaskSuspend(task_handles.grind_control_task);
    }
    
    ota_suspended = true;
}

void TaskManager::resume_hardware_tasks() {
    if (!ota_suspended) return;
    
    BLE_LOG("TaskManager: Resuming hardware tasks after OTA operations\n");
    
    if (task_handles.weight_sampling_task) {
        vTaskResume(task_handles.weight_sampling_task);
    }
    
    if (task_handles.grind_control_task) {
        vTaskResume(task_handles.grind_control_task);
    }
    
    ota_suspended = false;
}

void TaskManager::delete_all_tasks() {
    if (task_handles.weight_sampling_task) {
        vTaskDelete(task_handles.weight_sampling_task);
        task_handles.weight_sampling_task = nullptr;
    }
    
    if (task_handles.grind_control_task) {
        vTaskDelete(task_handles.grind_control_task);
        task_handles.grind_control_task = nullptr;
    }
    
    if (task_handles.ui_render_task) {
        vTaskDelete(task_handles.ui_render_task);
        task_handles.ui_render_task = nullptr;
    }
    
    
    if (task_handles.bluetooth_task) {
        vTaskDelete(task_handles.bluetooth_task);
        task_handles.bluetooth_task = nullptr;
    }
    
    if (task_handles.file_io_task) {
        vTaskDelete(task_handles.file_io_task);
        task_handles.file_io_task = nullptr;
    }
    
    tasks_initialized = false;
}

bool TaskManager::validate_hardware_ready() const {
    bool hardware_ready = (hardware_manager != nullptr &&
                          state_machine != nullptr &&
                          profile_controller != nullptr &&
                          grind_controller != nullptr &&
                          bluetooth_manager != nullptr &&
                          ui_manager != nullptr);
    
    if (!hardware_ready) {
        BLE_LOG("TaskManager validation: Hardware interfaces not ready\n");
        return false;
    }
    
    // Validate that critical task modules have their dependencies initialized
    // This prevents tasks from starting with nullptr dependencies
    extern WeightSamplingTask weight_sampling_task;
    extern GrindControlTask grind_control_task;
    
    bool weight_task_ready = weight_sampling_task.validate_hardware_ready();
    bool grind_task_ready = grind_control_task.validate_hardware_ready();
    
    if (!weight_task_ready) {
        BLE_LOG("TaskManager validation: WeightSamplingTask dependencies not ready\n");
        return false;
    }
    
    if (!grind_task_ready) {
        BLE_LOG("TaskManager validation: GrindControlTask dependencies not ready\n");
        return false;
    }
    
    BLE_LOG("TaskManager validation: All hardware and task dependencies ready\n");
    return true;
}

// Static task wrapper implementations
void TaskManager::weight_sampling_task_wrapper(void* parameter) {
    if (instance) {
        instance->weight_sampling_task_impl();
    }
    vTaskDelete(nullptr);
}

void TaskManager::grind_control_task_wrapper(void* parameter) {
    if (instance) {
        instance->grind_control_task_impl();
    }
    vTaskDelete(nullptr);
}

void TaskManager::ui_render_task_wrapper(void* parameter) {
    if (instance) {
        instance->ui_render_task_impl();
    }
    vTaskDelete(nullptr);
}


void TaskManager::bluetooth_task_wrapper(void* parameter) {
    if (instance) {
        instance->bluetooth_task_impl();
    }
    vTaskDelete(nullptr);
}

void TaskManager::file_io_task_wrapper(void* parameter) {
    if (instance) {
        instance->file_io_task_impl();
    }
    vTaskDelete(nullptr);
}

// Task implementation methods (delegate to dedicated task classes)
void TaskManager::weight_sampling_task_impl() {
    // Delegate to dedicated WeightSamplingTask implementation
    weight_sampling_task.task_impl();
}

void TaskManager::grind_control_task_impl() {
    // Delegate to dedicated GrindControlTask implementation
    grind_control_task.task_impl();
}

void TaskManager::ui_render_task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_UI_INTERVAL_MS);
    
    BLE_LOG("UI Render Task started on Core %d\n", xPortGetCoreID());
    
    while (true) {
        uint32_t start_time = millis();

        // Process queued UI events from Core 0 here to ensure
        // all LVGL interactions happen on the UI task context
        if (grind_controller) {
            grind_controller->process_queued_ui_events();
        }

        // UI rendering separated from touch handling
        // Process touch events from TouchInputTask queue
        // TODO: Process touch events from queue and update UI

        // UI logic and display updates (separated from touch input)
        if (ui_manager) {
            // Drain BLE UI status messages here to keep LVGL single-threaded
            if (bluetooth_manager) {
                char status[64];
                while (bluetooth_manager->dequeue_ui_status(status, sizeof(status))) {
                    ui_manager->update_ota_status(status);
                }
            }
            ui_manager->update();
        }
        
        // LVGL processing and display update - this contains lv_timer_handler()
        if (hardware_manager) {
            hardware_manager->get_display()->update();
        }
        
        uint32_t end_time = millis();
        record_task_timing(2, start_time, end_time); // Task index 2 for UI render
        
        // Use vTaskDelayUntil for predictable timing
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}


void TaskManager::bluetooth_task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_BLUETOOTH_INTERVAL_MS);
    
    BLE_LOG("Bluetooth Task started on Core %d\n", xPortGetCoreID());
    
    while (true) {
        uint32_t start_time = millis();
        
        // Use existing bluetooth manager handle method
        if (bluetooth_manager) {
            bluetooth_manager->handle();
        }
        
        uint32_t end_time = millis();
        record_task_timing(4, start_time, end_time); // Task index 4 for bluetooth
        
        // Use vTaskDelayUntil for predictable timing
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskManager::file_io_task_impl() {
    // Delegate to dedicated FileIOTask implementation
    file_io_task.task_impl();
}

void TaskManager::record_task_timing(int task_index, uint32_t start_time, uint32_t end_time) {
    if (task_index < 0 || task_index >= 6) return;
    
    TaskMetrics& metrics = task_metrics[task_index];
    uint32_t cycle_duration = end_time - start_time;
    
    metrics.cycle_count++;
    metrics.cycle_time_sum_ms += cycle_duration;
    
    if (cycle_duration < metrics.cycle_time_min_ms) {
        metrics.cycle_time_min_ms = cycle_duration;
    }
    if (cycle_duration > metrics.cycle_time_max_ms) {
        metrics.cycle_time_max_ms = cycle_duration;
    }
    
#if ENABLE_REALTIME_HEARTBEAT
    // Print task heartbeat every 10 seconds
    if (end_time - metrics.last_heartbeat_time >= REALTIME_HEARTBEAT_INTERVAL_MS) {
        const char* task_names[] = {"WeightSampling", "GrindControl", "UIRender", "Bluetooth", "FileIO"};
        print_task_heartbeat(task_index, task_names[task_index]);
        
        // Reset metrics
        metrics.cycle_count = 0;
        metrics.cycle_time_sum_ms = 0;
        metrics.cycle_time_min_ms = UINT32_MAX;
        metrics.cycle_time_max_ms = 0;
        metrics.last_heartbeat_time = end_time;
    }
#endif
}

void TaskManager::print_task_heartbeat(int task_index, const char* task_name) const {
#if ENABLE_REALTIME_HEARTBEAT
    const TaskMetrics& metrics = task_metrics[task_index];
    uint32_t avg_cycle_time = metrics.cycle_count > 0 ? metrics.cycle_time_sum_ms / metrics.cycle_count : 0;
    
    BLE_LOG("[%lums TASK_HEARTBEAT_%s] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Build: #%d\n",
           millis(), task_name, metrics.cycle_count, avg_cycle_time, 
           metrics.cycle_time_min_ms, metrics.cycle_time_max_ms, BUILD_NUMBER);
#endif
}

bool TaskManager::are_tasks_healthy() const {
    return tasks_initialized && 
           task_handles.weight_sampling_task && 
           task_handles.grind_control_task &&
           task_handles.ui_render_task &&
           task_handles.bluetooth_task &&
           task_handles.file_io_task;
}

void TaskManager::print_task_status() const {
    BLE_LOG("=== TaskManager Status ===\n");
    BLE_LOG("Tasks initialized: %s\n", tasks_initialized ? "YES" : "NO");
    BLE_LOG("OTA suspended: %s\n", ota_suspended ? "YES" : "NO");
    BLE_LOG("Task handles:\n");
    BLE_LOG("  WeightSampling: %s\n", task_handles.weight_sampling_task ? "RUNNING" : "NULL");
    BLE_LOG("  GrindControl: %s\n", task_handles.grind_control_task ? "RUNNING" : "NULL");
    BLE_LOG("  UIRender: %s\n", task_handles.ui_render_task ? "RUNNING" : "NULL");
    BLE_LOG("  Bluetooth: %s\n", task_handles.bluetooth_task ? "RUNNING" : "NULL");
    BLE_LOG("  FileIO: %s\n", task_handles.file_io_task ? "RUNNING" : "NULL");
    BLE_LOG("========================\n");
}
