#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "grinding_screen_base.h"

class GrindingScreenArc : public IGrindingScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* profile_label;
    lv_obj_t* target_label;
    lv_obj_t* weight_label;
    lv_obj_t* progress_arc;
    bool visible;
    bool time_mode;

public:
    void create() override;
    void show() override;
    void hide() override;
    void update_profile_name(const char* name) override;
    void update_target_weight(float weight) override;
    void update_target_weight_text(const char* text) override;
    void update_target_time(float seconds);
    void update_current_weight(float weight) override;
    void update_tare_display() override;
    void update_progress(int percent) override;
    void set_time_mode(bool enabled);
    
    bool is_visible() const override { return visible; }
    lv_obj_t* get_screen() const override { return screen; }
};
