#include "autotune_screen.h"
#include "../ui_helpers.h"
#include <Arduino.h>
#include <cstring>

void AutoTuneScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 6, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    content_container = lv_obj_create(screen);
    lv_obj_set_size(content_container, 280, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(content_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_pad_all(content_container, 0, 0);
    lv_obj_clear_flag(content_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(content_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(content_container, 8, 0);

    lv_coord_t content_width = lv_obj_get_width(content_container);
    if (content_width <= 0) {
        content_width = 280;
    }

    // Title label
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Pulse Tune");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_align_to(content_container, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

    // Phase label - flows from title
    phase_label = lv_label_create(content_container);
    lv_label_set_text(phase_label, "Phase: Initializing");
    lv_obj_set_style_text_font(phase_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(phase_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(phase_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(phase_label, LV_PCT(100));

    // Step size data row
    step_size_row = create_data_label(content_container, "Step Size", &step_size_value_label);
    lv_obj_set_width(step_size_row, LV_PCT(100));
    lv_label_set_text(step_size_value_label, "-- ms");

    // Iteration data row
    iteration_row = create_data_label(content_container, "Iteration", &iteration_value_label);
    lv_obj_set_width(iteration_row, LV_PCT(100));
    char iter_initial[32];
    snprintf(iter_initial, sizeof(iter_initial), "0 / %d", GRIND_AUTOTUNE_MAX_ITERATIONS);
    lv_label_set_text(iteration_value_label, iter_initial);

    // Next pulse data row
    pulse_row = create_data_label(content_container, "Next Pulse", &pulse_value_label);
    lv_obj_set_width(pulse_row, LV_PCT(100));
    lv_label_set_text(pulse_value_label, "--");

    // Last pulse summary row
    last_pulse_row = create_data_label(content_container, "Prev", &last_pulse_value_label);
    lv_obj_set_width(last_pulse_row, LV_PCT(100));
    lv_label_set_text(last_pulse_value_label, "--");

    // Verification summary row
    verification_row = create_data_label(content_container, "Verification", &verification_value_label);
    lv_obj_set_width(verification_row, LV_PCT(100));
    char verif_initial[32];
    snprintf(verif_initial, sizeof(verif_initial), "-- / %d", GRIND_AUTOTUNE_VERIFICATION_PULSES);
    lv_label_set_text(verification_value_label, verif_initial);

    // Progress bar - flows from verification value
    progress_bar = lv_bar_create(content_container);
    lv_obj_set_size(progress_bar, LV_PCT(100), 10);
    lv_obj_set_style_margin_top(progress_bar, 12, 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(THEME_COLOR_NEUTRAL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(THEME_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

    // Success/failure screen labels (hidden initially)
    final_latency_label = lv_label_create(screen);
    lv_label_set_text(final_latency_label, "xx ms");
    lv_obj_set_style_text_font(final_latency_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_align(final_latency_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);

    previous_latency_label = lv_label_create(screen);
    lv_label_set_text(previous_latency_label, "Previous Value: xx ms");
    lv_obj_set_style_text_font(previous_latency_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(previous_latency_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(previous_latency_label, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);

    message_label = lv_label_create(screen);
    lv_label_set_text(message_label, "Tune\nComplete!");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(message_label, content_width);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_flag(message_label, LV_OBJ_FLAG_HIDDEN);

    // Buttons
    button_row = create_dual_button_row(screen, &cancel_button, &ok_button,
                                        LV_SYMBOL_CLOSE, LV_SYMBOL_OK,
                                        lv_color_hex(THEME_COLOR_NEUTRAL), lv_color_hex(THEME_COLOR_SUCCESS),
                                        80, &lv_font_montserrat_32);
    lv_obj_set_width(button_row, content_width);
    lv_obj_align(button_row, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    // Initially only cancel button is visible
    lv_obj_add_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    visible = false;
    current_display_phase = AutoTunePhase::IDLE;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void AutoTuneScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void AutoTuneScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void AutoTuneScreen::show_progress_screen() {
    // Show progress UI elements
    lv_obj_clear_flag(phase_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(step_size_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(iteration_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pulse_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(last_pulse_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(verification_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(phase_label, "Phase: Initializing");
    lv_label_set_text(step_size_value_label, "-- ms");
    char iter_text[32];
    snprintf(iter_text, sizeof(iter_text), "0 / %d", GRIND_AUTOTUNE_MAX_ITERATIONS);
    lv_label_set_text(iteration_value_label, iter_text);
    lv_label_set_text(pulse_value_label, "--");
    lv_label_set_text(last_pulse_value_label, "--");
    char verif_text[32];
    snprintf(verif_text, sizeof(verif_text), "-- / %d", GRIND_AUTOTUNE_VERIFICATION_PULSES);
    lv_label_set_text(verification_value_label, verif_text);
    set_progress_bar(0);

    // Hide success/failure elements
    lv_obj_add_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Pulse Tune");
}

void AutoTuneScreen::show_success_screen(float new_latency_ms, float previous_latency_ms) {
    // Hide progress elements
    lv_obj_add_flag(phase_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(step_size_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(iteration_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pulse_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(last_pulse_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(verification_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    // Show success elements
    lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Tune\nComplete!");
    lv_label_set_text(message_label, "New Motor Latency:");

    char latency_text[32];
    snprintf(latency_text, sizeof(latency_text), "%.0f ms", new_latency_ms);
    lv_label_set_text(final_latency_label, latency_text);

    char previous_text[64];
    snprintf(previous_text, sizeof(previous_text), "Previous Value: %.0f ms", previous_latency_ms);
    lv_label_set_text(previous_latency_label, previous_text);

    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);
}

void AutoTuneScreen::show_failure_screen(const char* error_message) {
    // Hide progress elements
    lv_obj_add_flag(phase_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(step_size_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(iteration_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pulse_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(last_pulse_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(verification_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    // Show failure elements
    lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Tune]\nFailed");

    // Build error message
    char full_message[256];
    const char* failure_detail = (error_message && error_message[0] != '\0') ? error_message : nullptr;
    char detail_buffer[128] = {0};

    if (failure_detail) {
        size_t len = strlen(failure_detail);
        bool needs_period = failure_detail[len - 1] != '.' && failure_detail[len - 1] != '!';
        snprintf(detail_buffer, sizeof(detail_buffer), "%s%s", failure_detail, needs_period ? "." : "");
    }

    if (detail_buffer[0] != '\0') {
        snprintf(full_message, sizeof(full_message),
                 "Could not find reliable minimum pulse duration. %s Check grinder power connection, beans in hopper, and ensure a cup is on the scale.",
                 detail_buffer);
    } else {
        snprintf(full_message, sizeof(full_message),
                 "Could not find reliable minimum pulse duration. Check grinder power connection, beans in hopper, and ensure a cup is on the scale.");
    }
    lv_label_set_text(message_label, full_message);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, -20);

    char default_text[32];
    snprintf(default_text, sizeof(default_text), "%.0f ms", (float)GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS);
    lv_label_set_text(final_latency_label, default_text);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(final_latency_label, LV_ALIGN_CENTER, 0, 80);

    lv_label_set_text(previous_latency_label, "Using default:");
    lv_obj_align(previous_latency_label, LV_ALIGN_CENTER, 0, 50);
}

void AutoTuneScreen::update_progress(const AutoTuneProgress& progress) {
    current_display_phase = progress.phase;

    // Update phase label
    const char* phase_name = "";
    switch (progress.phase) {
        case AutoTunePhase::PRIMING:
            phase_name = "Priming Chute";
            break;
        case AutoTunePhase::BINARY_SEARCH:
            phase_name = "Binary Search";
            break;
        case AutoTunePhase::VERIFICATION:
            phase_name = "Verifying Result";
            break;
        default:
            phase_name = "Initializing";
            break;
    }

    char phase_text[64];
    snprintf(phase_text, sizeof(phase_text), "Phase: %s", phase_name);
    lv_label_set_text(phase_label, phase_text);

    // Update step size
    char step_text[32];
    snprintf(step_text, sizeof(step_text), "%.1f ms", progress.step_size_ms);
    lv_label_set_text(step_size_value_label, step_text);

    // Update iteration
    char iter_text[32];
    snprintf(iter_text, sizeof(iter_text), "%d / %d",
             progress.iteration, GRIND_AUTOTUNE_MAX_ITERATIONS);
    lv_label_set_text(iteration_value_label, iter_text);

    // Update pulse (next scheduled)
    char pulse_text[64];
    if (progress.phase == AutoTunePhase::PRIMING) {
        snprintf(pulse_text, sizeof(pulse_text), "%d ms", GRIND_AUTOTUNE_PRIMING_PULSE_MS);
    } else if (progress.current_pulse_ms > 0.0f) {
        snprintf(pulse_text, sizeof(pulse_text), "%.1f ms", progress.current_pulse_ms);
    } else {
        snprintf(pulse_text, sizeof(pulse_text), "--");
    }
    lv_label_set_text(pulse_value_label, pulse_text);

    // Update last pulse summary
    char last_pulse_text[64];
    if (progress.last_pulse_ms > 0.0f) {
        const char* result_str = progress.last_pulse_success ? "Ok" : "--";
        snprintf(last_pulse_text, sizeof(last_pulse_text), "%.1f ms (%s)",
                 progress.last_pulse_ms, result_str);
    } else {
        snprintf(last_pulse_text, sizeof(last_pulse_text), "--");
    }
    lv_label_set_text(last_pulse_value_label, last_pulse_text);

    // Update verification value (right-aligned)
    char verif_text[32];
    if (progress.phase == AutoTunePhase::VERIFICATION) {
        snprintf(verif_text, sizeof(verif_text), "%d / %d Ok",
                 progress.verification_success_count, GRIND_AUTOTUNE_VERIFICATION_PULSES);
    } else {
        snprintf(verif_text, sizeof(verif_text), "-- / %d Ok",
                 GRIND_AUTOTUNE_VERIFICATION_PULSES);
    }
    lv_label_set_text(verification_value_label, verif_text);

    // Update progress bar (rough estimate)
    int percent = 0;
    if (progress.phase == AutoTunePhase::PRIMING) {
        percent = 5;
    } else if (progress.phase == AutoTunePhase::BINARY_SEARCH) {
        percent = 10 + (progress.iteration * 60 / GRIND_AUTOTUNE_MAX_ITERATIONS);
    } else if (progress.phase == AutoTunePhase::VERIFICATION) {
        percent = 70 + (progress.verification_round * 30 / 5);
    }
    set_progress_bar(percent);
}

void AutoTuneScreen::set_progress_bar(int percent) {
    lv_bar_set_value(progress_bar, percent, LV_ANIM_OFF);
}
