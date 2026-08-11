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
    void start_screensaver_preview();

private:
    UIManager* ui_manager_;
    bool screen_dimmed_;
    bool preview_active_ = false;
    std::unique_ptr<ScreensaverOverlay> screensaver_;
};
