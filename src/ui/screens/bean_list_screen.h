#pragma once

#include <lvgl.h>

class BeanController;

class BeanListScreen {
private:
    lv_obj_t* screen = nullptr;
    lv_obj_t* count_label = nullptr;
    lv_obj_t* list = nullptr;
    bool visible = false;

public:
    void create();
    void show();
    void hide();
    void update(const BeanController* beans);

    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }

private:
    lv_obj_t* create_row(lv_obj_t* parent, const BeanController* beans, uint8_t index);
};
