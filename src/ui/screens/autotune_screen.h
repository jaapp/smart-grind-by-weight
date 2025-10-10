#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../controllers/autotune_controller.h"

class AutoTuneScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* content_container;
    lv_obj_t* title_label;
    lv_obj_t* phase_label;
    lv_obj_t* step_size_row;
    lv_obj_t* iteration_row;
    lv_obj_t* pulse_row;
    lv_obj_t* last_pulse_row;
    lv_obj_t* verification_row;
    lv_obj_t* button_row;
    lv_obj_t* step_size_value_label;
    lv_obj_t* iteration_value_label;
    lv_obj_t* pulse_value_label;
    lv_obj_t* last_pulse_value_label;
    lv_obj_t* verification_value_label;
    lv_obj_t* progress_bar;
    lv_obj_t* cancel_button;
    lv_obj_t* ok_button;

    // Success/failure screen elements
    lv_obj_t* final_latency_label;
    lv_obj_t* previous_latency_label;
    lv_obj_t* message_label;

    bool visible;
    AutoTunePhase current_display_phase;

public:
    void create();
    void show();
    void hide();

    void show_progress_screen();
    void show_success_screen(float new_latency_ms, float previous_latency_ms);
    void show_failure_screen(const char* error_message);

    void update_progress(const AutoTuneProgress& progress);
    void set_progress_bar(int percent);

    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_cancel_button() const { return cancel_button; }
    lv_obj_t* get_ok_button() const { return ok_button; }
};
