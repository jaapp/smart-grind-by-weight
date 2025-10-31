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
    ota_watchdog_task = nullptr;
    ota_watchdog_active = false;
    ota_watchdog_ble_registered = false;
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
    
    LOG_BLE("TaskManager: Initializing FreeRTOS task architecture...\n");
    
    // Validate hardware is ready
    if (!validate_hardware_ready()) {
        LOG_BLE("ERROR: Hardware not ready for task initialization\n");
        return false;
    }
    
    // Create inter-task communication queues
    if (!create_inter_task_queues()) {
        LOG_BLE("ERROR: Failed to create inter-task communication queues\n");
        return false;
    }
    
    // Create all FreeRTOS tasks
    if (!create_all_tasks()) {
        LOG_BLE("ERROR: Failed to create FreeRTOS tasks\n");
        cleanup_queues();
        return false;
    }
    
    tasks_initialized = true;
    LOG_BLE("TaskManager: All tasks created successfully\n");
    
    return true;
}

bool TaskManager::create_inter_task_queues() {
    // Touch to UI queue
    
    // UI to Grind queue  
    task_queues.ui_to_grind_queue = xQueueCreate(SYS_QUEUE_UI_TO_GRIND_SIZE, sizeof(void*)); // Generic pointer for UI events
    if (!task_queues.ui_to_grind_queue) {
        LOG_BLE("ERROR: Failed to create ui_to_grind_queue\n");
        return false;
    }
    
    // File I/O queue
    task_queues.file_io_queue = xQueueCreate(SYS_QUEUE_FILE_IO_SIZE, sizeof(FileIORequest));
    if (!task_queues.file_io_queue) {
        LOG_BLE("ERROR: Failed to create file_io_queue\n");
        return false;
    }
    
    LOG_BLE("TaskManager: Inter-task communication queues created successfully\n");
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
        LOG_BLE("ERROR: Failed to create weight sampling task\n");
        return false;
    }
    
    if (!create_grind_control_task()) {
        LOG_BLE("ERROR: Failed to create grind control task\n");
        return false;
    }
    
    if (!create_ui_render_task()) {
        LOG_BLE("ERROR: Failed to create UI render task\n");
        return false;
    }
    
    
    if (!create_bluetooth_task()) {
        LOG_BLE("ERROR: Failed to create bluetooth task\n");
        return false;
    }
    
    if (!create_file_io_task()) {
        LOG_BLE("ERROR: Failed to create file I/O task\n");
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
        LOG_BLE("ERROR: Failed to create weight sampling task\n");
        return false;
    }
    
    LOG_BLE("✅ Weight Sampling Task created (Core 0, Priority %d, %dHz)\n", 
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
        LOG_BLE("ERROR: Failed to create grind control task\n");
        return false;
    }
    
    LOG_BLE("✅ Grind Control Task created (Core 0, Priority %d, %dHz)\n", 
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
        LOG_BLE("ERROR: Failed to create UI render task\n");
        return false;
    }
    
    LOG_BLE("✅ UI Render Task created (Core 1, Priority %d, %dHz)\n", 
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
        LOG_BLE("ERROR: Failed to create bluetooth task\n");
        return false;
    }
    
    LOG_BLE("✅ Bluetooth Task created (Core 1, Priority %d, %dHz)\n", 
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
        LOG_BLE("ERROR: Failed to create file I/O task\n");
        return false;
    }
    
    LOG_BLE("✅ File I/O Task created (Core 1, Priority %d, %dHz)\n", 
            SYS_TASK_PRIORITY_FILE_IO, 1000 / SYS_TASK_FILE_IO_INTERVAL_MS);
    return true;
}

