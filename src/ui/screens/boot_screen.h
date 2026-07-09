#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

// Boot splash: opaque black screen with a centered logo, shown while the scale and
// background processes initialize. Hides the first-frame render artifact behind a
// backlight fade-in (driven by UIManager) and fades out to the main screen.
class BootScreen {
private:
    lv_obj_t* screen = nullptr;
    lv_obj_t* logo = nullptr;
    bool visible = false;

public:
    void create();
    void show();
    void hide();
    // Delete the splash LVGL objects once boot completes to reclaim their heap.
    void destroy();

    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_logo() const { return logo; }
};
