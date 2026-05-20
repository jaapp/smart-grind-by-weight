#pragma once
#include <lvgl.h>

class UIManager;

// Shows connectivity status icon with color coding

class StatusIndicatorController {
public:
    explicit StatusIndicatorController(UIManager* manager);

    void build();
    void update();

private:
    void update_connectivity_status_icon();

    UIManager* ui_manager_;
    lv_obj_t* connectivity_status_icon_ = nullptr;
};