void TaskManager::suspend_hardware_tasks() {
    if (ota_suspended) return;
    
    LOG_BLE("TaskManager: Suspending hardware tasks for OTA operations\n");
    
    if (task_handles.weight_sampling_task) {
        esp_err_t err = esp_task_wdt_delete(task_handles.weight_sampling_task);
        if (err != ESP_OK) {
            LOG_BLE("TaskManager: Warning - failed to unsubscribe WeightSampling task from WDT (err=%d)\n", err);
        }
        vTaskSuspend(task_handles.weight_sampling_task);
    }
    
    if (task_handles.grind_control_task) {
        esp_err_t err = esp_task_wdt_delete(task_handles.grind_control_task);
        if (err != ESP_OK) {
            LOG_BLE("TaskManager: Warning - failed to unsubscribe GrindControl task from WDT (err=%d)\n", err);
        }
        vTaskSuspend(task_handles.grind_control_task);
    }

    if (task_handles.file_io_task) {
        vTaskSuspend(task_handles.file_io_task);
    }
    
    enable_ota_watchdog_keepalive();
    ota_suspended = true;
}

void TaskManager::resume_hardware_tasks() {
    if (!ota_suspended) return;
    
    LOG_BLE("TaskManager: Resuming hardware tasks after OTA operations\n");
    
    if (task_handles.weight_sampling_task) {
        esp_err_t err = esp_task_wdt_add(task_handles.weight_sampling_task);
        if (err != ESP_OK) {
            LOG_BLE("TaskManager: Warning - failed to resubscribe WeightSampling task to WDT (err=%d)\n", err);
        }
        vTaskResume(task_handles.weight_sampling_task);
    }
    
    if (task_handles.grind_control_task) {
        esp_err_t err = esp_task_wdt_add(task_handles.grind_control_task);
        if (err != ESP_OK) {
            LOG_BLE("TaskManager: Warning - failed to resubscribe GrindControl task to WDT (err=%d)\n", err);
        }
        vTaskResume(task_handles.grind_control_task);
    }

    if (task_handles.file_io_task) {
        vTaskResume(task_handles.file_io_task);
    }
    
    disable_ota_watchdog_keepalive();
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
        LOG_BLE("TaskManager validation: Hardware interfaces not ready\n");
        return false;
    }
    
    // Validate that critical task modules have their dependencies initialized
    // This prevents tasks from starting with nullptr dependencies
    extern WeightSamplingTask weight_sampling_task;
    extern GrindControlTask grind_control_task;
    
    bool weight_task_ready = weight_sampling_task.validate_hardware_ready();
    bool grind_task_ready = grind_control_task.validate_hardware_ready();
    
    if (!weight_task_ready) {
        LOG_BLE("TaskManager validation: WeightSamplingTask dependencies not ready\n");
        return false;
    }
    
    if (!grind_task_ready) {
        LOG_BLE("TaskManager validation: GrindControlTask dependencies not ready\n");
        return false;
    }
    
    LOG_BLE("TaskManager validation: All hardware and task dependencies ready\n");
    return true;
}

void TaskManager::enable_ota_watchdog_keepalive() {
    if (ota_watchdog_active) return;
    
    ota_watchdog_ble_registered = false;
    ota_watchdog_task = task_handles.bluetooth_task;

    if (ota_watchdog_task) {
        ota_watchdog_active = true;
        esp_err_t err = esp_task_wdt_add(ota_watchdog_task);
        if (err == ESP_OK) {
            ota_watchdog_ble_registered = true;
            LOG_BLE("TaskManager: OTA watchdog keepalive registered for Bluetooth task\n");
        } else if (err == ESP_ERR_INVALID_STATE) {
            ota_watchdog_ble_registered = true;
            LOG_BLE("TaskManager: Bluetooth task already registered with watchdog\n");
        } else if (err != ESP_ERR_INVALID_ARG) {
            LOG_BLE("TaskManager: Failed to register Bluetooth task with watchdog (err=%d)\n", err);
        }
    } else {
        ota_watchdog_active = false;
        LOG_BLE("TaskManager: Bluetooth task handle missing, unable to register watchdog keepalive\n");
    }
}

