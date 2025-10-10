#include "autotune_screen.h"
#include "../ui_helpers.h"
#include <Arduino.h>

void AutoTuneScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 6, 0);

    // Title label
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Motor Response Auto-Tune");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    // Phase label
    phase_label = lv_label_create(screen);
    lv_label_set_text(phase_label, "Phase: Initializing");
    lv_obj_set_style_text_font(phase_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(phase_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(phase_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(phase_label, LV_ALIGN_CENTER, 0, -70);

    // Step size label
    step_size_label = lv_label_create(screen);
    lv_label_set_text(step_size_label, "Step Size: -- ms");
    lv_obj_set_style_text_font(step_size_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(step_size_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(step_size_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(step_size_label, LV_ALIGN_CENTER, 0, -40);

    // Iteration label
    iteration_label = lv_label_create(screen);
    lv_label_set_text(iteration_label, "Iteration: 0 / 50");
    lv_obj_set_style_text_font(iteration_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(iteration_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(iteration_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(iteration_label, LV_ALIGN_CENTER, 0, -10);

    // Pulse label
    pulse_label = lv_label_create(screen);
    lv_label_set_text(pulse_label, "Pulse: -- ms");
    lv_obj_set_style_text_font(pulse_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pulse_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(pulse_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(pulse_label, LV_ALIGN_CENTER, 0, 20);

    // Result label
    result_label = lv_label_create(screen);
    lv_label_set_text(result_label, "Result: --");
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 50);

    // Verification label
    verification_label = lv_label_create(screen);
    lv_label_set_text(verification_label, "Verification: -- / 5 Success");
    lv_obj_set_style_text_font(verification_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(verification_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(verification_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(verification_label, LV_ALIGN_CENTER, 0, 80);

    // Progress bar
    progress_bar = lv_bar_create(screen);
    lv_obj_set_size(progress_bar, 280, 10);
    lv_obj_align(progress_bar, LV_ALIGN_CENTER, 0, 115);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(THEME_COLOR_NEUTRAL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(THEME_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

    // Success/failure screen labels (hidden initially)
    final_latency_label = lv_label_create(screen);
    lv_label_set_text(final_latency_label, "40 ms");
    lv_obj_set_style_text_font(final_latency_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_align(final_latency_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);

    previous_latency_label = lv_label_create(screen);
    lv_label_set_text(previous_latency_label, "Previous Value: 75 ms");
    lv_obj_set_style_text_font(previous_latency_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(previous_latency_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(previous_latency_label, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);

    message_label = lv_label_create(screen);
    lv_label_set_text(message_label, "Auto-Tune Complete!");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_flag(message_label, LV_OBJ_FLAG_HIDDEN);

    // Buttons
    lv_obj_t* button_row = create_dual_button_row(screen, &cancel_button, &ok_button,
                                                   LV_SYMBOL_CLOSE, LV_SYMBOL_OK,
                                                   lv_color_hex(THEME_COLOR_NEUTRAL), lv_color_hex(THEME_COLOR_SUCCESS),
                                                   80, &lv_font_montserrat_32);
    lv_obj_align(button_row, LV_ALIGN_BOTTOM_MID, 0, 0);

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
    lv_obj_clear_flag(step_size_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(iteration_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pulse_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(result_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(verification_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);

    // Hide success/failure elements
    lv_obj_add_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Motor Response Auto-Tune");
}

void AutoTuneScreen::show_success_screen(float new_latency_ms, float previous_latency_ms) {
    // Hide progress elements
    lv_obj_add_flag(phase_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(step_size_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(iteration_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pulse_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(result_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(verification_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);

    // Show success elements
    lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Auto-Tune Complete!");
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
    lv_obj_add_flag(step_size_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(iteration_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pulse_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(result_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(verification_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_HIDDEN);

    // Show failure elements
    lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(previous_latency_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Auto-Tune Failed");

    // Build error message
    char full_message[256];
    snprintf(full_message, sizeof(full_message),
             "Could not find reliable\nminimum pulse duration.\n\n%s\n\nCheck:\n• Grinder power connection\n• Beans in hopper\n• Cup on scale",
             error_message ? error_message : "");
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
    char step_text[64];
    snprintf(step_text, sizeof(step_text), "Step Size: %.1f ms", progress.step_size_ms);
    lv_label_set_text(step_size_label, step_text);

    // Update iteration
    char iter_text[64];
    snprintf(iter_text, sizeof(iter_text), "Iteration: %d / %d",
             progress.iteration, GRIND_AUTOTUNE_MAX_ITERATIONS);
    lv_label_set_text(iteration_label, iter_text);

    // Update pulse
    char pulse_text[64];
    snprintf(pulse_text, sizeof(pulse_text), "Pulse: %.1f ms", progress.current_pulse_ms);
    lv_label_set_text(pulse_label, pulse_text);

    // Update result
    char result_text[64];
    if (progress.phase == AutoTunePhase::PRIMING) {
        snprintf(result_text, sizeof(result_text), "Result: --");
    } else {
        snprintf(result_text, sizeof(result_text), "Result: %s",
                 progress.last_pulse_success ? "✓ Grounds Detected" : "✗ No Grounds");
    }
    lv_label_set_text(result_label, result_text);

    // Update verification
    char verif_text[64];
    if (progress.phase == AutoTunePhase::VERIFICATION) {
        snprintf(verif_text, sizeof(verif_text), "Verification: %d / %d Success",
                 progress.verification_success_count, GRIND_AUTOTUNE_VERIFICATION_PULSES);
    } else {
        snprintf(verif_text, sizeof(verif_text), "Verification: -- / %d Success",
                 GRIND_AUTOTUNE_VERIFICATION_PULSES);
    }
    lv_label_set_text(verification_label, verif_text);

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
