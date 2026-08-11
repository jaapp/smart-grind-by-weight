#pragma once
#include <cstdint>

class UIManager;
enum class UIState;

// Drives the manual pane: free-running motor with elapsed time and live weight readouts.
// Runs the grinder directly (no GrindController session, no grind logging).

class ManualGrindUIController {
public:
    explicit ManualGrindUIController(UIManager* manager);

    void register_events();
    void update();

    void handle_grind_button();
    void on_enter();
    void on_state_changed(UIState new_state);
    void stop_and_reset();

    bool is_running() const { return running_; }

private:
    void start_run();
    void stop_run();
    void handle_time_tap();
    void handle_weight_tap();
    void refresh_readouts(bool force);

    UIManager* ui_manager_;
    bool running_ = false;
    uint32_t run_start_ms_ = 0;
    uint32_t accumulated_ms_ = 0;
    uint32_t last_readout_update_ms_ = 0;
};
