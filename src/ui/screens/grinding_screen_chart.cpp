#include "grinding_screen_chart.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include <lvgl.h>
#include <widgets/span/lv_span.h>

void GrindingScreenChart::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0); // Keep transparent
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE); // Make the parent screen container clickable

    // Use flex layout for centering
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(screen, 15, 0);

    // Profile name label
    profile_label = lv_label_create(screen);
    lv_label_set_text(profile_label, "DOUBLE");
    lv_obj_set_style_text_font(profile_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);

    // Create chart - use full screen width
    chart = lv_chart_create(screen);
    lv_obj_set_size(chart, LV_PCT(100), 140);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, MAX_CHART_POINTS);
    lv_chart_set_div_line_count(chart, 0, 0);  // No grid lines for clean look
    
    // Chart styling - dark background
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    
    // Initialize data tracking
    target_weight_value = 18.0f;
    chart_start_time_ms = 0;
    predicted_grind_time_ms = (uint32_t)(1000.0f + (target_weight_value / REFERENCE_FLOW_RATE_GPS) * 1000.0f);
    predicted_chart_points = (predicted_grind_time_ms / DATA_POINT_INTERVAL_MS);
    if (predicted_chart_points > MAX_CHART_POINTS) {
        predicted_chart_points = MAX_CHART_POINTS;
    }
    max_y_value = target_weight_value + 0.2f;
    last_data_point_time_ms = 0;
    time_mode = false;
    target_time_seconds = 0.0f;
    
    // Set chart to use predicted number of points, enable sliding window
    lv_chart_set_point_count(chart, predicted_chart_points);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT); // Slide data left when full
    
    // Set Y-axis ranges (scale by 10 to handle decimals)
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)(max_y_value * 10)); // Weight axis
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 25); // Flow rate axis: 0-2.5 g/s * 10
    
    // Add data series in z-order: weight (bottom/filled), flow rate, target (top)
    // Weight series (red filled area) - Use primary Y axis for weight
    weight_series = lv_chart_add_series(chart, lv_color_hex(THEME_COLOR_PRIMARY), LV_CHART_AXIS_PRIMARY_Y);
    
    // Flow rate series (green line) - Use secondary Y axis for flow rate
    flow_rate_series = lv_chart_add_series(chart, lv_color_hex(THEME_COLOR_SUCCESS), LV_CHART_AXIS_SECONDARY_Y);
    
    // Configure chart to show both lines and bars (for filled area effect)
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    
    // Style each series individually - remove data point markers
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart, lv_color_hex(THEME_COLOR_PRIMARY), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(chart, lv_color_hex(THEME_COLOR_PRIMARY), LV_PART_ITEMS);
    // lv_obj_set_style_bg_opa(chart, LV_OPA_20, LV_PART_ITEMS);
    
    // Remove data point markers/circles - set both width and height to 0
    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(chart, 0, LV_PART_INDICATOR);
    
    // Flow rate and target series will inherit line styling
    
    // Initialize all series with zero values
    reset_chart_data();

    // Current/Target weight display with mixed font sizes using spangroup
    weight_spangroup = lv_spangroup_create(screen);
    lv_obj_set_width(weight_spangroup, LV_PCT(100));
    lv_obj_set_style_text_align(weight_spangroup, LV_TEXT_ALIGN_CENTER, 0);
    lv_spangroup_set_align(weight_spangroup, LV_TEXT_ALIGN_CENTER);
    lv_spangroup_set_overflow(weight_spangroup, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_indent(weight_spangroup, 0);
    lv_spangroup_set_mode(weight_spangroup, LV_SPAN_MODE_BREAK);
    
    // Create initial spans using correct API
    lv_span_t* current_span = lv_spangroup_add_span(weight_spangroup);
    lv_style_set_text_font(lv_span_get_style(current_span), &lv_font_montserrat_56);
    lv_style_set_text_color(lv_span_get_style(current_span), lv_color_hex(THEME_COLOR_TEXT_PRIMARY));
    lv_span_set_text(current_span, "0.0g");
    
    lv_span_t* separator_span = lv_spangroup_add_span(weight_spangroup);
    lv_style_set_text_font(lv_span_get_style(separator_span), &lv_font_montserrat_24);
    lv_style_set_text_color(lv_span_get_style(separator_span), lv_color_hex(THEME_COLOR_TEXT_SECONDARY));
    lv_span_set_text(separator_span, " / 18.0g");
    
    lv_spangroup_refresh(weight_spangroup);
    
    // MODIFIED: Ensure all child widgets pass click events to the parent screen
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen); i++) {
        lv_obj_clear_flag(lv_obj_get_child(screen, i), LV_OBJ_FLAG_CLICKABLE);
    }

    visible = false;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void GrindingScreenChart::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void GrindingScreenChart::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void GrindingScreenChart::update_profile_name(const char* name) {
    lv_label_set_text(profile_label, name);
}

