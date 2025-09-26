#pragma once
#include "grind_controller.h"

// Event types that GrindController can emit to UIManager
enum class UIGrindEvent {
    PHASE_CHANGED,    // Tare started/completed, grinding phases changed
    PROGRESS_UPDATED, // Weight/progress changes during grinding
    COMPLETED,        // Grind completed successfully
    TIMEOUT,          // Grind timed out with phase info
    STOPPED,          // Grind stopped by user or error
    BACKGROUND_CHANGE // Background color change for grinder activity indication
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
};
