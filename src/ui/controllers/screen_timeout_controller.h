#pragma once

#include <cstdint>
#include <memory>
#include "../components/screensaver.h"

class UIManager;

// Implements automatic screen dimming and the idle screensaver overlay,
// based on touch/weight activity

class ScreenTimeoutController {
public:
    explicit ScreenTimeoutController(UIManager* manager);

    void register_events();
    void update();
    // Dims and shows the screensaver immediately, holding it until a tap
    // dismisses it (used by the settings Preview button and the ready-screen
    // vertical swipe)
    void start_screensaver_now();

private:
    UIManager* ui_manager_;
    bool screen_dimmed_;
    bool held_until_touch_ = false;
    std::unique_ptr<ScreensaverOverlay> screensaver_;
};
