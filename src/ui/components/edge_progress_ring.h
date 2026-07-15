#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

// Apple Watch-style progress ring that traces the outer border of the screen:
// a rounded-rectangle track hugging the display edge, with the indicator sweeping
// clockwise from top-center as progress goes 0-100%.
//
// Implemented with LVGL custom drawing (lines for the edges, quarter arcs for the
// corners) on a full-screen transparent, non-interactive object.
class EdgeProgressRing {
public:
    void create(lv_obj_t* parent);
    void show();
    void hide();
    void set_progress(int percent);   // 0-100, clamped
    int  get_progress() const { return progress_percent_; }
    bool is_created() const { return ring_ != nullptr; }

private:
    lv_obj_t* ring_ = nullptr;
    int progress_percent_ = 0;

    static void draw_event_cb(lv_event_t* e);
    void draw(lv_layer_t* layer);

    // Draw the border path from top-center clockwise, stopping after path_budget
    // pixels (pass a huge budget to draw the full track).
    void draw_path(lv_layer_t* layer, lv_color_t color, float path_budget);
};
