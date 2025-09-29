#include "ui_helpers.h"
#include <cstdio>
#include <cstdlib>

void style_as_button(lv_obj_t* object, int32_t width, int32_t height, const lv_font_t* font) {
    lv_obj_set_style_radius(object, THEME_CORNER_RADIUS_PX, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(THEME_COLOR_NEUTRAL), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_hor(object, 20, 0);
    if (width >= 0){
        lv_obj_set_style_width(object, width, 0);
    }
    if (height >= 0){
        lv_obj_set_style_height(object, height, 0);
    }
    
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_button(lv_obj_t* parent, const char* text, lv_color_t bg_color, int32_t width, int32_t height, const lv_font_t* font){ 
    lv_obj_t* button = lv_btn_create(parent);
    style_as_button(button, width, height, font);
    lv_obj_set_style_bg_color(button, bg_color, 0);
    
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    
    return button;  
}

void set_label_text_int(lv_obj_t* label, int32_t value, const char* unit) {
    if (!label) return;
    char buf[24];

    if (unit) {
        snprintf(buf, sizeof(buf), "%ld %s", value, unit);
    } else {
        snprintf(buf, sizeof(buf), "%ld", value);
    }

    lv_label_set_text(label, buf);
}

void set_label_text_float(lv_obj_t* label, float value, const char* unit) {
    if (!label) return;
    char buf[24];
    
    if (unit) {
        snprintf(buf, sizeof(buf), "%.2fg %s", value, unit);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", value);
    }

    lv_label_set_text(label, buf);
}

lv_obj_t* create_profile_label(lv_obj_t* parent, lv_obj_t** profile_label, lv_obj_t** weight_label){
    lv_obj_t* label_container = lv_obj_create(parent);
    lv_obj_set_size(label_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(label_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label_container, 0, 0);
    lv_obj_set_style_pad_all(label_container, 0, 0);
    
    // Set up button container as horizontal flex
    lv_obj_set_layout(label_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(label_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(label_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(label_container, 0, 0);

    *profile_label = lv_label_create(label_container);
    lv_label_set_text(*profile_label, "DOUBLE");
    lv_obj_set_style_text_font(*profile_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(*profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    
    *weight_label = lv_label_create(label_container);
    lv_label_set_text(*weight_label, "18.0g");
    lv_obj_set_style_text_font(*weight_label, &lv_font_montserrat_60, 0);
    lv_obj_set_style_text_color(*weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    return label_container;
}

lv_obj_t* create_dual_button_row(lv_obj_t* parent, lv_obj_t** left_button, lv_obj_t** right_button, const char* left_name, const char* right_name, lv_color_t left_color, lv_color_t right_color, int height, const lv_font_t* font){
    lv_obj_t *row_container = lv_obj_create(parent);
    lv_obj_set_size(row_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_container, 0, 0);
    lv_obj_set_style_pad_all(row_container, 0, 0);
    
    lv_obj_set_layout(row_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row_container, 10, 0);

    *left_button = create_button(row_container, left_name, left_color, -1, height, font);
    lv_obj_set_flex_grow(*left_button, 1);

    *right_button = create_button(row_container, right_name, right_color, -1, height, font);
    lv_obj_set_flex_grow(*right_button, 1);

    return row_container;
}

// Radio button group data structure
struct RadioButtonGroupData {
    lv_obj_t** buttons;
    int button_count;
    int selected_index;
    radio_button_callback_t callback;
    void* user_data;
};

// Internal event handler for radio button clicks
static void radio_button_event_handler(lv_event_t* e) {
    lv_obj_t* clicked_button = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* group = lv_obj_get_parent(clicked_button);
    RadioButtonGroupData* data = (RadioButtonGroupData*)lv_obj_get_user_data(group);
    
    if (!data) return;
    
    // Find which button was clicked
    int clicked_index = -1;
    for (int i = 0; i < data->button_count; i++) {
        if (data->buttons[i] == clicked_button) {
            clicked_index = i;
            break;
        }
    }
    
    if (clicked_index == -1 || clicked_index == data->selected_index) return;
    
    // Update selection
    data->selected_index = clicked_index;
    
    // Update visual states
    for (int i = 0; i < data->button_count; i++) {
        if (i == clicked_index) {
            lv_obj_set_style_bg_color(data->buttons[i], lv_color_hex(THEME_COLOR_PRIMARY), 0);
        } else {
            lv_obj_set_style_bg_color(data->buttons[i], lv_color_hex(THEME_COLOR_NEUTRAL), 0);
        }
    }
    
    // Call user callback
    if (data->callback) {
        data->callback(clicked_index, data->user_data);
    }
}

lv_obj_t* create_radio_button_group(
    lv_obj_t* parent,
    const char* options[],
    int option_count,
    lv_flex_flow_t layout,
    int initial_selection,
    int32_t button_width,
    int32_t button_height,
    radio_button_callback_t callback,
    void* user_data) {
    
    // Create container
    lv_obj_t* group_container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(group_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group_container, 0, 0);
    lv_obj_set_style_pad_all(group_container, 0, 0);
    lv_obj_set_style_margin_all(group_container, 0, 0);
    lv_obj_set_style_margin_bottom(group_container, 10, 0);
    
    // Set layout
    lv_obj_set_layout(group_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(group_container, layout);
    
    if (layout == LV_FLEX_FLOW_ROW) {
        lv_obj_set_flex_align(group_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(group_container, 10, 0);
        lv_obj_set_size(group_container, 280, LV_SIZE_CONTENT);
    } else {
        lv_obj_set_flex_align(group_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(group_container, 10, 0);
        lv_obj_set_size(group_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    
    // Allocate data structure
    RadioButtonGroupData* data = (RadioButtonGroupData*)malloc(sizeof(RadioButtonGroupData));
    data->buttons = (lv_obj_t**)malloc(sizeof(lv_obj_t*) * option_count);
    data->button_count = option_count;
    data->selected_index = initial_selection;
    data->callback = callback;
    data->user_data = user_data;
    
    // Calculate button width if auto
    int32_t actual_button_width = button_width;
    if (layout == LV_FLEX_FLOW_ROW && button_width == -1) {
        actual_button_width = (280 - (option_count - 1) * 10) / option_count;
    }
    
    // Create buttons
    for (int i = 0; i < option_count; i++) {
        lv_color_t color = (i == initial_selection) ? lv_color_hex(THEME_COLOR_PRIMARY) : lv_color_hex(THEME_COLOR_NEUTRAL);
        data->buttons[i] = create_button(group_container, options[i], color, actual_button_width, button_height, &lv_font_montserrat_24);
        
        // Add event handler
        lv_obj_add_event_cb(data->buttons[i], radio_button_event_handler, LV_EVENT_CLICKED, nullptr);
    }
    
    // Store data in container
    lv_obj_set_user_data(group_container, data);
    
    return group_container;
}

void radio_button_group_set_selection(lv_obj_t* group, int selected_index) {
    RadioButtonGroupData* data = (RadioButtonGroupData*)lv_obj_get_user_data(group);
    if (!data || selected_index < 0 || selected_index >= data->button_count) return;
    
    data->selected_index = selected_index;
    
    // Update visual states
    for (int i = 0; i < data->button_count; i++) {
        if (i == selected_index) {
            lv_obj_set_style_bg_color(data->buttons[i], lv_color_hex(THEME_COLOR_PRIMARY), 0);
        } else {
            lv_obj_set_style_bg_color(data->buttons[i], lv_color_hex(THEME_COLOR_NEUTRAL), 0);
        }
    }
}

int radio_button_group_get_selection(lv_obj_t* group) {
    RadioButtonGroupData* data = (RadioButtonGroupData*)lv_obj_get_user_data(group);
    return data ? data->selected_index : -1;
}