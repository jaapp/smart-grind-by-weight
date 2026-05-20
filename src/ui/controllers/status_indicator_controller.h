#pragma once
#include <lvgl.h>

class UIManager;

// Shows connectivity status icon with color coding
// Shows diagnostic warning icon when issues detected

class StatusIndicatorController {
public:
    explicit StatusIndicatorController(UIManager* manager);

    void build();
    void update();

private:
    void update_connectivity_status_icon();
    void update_warning_icon();

    UIManager* ui_manager_;
    lv_obj_t* connectivity_status_icon_ = nullptr;
    lv_obj_t* warning_icon_ = nullptr;
};
