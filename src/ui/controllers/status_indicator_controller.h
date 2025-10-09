#pragma once
#include <lvgl.h>

class UIManager;

// Shows Bluetooth connection status icon with color coding
// Shows diagnostic warning icon when issues detected

class StatusIndicatorController {
public:
    explicit StatusIndicatorController(UIManager* manager);

    void build();
    void update();

private:
    void update_ble_status_icon();
    void update_warning_icon();

    UIManager* ui_manager_;
    lv_obj_t* ble_status_icon_ = nullptr;
    lv_obj_t* warning_icon_ = nullptr;
};
