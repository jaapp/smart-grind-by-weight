#pragma once
#include "arduino_compat.h"

struct TaskPerformance {
    unsigned long min_interval = ULONG_MAX;
    unsigned long max_interval = 0;
    unsigned long total_interval_time = 0;
    unsigned long sample_count = 0;
    const char* task_name;
    unsigned long expected_interval;
    
    // Runtime tracking
    unsigned long min_runtime = ULONG_MAX;
    unsigned long max_runtime = 0;
    unsigned long total_runtime = 0;
    
    TaskPerformance(const char* name, unsigned long expected) 
        : task_name(name), expected_interval(expected) {}
    
    void record_interval(unsigned long actual_interval) {
        if (actual_interval < min_interval) min_interval = actual_interval;
        if (actual_interval > max_interval) max_interval = actual_interval;
        total_interval_time += actual_interval;
        sample_count++;
    }
    
    void record_runtime(unsigned long runtime) {
        if (runtime < min_runtime) min_runtime = runtime;
        if (runtime > max_runtime) max_runtime = runtime;
        total_runtime += runtime;
    }
    
    unsigned long get_average_interval() const {
        return sample_count > 0 ? total_interval_time / sample_count : 0;
    }
    
    unsigned long get_average_runtime() const {
        return sample_count > 0 ? total_runtime / sample_count : 0;
    }
    
    void reset() {
        min_interval = ULONG_MAX;
        max_interval = 0;
        total_interval_time = 0;
        sample_count = 0;
        min_runtime = ULONG_MAX;
        max_runtime = 0;
        total_runtime = 0;
    }
};

class PerformanceMonitor {
private:
    TaskPerformance* tasks[6];
    int task_count = 0;
    
public:
    PerformanceMonitor();
    
    void register_task(const char* name, unsigned long expected_interval);
    void record_task_interval(const char* name, unsigned long actual_interval);
    void record_task_runtime(const char* name, unsigned long runtime);
    void update_expected_interval(const char* name, unsigned long new_expected_interval);
    void print_statistics();
    void reset_statistics();
    
private:
    TaskPerformance* find_task(const char* name);
};

extern PerformanceMonitor performance_monitor;