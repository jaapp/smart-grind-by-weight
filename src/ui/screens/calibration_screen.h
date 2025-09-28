#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

enum CalibrationStep {
    CAL_STEP_EMPTY,
    CAL_STEP_WEIGHT,
    CAL_STEP_COMPLETE
};

class CalibrationScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* title_label;
    lv_obj_t* instruction_label;
    lv_obj_t* weight_label;
    lv_obj_t* ok_button;
    lv_obj_t* cancel_button;
    lv_obj_t* plus_btn;
    lv_obj_t* minus_btn;
    lv_obj_t* weight_input;
    lv_obj_t* top_button_row;
    CalibrationStep current_step;
    float calibration_weight;
    bool visible;

public:
    void create();
    void show();
    void hide();
    void set_step(CalibrationStep step);
    void update_current_weight(float weight);
    void update_calibration_weight(float weight);
    
    bool is_visible() const { return visible; }
    CalibrationStep get_step() const { return current_step; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_ok_button() const { return ok_button; }
    lv_obj_t* get_cancel_button() const { return cancel_button; }
    lv_obj_t* get_plus_btn() const { return plus_btn; }
    lv_obj_t* get_minus_btn() const { return minus_btn; }
    lv_obj_t* get_weight_input() const { return weight_input; }
    float get_calibration_weight() const { return calibration_weight; }
};