void GrindingScreenChart::update_target_weight(float weight) {
    target_weight_value = weight;
    max_y_value = target_weight_value + 1.2f;
    
    // Recalculate predicted chart width based on new target weight
    predicted_grind_time_ms = (uint32_t)(1000.0f + (target_weight_value / REFERENCE_FLOW_RATE_GPS) * 1000.0f);
    predicted_chart_points = (predicted_grind_time_ms / DATA_POINT_INTERVAL_MS);
    if (predicted_chart_points > MAX_CHART_POINTS) {
        predicted_chart_points = MAX_CHART_POINTS;
    }
    
    if (!time_mode) {
        // Update the weight display spans for current/target format
        char current_text[16], target_text[16];
        snprintf(current_text, sizeof(current_text), "0.0g");
        snprintf(target_text, sizeof(target_text), " / " SYS_WEIGHT_DISPLAY_FORMAT, weight);

        // Update spans
        lv_span_t* current_span = lv_spangroup_get_child(weight_spangroup, 0);
        lv_span_t* separator_span = lv_spangroup_get_child(weight_spangroup, 1);

        if (current_span && separator_span) {
            lv_span_set_text(current_span, current_text);
            lv_span_set_text(separator_span, target_text);
            lv_spangroup_refresh(weight_spangroup);
        }
    }
    
    // Update chart configuration
    lv_chart_set_point_count(chart, predicted_chart_points);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)(max_y_value * 10)); // Weight axis
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 25); // Flow rate axis: 0-2.5 g/s * 10
    
    lv_chart_refresh(chart);
}

void GrindingScreenChart::update_target_weight_text(const char* text) {
    lv_span_t* current_span = lv_spangroup_get_child(weight_spangroup, 0);
    lv_span_t* separator_span = lv_spangroup_get_child(weight_spangroup, 1);

    if (current_span && separator_span) {
        const char* target_text = text ? text : "";
        char formatted_text[48];
        if (target_text[0] && target_text[0] != ' ' && target_text[0] != '/' && target_text[0] != '\n') {
            snprintf(formatted_text, sizeof(formatted_text), "\n%s", target_text);
        } else {
            snprintf(formatted_text, sizeof(formatted_text), "%s", target_text);
        }
        lv_span_set_text(separator_span, formatted_text);
        lv_spangroup_refresh(weight_spangroup);
    }
}

void GrindingScreenChart::update_target_time(float seconds) {
    target_time_seconds = seconds;
    lv_span_t* current_span = lv_spangroup_get_child(weight_spangroup, 0);
    lv_span_t* separator_span = lv_spangroup_get_child(weight_spangroup, 1);

    if (current_span && separator_span) {
        // Keep the current weight span untouched; show time on a new line without slash
        char target_text[48];
        snprintf(target_text, sizeof(target_text), "\nTime: %.1fs", seconds);
        lv_span_set_text(separator_span, target_text);
        lv_spangroup_refresh(weight_spangroup);
    }
}

