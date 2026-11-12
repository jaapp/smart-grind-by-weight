#pragma once

#include <cstdint>

class UIManager;

// Implements automatic screen dimming based on touch/weight activity

class ScreenTimeoutController {
public:
    explicit ScreenTimeoutController(UIManager* manager);

    void register_events();
    bool update(); // True only on an idle dim-to-awake transition.

private:
    UIManager* ui_manager_;
    bool screen_dimmed_;
};
