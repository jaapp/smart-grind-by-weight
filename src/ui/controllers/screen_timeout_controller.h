#pragma once

#include <cstdint>

class UIManager;
class ScreensaverController;

// Implements automatic screen dimming based on touch/weight activity

class ScreenTimeoutController {
public:
    explicit ScreenTimeoutController(UIManager* manager);

    void register_events();
    void update();

    void set_screensaver_controller(ScreensaverController* controller) {
        screensaver_controller_ = controller;
    }

private:
    UIManager* ui_manager_;
    ScreensaverController* screensaver_controller_ = nullptr;
    bool screen_dimmed_;
};