void TaskManager::disable_ota_watchdog_keepalive() {
    if (!ota_watchdog_active) return;

    if (ota_watchdog_ble_registered && ota_watchdog_task) {
        esp_err_t ble_err = esp_task_wdt_delete(ota_watchdog_task);
        if (ble_err != ESP_OK) {
            LOG_BLE("TaskManager: Failed to unregister Bluetooth task watchdog keepalive (err=%d)\n", ble_err);
        } else {
            LOG_BLE("TaskManager: Bluetooth task watchdog keepalive unregistered\n");
        }
        ota_watchdog_ble_registered = false;
    }

    ota_watchdog_task = nullptr;
    ota_watchdog_active = false;
    ota_watchdog_ble_registered = false;
}

// Static task wrapper implementations
void TaskManager::weight_sampling_task_wrapper(void* parameter) {
    if (instance) {
        instance->weight_sampling_task_impl();
        instance->task_handles.weight_sampling_task = nullptr;
    }
    vTaskDelete(nullptr);
}

void TaskManager::grind_control_task_wrapper(void* parameter) {
    if (instance) {
        instance->grind_control_task_impl();
        instance->task_handles.grind_control_task = nullptr;
    }
    vTaskDelete(nullptr);
}

void TaskManager::ui_render_task_wrapper(void* parameter) {
    if (instance) {
        instance->ui_render_task_impl();
        instance->task_handles.ui_render_task = nullptr;
    }
    vTaskDelete(nullptr);
}


void TaskManager::bluetooth_task_wrapper(void* parameter) {
    if (instance) {
        instance->bluetooth_task_impl();
        instance->task_handles.bluetooth_task = nullptr;
    }
    vTaskDelete(nullptr);
}

void TaskManager::file_io_task_wrapper(void* parameter) {
    if (instance) {
        instance->file_io_task_impl();
        instance->task_handles.file_io_task = nullptr;
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
    
    LOG_BLE("UI Render Task started on Core %d\n", xPortGetCoreID());
    
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
                auto* ota = ui_manager->get_ota_data_export_controller();
                while (ota && bluetooth_manager->dequeue_ui_status(status, sizeof(status))) {
                    ota->update_status(status);
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
    
    LOG_BLE("Bluetooth Task started on Core %d\n", xPortGetCoreID());
    
    while (true) {
        uint32_t start_time = millis();
        
        // Use existing bluetooth manager handle method
        if (bluetooth_manager) {
            bluetooth_manager->handle();
        }
        
        if (ota_watchdog_active) {
            esp_task_wdt_reset();
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
    
#if SYS_ENABLE_REALTIME_HEARTBEAT
    // Print task heartbeat every 10 seconds
    if (end_time - metrics.last_heartbeat_time >= SYS_REALTIME_HEARTBEAT_INTERVAL_MS) {
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
#if SYS_ENABLE_REALTIME_HEARTBEAT
    const TaskMetrics& metrics = task_metrics[task_index];
    uint32_t avg_cycle_time = metrics.cycle_count > 0 ? metrics.cycle_time_sum_ms / metrics.cycle_count : 0;
    
    LOG_BLE("[%lums TASK_HEARTBEAT_%s] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Build: #%d\n",
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
    LOG_BLE("=== TaskManager Status ===\n");
    LOG_BLE("Tasks initialized: %s\n", tasks_initialized ? "YES" : "NO");
    LOG_BLE("OTA suspended: %s\n", ota_suspended ? "YES" : "NO");
    LOG_BLE("Task handles:\n");
    LOG_BLE("  WeightSampling: %s\n", task_handles.weight_sampling_task ? "RUNNING" : "NULL");
    LOG_BLE("  GrindControl: %s\n", task_handles.grind_control_task ? "RUNNING" : "NULL");
    LOG_BLE("  UIRender: %s\n", task_handles.ui_render_task ? "RUNNING" : "NULL");
    LOG_BLE("  Bluetooth: %s\n", task_handles.bluetooth_task ? "RUNNING" : "NULL");
    LOG_BLE("  FileIO: %s\n", task_handles.file_io_task ? "RUNNING" : "NULL");
    LOG_BLE("========================\n");
}
