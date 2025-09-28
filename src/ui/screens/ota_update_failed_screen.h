#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

#define OTA_WARNING_BUTTON_WIDTH 120
#define OTA_WARNING_BUTTON_HEIGHT 60

class OtaUpdateFailedScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* title_label;
    lv_obj_t* message_label;
    lv_obj_t* details_label;
    lv_obj_t* ok_button;
    lv_obj_t* ok_button_label;
    bool visible;

public:
    void create();
    void show(const char* expected_build);
    void hide();
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_ok_button() const { return ok_button; }
};