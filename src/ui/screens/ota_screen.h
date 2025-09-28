#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

class OTAScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* title_label;
    lv_obj_t* status_label;
    lv_obj_t* percentage_label;
    lv_obj_t* progress_arc;
    bool visible;

public:
    void create();
    void show();
    void hide();
    void update_progress(int percent);
    void update_status(const char* status);
    void update_title(const char* title);
    
    // Mode-specific convenience methods
    void show_ota_mode();
    void show_data_export_mode();
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
};