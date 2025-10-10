#pragma once
#include <lvgl.h>

class UIManager;

// Handles the auto-tune workflow and UI state transitions
class AutoTuneUIController {
public:
    explicit AutoTuneUIController(UIManager* manager);

    void register_events();
    void update();

    void start_autotune();
    void handle_cancel();
    void handle_ok();

private:
    UIManager* ui_manager_;
    bool autotune_started_;
};
