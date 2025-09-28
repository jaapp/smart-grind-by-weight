#pragma once
#include <lvgl.h>
#include "grinding_screen_base.h"
#include "../../config/constants.h"

class GrindingScreenChart : public IGrindingScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* profile_label;
    lv_obj_t* weight_spangroup;
    lv_obj_t* chart;
    lv_chart_series_t* weight_series;
    lv_chart_series_t* flow_rate_series;
    bool visible;
    bool time_mode;
    
    // Chart data management
    static const uint16_t MAX_CHART_POINTS = 1000;
    static constexpr float REFERENCE_FLOW_RATE_GPS = 1.6f;  // Reference flow rate for time prediction
    static const uint32_t DATA_POINT_INTERVAL_MS = SYS_TASK_GRIND_CONTROL_INTERVAL_MS; // Match grind control loop (50Hz)
    uint32_t chart_start_time_ms;
    uint32_t predicted_grind_time_ms;
    uint16_t predicted_chart_points;
    float target_weight_value;
    float max_y_value;
    uint32_t last_data_point_time_ms;
    float target_time_seconds;

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
    void set_chart_time_prediction(uint32_t predicted_time_ms);
    void reset_chart_data();
    void set_time_mode(bool enabled);

    bool is_visible() const override { return visible; }
    lv_obj_t* get_screen() const override { return screen; }
};
