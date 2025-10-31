#pragma once

enum class UIState {
    READY,
    GRINDING,
    GRIND_COMPLETE,
    GRIND_TIMEOUT,
    EDIT,
    MENU,
    CALIBRATION,
    CONFIRM,
    PURGE_CONFIRM,
    AUTOTUNING,
    OTA_UPDATE,
    OTA_UPDATE_FAILED
};

class StateMachine {
private:
    UIState current_state;
    UIState previous_state;

public:
    void init(UIState initial_state = UIState::READY);
    void transition_to(UIState new_state);
    
    UIState get_current_state() const { return current_state; }
    UIState get_previous_state() const { return previous_state; }
    bool is_state(UIState state) const { return current_state == state; }
    const char* get_state_name(UIState state) const;
};
