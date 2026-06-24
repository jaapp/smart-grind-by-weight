#pragma once
#include "arduino_compat.h"
#include <vector>
#include <functional>

class TaskScheduler {
private:
    struct Task {
        const char* name;
        unsigned long interval_ms;
        unsigned long last_run;
        std::function<void()> callback;
        bool enabled;
        
        Task(const char* n, unsigned long interval, std::function<void()> cb) 
            : name(n), interval_ms(interval), last_run(0), callback(cb), enabled(true) {}
    };
    
    std::vector<Task> tasks;
    
public:
    TaskScheduler();
    
    // Register a task with name, interval in ms, and callback function
    void register_task(const char* name, unsigned long interval_ms, std::function<void()> callback);
    
    // Enable/disable a task
    void enable_task(const char* name, bool enabled = true);
    
    // Change task interval at runtime
    void set_task_interval(const char* name, unsigned long interval_ms);
    
    // Run all tasks that are due (maintains serial execution)
    void run();
    
    // Get time until next task needs to run (for micro-sleep optimization)
    unsigned long get_next_wake_time();
    
    // Get task count for debugging
    size_t get_task_count() const { return tasks.size(); }
    
    // Print task status for debugging
    void print_task_status();
    
    // Suspend hardware-related tasks during OTA operations
    void suspend_hardware_tasks();
    
    // Resume hardware-related tasks after OTA operations
    void resume_hardware_tasks();
    
private:
    // Find task by name
    Task* find_task(const char* name);
};