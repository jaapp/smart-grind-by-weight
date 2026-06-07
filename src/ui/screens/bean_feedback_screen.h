#pragma once

#include <lvgl.h>
#include <stdint.h>

class BeanFeedbackScreen {
private:
    lv_obj_t* screen = nullptr;
    lv_obj_t* bean_label = nullptr;
    lv_obj_t* gs_label = nullptr;
    bool visible = false;

public:
    void create();
    void show();
    void hide();
    void update(const char* bean_name, uint16_t mahlgrad_x2);

    bool is_visible() const { return visible; }
};
