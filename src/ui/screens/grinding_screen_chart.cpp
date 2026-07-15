#include "grinding_screen_chart.h"
#include "arduino_compat.h"
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

    // Scrollable viewport: the chart inside grows wider than this as the session
    // runs, and the viewport scrolls horizontally through the full history
    chart_scroll = lv_obj_create(screen);
    lv_obj_set_size(chart_scroll, LV_PCT(100), 140);
    lv_obj_set_style_bg_opa(chart_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_scroll, 0, 0);
    lv_obj_set_style_pad_all(chart_scroll, 0, 0);
    lv_obj_set_scroll_dir(chart_scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(chart_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(chart_scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);

    // Create chart - starts at viewport width, widens as history accumulates
    chart = lv_chart_create(chart_scroll);
    lv_obj_set_size(chart, LV_PCT(100), 140);
    lv_obj_align(chart, LV_ALIGN_LEFT_MID, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HISTORY_MAX_POINTS);
    lv_chart_set_div_line_count(chart, 0, 0);  // No grid lines for clean look
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR); // Values written by id; never shifted

    // Chart styling - dark background
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);

    // Initialize data tracking
    target_weight_value = 18.0f;
    max_y_value = target_weight_value + 0.2f;
    time_mode = false;
    target_time_seconds = 0.0f;
    history_count = 0;
    last_append_ms = 0;
    flow_accum = 0.0f;
    flow_accum_samples = 0;
    chart_view_width = 0;

    // Set Y-axis ranges (scale by 10 to handle decimals)
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)(max_y_value * 10)); // Weight axis
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 25); // Flow rate axis: 0-2.5 g/s * 10

    // Add data series in z-order: weight (bottom/filled), flow rate on top
    // Weight series (red filled area) - Use primary Y axis for weight
    weight_series = lv_chart_add_series(chart, lv_color_hex(THEME_COLOR_PRIMARY), LV_CHART_AXIS_PRIMARY_Y);

    // Flow rate series (green line) - Use secondary Y axis for flow rate
    flow_rate_series = lv_chart_add_series(chart, lv_color_hex(THEME_COLOR_SUCCESS), LV_CHART_AXIS_SECONDARY_Y);

    // Style each series individually - remove data point markers
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart, lv_color_hex(THEME_COLOR_PRIMARY), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(chart, lv_color_hex(THEME_COLOR_PRIMARY), LV_PART_ITEMS);

    // Remove data point markers/circles - set both width and height to 0
    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(chart, 0, LV_PART_INDICATOR);

    // Start with an empty history (all points hidden)
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

    // The scroll viewport must stay pressable so horizontal drags scroll it, but a
    // plain tap has to keep bubbling up to the screen (which toggles the arc/chart
    // layout) - hence CLICKABLE restored after the loop above, plus EVENT_BUBBLE.
    lv_obj_add_flag(chart_scroll, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(chart_scroll, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);

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
    // Average the flow rate between appends so the decimated line stays smooth
    flow_accum += (flow_rate < 0.0f) ? 0.0f : flow_rate;
    flow_accum_samples++;

    if (last_append_ms != 0 && (current_time_ms - last_append_ms) < APPEND_INTERVAL_MS) {
        return;
    }
    last_append_ms = current_time_ms;

    float avg_flow_rate = (flow_accum_samples > 0) ? (flow_accum / flow_accum_samples) : 0.0f;
    flow_accum = 0.0f;
    flow_accum_samples = 0;

    append_history_point(current_weight, avg_flow_rate);
}

void GrindingScreenChart::append_history_point(float current_weight, float avg_flow_rate) {
    if (!chart || history_count >= HISTORY_MAX_POINTS) {
        return;
    }

    // Scale weight and flow rate by 10 to handle decimals in LVGL chart
    float clamped_weight = (current_weight < 0.0f) ? 0.0f : current_weight;
    int32_t weight_value = (int32_t)(clamped_weight * 10);
    float clamped_flow_rate = (avg_flow_rate > 2.5f) ? 2.5f : avg_flow_rate;
    int32_t flow_rate_value = (int32_t)(clamped_flow_rate * 10);

    lv_chart_set_value_by_id(chart, weight_series, history_count, weight_value);
    lv_chart_set_value_by_id(chart, flow_rate_series, history_count, flow_rate_value);
    history_count++;

    // Resolve the viewport width once the layout is final
    if (chart_view_width <= 0) {
        lv_obj_update_layout(chart_scroll);
        chart_view_width = lv_obj_get_content_width(chart_scroll);
    }

    // Widen the chart as data accumulates and keep the newest data in view. When the
    // grind is paused/complete no points arrive, so the user can scroll back freely.
    int32_t needed_width = (int32_t)history_count * PX_PER_POINT;
    if (chart_view_width > 0 && needed_width > chart_view_width) {
        lv_obj_set_width(chart, needed_width);
        lv_obj_scroll_to_x(chart_scroll, needed_width - chart_view_width, LV_ANIM_OFF);
    }

    lv_chart_refresh(chart);
}

void GrindingScreenChart::reset_chart_data() {
    history_count = 0;
    last_append_ms = 0;
    flow_accum = 0.0f;
    flow_accum_samples = 0;

    if (!chart) {
        return;
    }

    // Hide the entire history (LV_CHART_POINT_NONE points are not drawn) and shrink
    // the chart back to the viewport
    if (weight_series) {
        lv_chart_set_all_values(chart, weight_series, LV_CHART_POINT_NONE);
    }
    if (flow_rate_series) {
        lv_chart_set_all_values(chart, flow_rate_series, LV_CHART_POINT_NONE);
    }
    lv_obj_set_width(chart, LV_PCT(100));
    if (chart_scroll) {
        lv_obj_scroll_to_x(chart_scroll, 0, LV_ANIM_OFF);
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
