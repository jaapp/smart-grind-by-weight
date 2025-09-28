#include "task_scheduler.h"
#include "../config/constants.h"

TaskScheduler::TaskScheduler() {
    tasks.reserve(8); // Reserve space for typical number of tasks
}

void TaskScheduler::register_task(const char* name, unsigned long interval_ms, std::function<void()> callback) {
    tasks.emplace_back(name, interval_ms, callback);
}

void TaskScheduler::enable_task(const char* name, bool enabled) {
    Task* task = find_task(name);
    if (task) {
        task->enabled = enabled;
    }
}

void TaskScheduler::set_task_interval(const char* name, unsigned long interval_ms) {
    Task* task = find_task(name);
    if (task) {
        task->interval_ms = interval_ms;
    }
}

void TaskScheduler::run() {
    unsigned long now = millis();
    
    // Run tasks serially - maintains thread safety
    for (auto& task : tasks) {
        if (task.enabled && (now - task.last_run >= task.interval_ms)) {
            task.callback();
            task.last_run = now;
        }
    }
}

unsigned long TaskScheduler::get_next_wake_time() {
    unsigned long now = millis();
    unsigned long next_wake = ULONG_MAX;
    
    for (const auto& task : tasks) {
        if (task.enabled) {
            unsigned long time_since_last = now - task.last_run;
            if (time_since_last >= task.interval_ms) {
                return 0; // Task is already due
            }
            
            unsigned long time_until_next = task.interval_ms - time_since_last;
            if (time_until_next < next_wake) {
                next_wake = time_until_next;
            }
        }
    }
    
    return (next_wake == ULONG_MAX) ? 1000 : next_wake; // Default to 1s if no tasks
}

void TaskScheduler::print_task_status() {
    BLE_LOG("=== Task Scheduler Status ===\n");
    unsigned long now = millis();
    
    for (const auto& task : tasks) {
        unsigned long time_since_last = now - task.last_run;
        BLE_LOG("Task: %-15s | Interval: %4lums | Last: %6lums ago | %s\n",
                     task.name, task.interval_ms, time_since_last,
                     task.enabled ? "ENABLED" : "DISABLED");
    }
    BLE_LOG("=============================\n");
}

void TaskScheduler::suspend_hardware_tasks() {
    enable_task("hardware", false);
    enable_task("grind_control", false);
    BLE_LOG("TaskScheduler: Hardware I2C tasks suspended for OTA\n");
}

void TaskScheduler::resume_hardware_tasks() {
    enable_task("hardware", true);
    enable_task("grind_control", true);
    BLE_LOG("TaskScheduler: Hardware I2C tasks resumed after OTA\n");
}

TaskScheduler::Task* TaskScheduler::find_task(const char* name) {
    for (auto& task : tasks) {
        if (strcmp(task.name, name) == 0) {
            return &task;
        }
    }
    return nullptr;
}