void GrindingScreenChart::update_current_weight(float weight) {
    char current_text[16], target_text[16];
    snprintf(current_text, sizeof(current_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    snprintf(target_text, sizeof(target_text), " / " SYS_WEIGHT_DISPLAY_FORMAT, target_weight_value);
    
    // Update spans
    lv_span_t* current_span = lv_spangroup_get_child(weight_spangroup, 0);
    lv_span_t* separator_span = lv_spangroup_get_child(weight_spangroup, 1);
    
    if (current_span && separator_span) {
        lv_span_set_text(current_span, current_text);
        if (time_mode) {
            char time_text[48];
            snprintf(time_text, sizeof(time_text), "\nTime: %.1fs", target_time_seconds);
            lv_span_set_text(separator_span, time_text);
        } else {
            lv_span_set_text(separator_span, target_text);
        }
        lv_spangroup_refresh(weight_spangroup);
    }
}

void GrindingScreenChart::update_tare_display() {
    char target_text[16];
    snprintf(target_text, sizeof(target_text), " / " SYS_WEIGHT_DISPLAY_FORMAT, target_weight_value);
    
    // Update spans for tare display
    lv_span_t* current_span = lv_spangroup_get_child(weight_spangroup, 0);
    lv_span_t* separator_span = lv_spangroup_get_child(weight_spangroup, 1);
    
    if (current_span && separator_span) {
        lv_span_set_text(current_span, "TARE");
        if (time_mode) {
            char time_text[48];
            snprintf(time_text, sizeof(time_text), "\nTime: %.1fs", target_time_seconds);
            lv_span_set_text(separator_span, time_text);
        } else {
            lv_span_set_text(separator_span, target_text);
        }
        lv_spangroup_refresh(weight_spangroup);
    }
}

void GrindingScreenChart::update_progress(int percent) {
    // Progress is now visualized through the chart data
    // This method is kept for compatibility but chart updates happen via add_chart_data_point
}

void GrindingScreenChart::add_chart_data_point(float current_weight, float flow_rate, uint32_t current_time_ms) {
    if (chart_start_time_ms == 0) {
        chart_start_time_ms = current_time_ms;
        last_data_point_time_ms = current_time_ms;
    }
    
    last_data_point_time_ms = current_time_ms;
    
    // Scale weight and flow rate by 10 to handle decimals in LVGL chart
    lv_coord_t weight_value = (lv_coord_t)(current_weight * 10);
    // Clamp flow rate to 0-2.5 g/s range then scale by 10
    float clamped_flow_rate = (flow_rate < 0.0f) ? 0.0f : ((flow_rate > 2.5f) ? 2.5f : flow_rate);
    lv_coord_t flow_rate_value = (lv_coord_t)(clamped_flow_rate * 10);
    
    // Add data points to series (LVGL will handle sliding window due to SHIFT mode)
    lv_chart_set_next_value(chart, weight_series, weight_value);
    lv_chart_set_next_value(chart, flow_rate_series, flow_rate_value);
    
    lv_chart_refresh(chart);
}

void GrindingScreenChart::set_chart_time_prediction(uint32_t predicted_time_ms) {
    predicted_grind_time_ms = predicted_time_ms;
    // LVGL charts don't have explicit X-axis scaling, so this mainly affects data collection frequency
}

void GrindingScreenChart::reset_chart_data() {
    chart_start_time_ms = 0;
    last_data_point_time_ms = 0;
    
    // Clear all series data using current predicted chart points
    for (int i = 0; i < predicted_chart_points; i++) {
        lv_chart_set_value_by_id(chart, weight_series, i, 0);
        lv_chart_set_value_by_id(chart, flow_rate_series, i, 0);
    }
    
    lv_chart_refresh(chart);
}

void GrindingScreenChart::set_time_mode(bool enabled) {
    time_mode = enabled;
    if (time_mode) {
        update_target_time(target_time_seconds);
    } else {
        // Revert to weight display formatting using the last known target weight
        update_target_weight(target_weight_value);
    }
}
