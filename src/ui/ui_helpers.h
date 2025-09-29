#pragma once
#include <lvgl.h>
#include "../config/constants.h"

// Function declarations
void style_as_button(lv_obj_t* object, int32_t width = 260, int32_t height = 80, const lv_font_t* font = &lv_font_montserrat_28);

lv_obj_t* create_button(lv_obj_t* parent, const char* text,
                       lv_color_t bg_color = lv_color_hex(THEME_COLOR_NEUTRAL),
                       int32_t width = 260, int32_t height = 80, 
                       const lv_font_t* font = &lv_font_montserrat_28);

void set_label_text_int(lv_obj_t* label, int32_t value, const char* unit = nullptr);

void set_label_text_float(lv_obj_t* label, float value, const char* unit = nullptr);

lv_obj_t* create_profile_label(lv_obj_t* parent, lv_obj_t** profile_label, lv_obj_t** weight_label);

lv_obj_t* create_dual_button_row(lv_obj_t* parent, lv_obj_t** left_button, lv_obj_t** right_button, 
                                const char* left_name, const char* right_name, 
                                lv_color_t left_color = lv_color_hex(THEME_COLOR_NEUTRAL), 
                                lv_color_t right_color = lv_color_hex(THEME_COLOR_NEUTRAL), 
                                int height = 80, const lv_font_t* font = &lv_font_montserrat_28);

// Callback signature for radio button selection changes
typedef void (*radio_button_callback_t)(int selected_index, void* user_data);

// Radio button group helper
lv_obj_t* create_radio_button_group(
    lv_obj_t* parent,
    const char* options[],           // Array of button labels
    int option_count,                // Number of options
    lv_flex_flow_t layout,          // LV_FLEX_FLOW_ROW or LV_FLEX_FLOW_COLUMN
    int initial_selection,           // Initially selected index (0-based)
    int32_t button_width,           // Width per button (-1 for auto)
    int32_t button_height,          // Height per button
    radio_button_callback_t callback, // Called when selection changes
    void* user_data                 // Passed to callback
);

// Radio button group utility functions
void radio_button_group_set_selection(lv_obj_t* group, int selected_index);
int radio_button_group_get_selection(lv_obj_t* group);
