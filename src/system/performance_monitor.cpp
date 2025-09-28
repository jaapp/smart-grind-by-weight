#include "performance_monitor.h"
#include "../config/constants.h"

PerformanceMonitor performance_monitor;

PerformanceMonitor::PerformanceMonitor() {
    // Initialize task array
    for (int i = 0; i < 6; i++) {
        tasks[i] = nullptr;
    }
}

void PerformanceMonitor::register_task(const char* name, unsigned long expected_interval) {
    if (task_count < 6) {
        tasks[task_count] = new TaskPerformance(name, expected_interval);
        task_count++;
    }
}

void PerformanceMonitor::record_task_interval(const char* name, unsigned long actual_interval) {
    TaskPerformance* task = find_task(name);
    if (task) {
        task->record_interval(actual_interval);
    }
}

void PerformanceMonitor::record_task_runtime(const char* name, unsigned long runtime) {
    TaskPerformance* task = find_task(name);
    if (task) {
        task->record_runtime(runtime);
    }
}

void PerformanceMonitor::update_expected_interval(const char* name, unsigned long new_expected_interval) {
    TaskPerformance* task = find_task(name);
    if (task) {
        task->expected_interval = new_expected_interval;
    }
}

void PerformanceMonitor::print_statistics() {
    // Check if we have any data to report
    bool has_data = false;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i] && tasks[i]->sample_count > 0) {
            has_data = true;
            break;
        }
    }
    
    if (!has_data) return; // Don't print if no performance data collected
    
    LOG_BLE("⚡ PERFORMANCE REPORT:\n");
    
    bool system_healthy = true;
    for (int i = 0; i < task_count; i++) {
        TaskPerformance* task = tasks[i];
        if (task && task->sample_count > 0) {
            unsigned long avg_interval = task->get_average_interval();
            unsigned long avg_runtime = task->get_average_runtime();
            float avg_deviation = ((float)(avg_interval - task->expected_interval) * 100.0f) / task->expected_interval;
            
            // Performance rating
            const char* status = "✓";
            if (avg_deviation > 50) {
                status = "✗";
                system_healthy = false;
            } else if (avg_deviation > 25) {
                status = "~";
            }
            
            LOG_BLE("  %s %s: req=%lums avg=%lums(%+.0f%%) max=%lums runtime=%lums", 
                         status, task->task_name, task->expected_interval, avg_interval, avg_deviation, task->max_interval, avg_runtime);
            
            // Runtime warning if task takes >50% of its interval
            if (avg_runtime > task->expected_interval / 2) {
                LOG_BLE(" CPU-HOG");
            }
            
            LOG_BLE("\n");
        }
    }
    
    LOG_BLE("⚡ SYSTEM: %s\n", system_healthy ? "OK" : "STRESSED");
}

void PerformanceMonitor::reset_statistics() {
    for (int i = 0; i < task_count; i++) {
        if (tasks[i]) {
            tasks[i]->reset();
        }
    }
}

TaskPerformance* PerformanceMonitor::find_task(const char* name) {
    for (int i = 0; i < task_count; i++) {
        if (tasks[i] && strcmp(tasks[i]->task_name, name) == 0) {
            return tasks[i];
        }
    }
    return nullptr;
}