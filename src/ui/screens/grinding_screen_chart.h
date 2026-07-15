#pragma once
#include <lvgl.h>
#include "grinding_screen_base.h"
#include "../../config/constants.h"

class GrindingScreenChart : public IGrindingScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* profile_label;
    lv_obj_t* weight_spangroup;
    lv_obj_t* chart_scroll;   // Horizontally scrollable viewport holding the chart
    lv_obj_t* chart;
    lv_chart_series_t* weight_series;
    lv_chart_series_t* flow_rate_series;
    bool visible;
    bool time_mode;

    // Full-session history chart: points are appended (never shifted out), the chart
    // widget widens with the data, and the viewport scrolls horizontally so the whole
    // session stays reviewable. Series are preallocated once; unfilled points hold
    // LV_CHART_POINT_NONE and are not drawn.
    static const uint16_t HISTORY_MAX_POINTS = 1024;    // 102s of data - beyond any session (60s active timeout)
    static const uint32_t APPEND_INTERVAL_MS = 100;     // Decimate control-loop updates to 10Hz
    static const int32_t PX_PER_POINT = 3;              // 30px per second of grinding
    uint16_t history_count;
    uint32_t last_append_ms;
    float flow_accum;             // Flow-rate samples accumulated since the last append
    uint16_t flow_accum_samples;
    int32_t chart_view_width;     // Viewport content width, resolved lazily after layout
    float target_weight_value;
    float max_y_value;
    float target_time_seconds;

    void append_history_point(float current_weight, float avg_flow_rate);

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
    void add_chart_data_point(float current_weight, float flow_rate, uint32_t current_time_ms) override;
    void reset_chart_data();
    void set_time_mode(bool enabled);

    bool is_visible() const override { return visible; }
    lv_obj_t* get_screen() const override { return screen; }
};
