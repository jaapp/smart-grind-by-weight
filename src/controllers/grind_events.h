#pragma once
#include "grind_controller.h"

// Event types that GrindController can emit to UIManager
enum class UIGrindEvent {
    PHASE_CHANGED,    // Tare started/completed, grinding phases changed
    PROGRESS_UPDATED, // Weight/progress changes during grinding
    COMPLETED,        // Grind completed successfully
    TIMEOUT,          // Grind timed out with phase info
    STOPPED,          // Grind stopped by user or error
    BACKGROUND_CHANGE,// Background color change for grinder activity indication
    PULSE_AVAILABLE,  // Time mode completion - pulses can be requested
    PULSE_STARTED,    // Additional pulse started
    PULSE_COMPLETED   // Additional pulse finished, weight updated
};

// Data payload for grind events
struct GrindEventData {
    UIGrindEvent event;
    GrindPhase phase;
    GrindMode mode;
    float current_weight;
    int progress_percent;
    const char* phase_display_text;
    bool show_taring_text;
    float flow_rate;              // For PROGRESS_UPDATED event
    
    // Additional data for specific events
    float final_weight;           // For COMPLETED event
    const char* error_message;    // For TIMEOUT/ERROR event
    float error_weight;           // For TIMEOUT/ERROR event
    int error_progress;           // For TIMEOUT/ERROR event
    bool background_active;       // For BACKGROUND_CHANGE event
    
    // Pulse-specific data
    int pulse_count;              // Number of additional pulses performed
    uint32_t pulse_duration_ms;   // Duration of current/last pulse
    bool can_pulse;               // Whether additional pulses are allowed
};
