#include "state_machine.h"
#include <Arduino.h>

void StateMachine::init(UIState initial_state) {
    current_state = initial_state;
    previous_state = initial_state;
}

void StateMachine::transition_to(UIState new_state) {
    if (current_state == new_state) return;
    
    previous_state = current_state;
    current_state = new_state;
}

const char* StateMachine::get_state_name(UIState state) const {
    switch (state) {
        case UIState::READY: return "READY";
        case UIState::GRINDING: return "GRINDING";
        case UIState::GRIND_COMPLETE: return "GRIND_COMPLETE";
        case UIState::GRIND_TIMEOUT: return "GRIND_TIMEOUT";
        case UIState::EDIT: return "EDIT";
        case UIState::MENU: return "MENU";
        case UIState::CALIBRATION: return "CALIBRATION";
        case UIState::CONFIRM: return "CONFIRM";
        case UIState::AUTOTUNING: return "AUTOTUNING";
        case UIState::OTA_UPDATE: return "OTA_UPDATE";
        case UIState::OTA_UPDATE_FAILED: return "OTA_UPDATE_FAILED";
        default: return "UNKNOWN";
    }
}
