#include "autotune_screen.h"
#include "../ui_helpers.h"
#include "arduino_compat.h"
#include <algorithm>
#include <cstring>

void AutoTuneScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_ver(screen, 6, 0);
    layout_below_menubar(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    // Title label (shared across all screens; always a single line so the
    // content below never interleaves with it)
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "Pulse Tune");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 8);

    // Live scale readout (weight + noise) so tuning problems are visible at a glance.
    // NOTE: "+/-" spelled out — the built-in Montserrat fonts have no U+00B1 glyph.
    live_stats_label = lv_label_create(screen);
    lv_label_set_text(live_stats_label, "Scale --.--g  +/---.---g");
    lv_obj_set_style_text_font(live_stats_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(live_stats_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(live_stats_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(live_stats_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    // Current activity ("Testing 300ms pulse", ...) — the headline of what's happening
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Starting...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status_label, 280);
    lv_obj_align_to(status_label, live_stats_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    // === Console Screen ===
    console_container = lv_obj_create(screen);
    lv_obj_set_size(console_container, 280, 255);
    lv_obj_set_style_bg_opa(console_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(console_container, 0, 0);
    lv_obj_set_style_pad_all(console_container, 0, 0);
    lv_obj_clear_flag(console_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(console_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align_to(console_container, status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    console_textarea = lv_textarea_create(console_container);
    lv_obj_set_size(console_textarea, 280, 255);
    lv_textarea_set_text(console_textarea, "");
    lv_obj_set_style_text_font(console_textarea, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(console_textarea, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_color(console_textarea, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_border_width(console_textarea, 1, 0);
    lv_obj_set_style_border_color(console_textarea, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(console_textarea, 8, 0);
    lv_textarea_set_cursor_click_pos(console_textarea, false);
    lv_obj_add_flag(console_textarea, LV_OBJ_FLAG_EVENT_BUBBLE);

    // === Result Screen ===
    // Flex column so message / value / sub-label always lay out in order — the old
    // absolute negative offsets inside a SIZE_CONTENT container clipped the failure
    // reason entirely (the "Tune Failed with no explanation" bug).
    result_container = lv_obj_create(screen);
    lv_obj_set_size(result_container, 280, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(result_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(result_container, 0, 0);
    lv_obj_set_style_pad_all(result_container, 0, 0);
    lv_obj_set_style_pad_gap(result_container, 18, 0);
    lv_obj_clear_flag(result_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(result_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(result_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(result_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(result_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Top-anchored below the title (never centered over it, never under the OK button)
    lv_obj_align(result_container, LV_ALIGN_TOP_MID, 0, 100);

    message_label = lv_label_create(result_container);
    lv_label_set_text(message_label, "New Motor Latency:");
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(message_label, 280);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    final_latency_label = lv_label_create(result_container);
    lv_label_set_text(final_latency_label, "110 ms");
    lv_obj_set_style_text_font(final_latency_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);

    previous_latency_label = lv_label_create(result_container);
    lv_label_set_text(previous_latency_label, "Previous Value: 150 ms");
    lv_obj_set_style_text_font(previous_latency_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(previous_latency_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(previous_latency_label, 280);
    lv_label_set_long_mode(previous_latency_label, LV_LABEL_LONG_WRAP);

    // Single OK button (shown on the result screens). During the console/run state the
    // global nav-bar back arrow cancels the tune, so no bottom button is shown then.
    button_row = nullptr;
    cancel_button = nullptr;
    ok_button = create_button(screen, LV_SYMBOL_OK, lv_color_hex(THEME_COLOR_SUCCESS), 260, 80, &lv_font_montserrat_32);
    lv_obj_align(ok_button, LV_ALIGN_BOTTOM_MID, 0, -10);

    visible = false;
    current_state = AutoTuneScreenState::CONSOLE;
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

void AutoTuneScreen::show_console_screen() {
    current_state = AutoTuneScreenState::CONSOLE;

    // Clear console
    lv_textarea_set_text(console_textarea, "");

    // Show console elements. No bottom button while running — back arrow cancels.
    lv_obj_clear_flag(console_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(live_stats_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ok_button, LV_OBJ_FLAG_HIDDEN);

    // Hide result screen
    lv_obj_add_flag(result_container, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Pulse Tune");
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_label_set_text(status_label, "Starting...");
}

void AutoTuneScreen::show_success_screen(float new_latency_ms, float previous_latency_ms) {
    current_state = AutoTuneScreenState::RESULT;

    // Hide console screen
    lv_obj_add_flag(console_container, LV_OBJ_FLAG_HIDDEN);

    // Show result elements + the OK button.
    lv_obj_clear_flag(result_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(live_stats_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Tune Complete");
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_align(result_container, LV_ALIGN_TOP_MID, 0, 100);
    lv_label_set_text(message_label, "New Motor Latency:");

    char latency_text[32];
    snprintf(latency_text, sizeof(latency_text), "%.0f ms", new_latency_ms);
    lv_label_set_text(final_latency_label, latency_text);

    char previous_text[64];
    snprintf(previous_text, sizeof(previous_text), "Previous Value: %.0f ms", previous_latency_ms);
    lv_label_set_text(previous_latency_label, previous_text);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(0x888888), 0);

    lv_obj_set_style_text_color(final_latency_label, lv_color_hex(THEME_COLOR_SUCCESS), 0);
}

void AutoTuneScreen::show_failure_screen(const char* error_message) {
    current_state = AutoTuneScreenState::RESULT;

    // Hide console screen
    lv_obj_add_flag(console_container, LV_OBJ_FLAG_HIDDEN);

    // Show result elements + the OK button. The live stats stay visible, frozen at
    // the reading from the moment of failure — for a settling failure that IS the
    // evidence (noise vs. the required tolerance).
    lv_obj_clear_flag(result_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ok_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(live_stats_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    // Failure has no meaningful "result number" — the compact default line says it all
    lv_obj_add_flag(final_latency_label, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(title_label, "Tune Failed");
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(result_container, LV_ALIGN_TOP_MID, 0, 110);

    // Lead with the actual failure reason, then a targeted, quantitative hint.
    const char* reason = (error_message && error_message[0] != '\0')
                             ? error_message
                             : "Unknown error";
    char hint[96] = {0};
    if (strstr(reason, "Settling")) {
        snprintf(hint, sizeof(hint), "Scale never settled.\nNoise must be under %.3fg.",
                 GRIND_SCALE_SETTLING_TOLERANCE_G);
    } else if (strstr(reason, "Priming") || strstr(reason, "No successful pulse")) {
        snprintf(hint, sizeof(hint), "No grounds detected.\nCheck beans, power, and cup.");
    } else if (!strstr(reason, "Cancelled")) {
        snprintf(hint, sizeof(hint), "Check beans, power, and cup, then retry.");
    }

    char full_message[224];
    if (hint[0] != '\0') {
        snprintf(full_message, sizeof(full_message), "%s\n\n%s", reason, hint);
    } else {
        snprintf(full_message, sizeof(full_message), "%s", reason);
    }
    lv_label_set_text(message_label, full_message);

    char default_text[48];
    snprintf(default_text, sizeof(default_text), "Using default: %.0f ms",
             (float)GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS);
    lv_label_set_text(previous_latency_label, default_text);
    lv_obj_set_style_text_color(previous_latency_label, lv_color_hex(THEME_COLOR_WARNING), 0);
}

void AutoTuneScreen::append_console_message(const char* message) {
    if (current_state != AutoTuneScreenState::CONSOLE) {
        return;
    }

    // Append new message with newline
    lv_textarea_add_text(console_textarea, message);
    lv_textarea_add_text(console_textarea, "\n");

    // Auto-scroll to bottom
    lv_obj_scroll_to_y(console_textarea, LV_COORD_MAX, LV_ANIM_OFF);
}

void AutoTuneScreen::update_progress(const AutoTuneProgress& progress) {
    // Append new console messages if available
    if (progress.has_new_message && progress.last_message[0] != '\0') {
        append_console_message(progress.last_message);
        // Note: We can't clear the flag here since progress is const.
        // The flag will be cleared on the next log_message() call anyway,
        // which overwrites the buffer with new content.
    }

    // Headline of the current activity
    if (status_label && current_state == AutoTuneScreenState::CONSOLE) {
        char status[48];
        switch (progress.phase) {
            case AutoTunePhase::PRIMING:
                snprintf(status, sizeof(status), "Priming chute...");
                break;
            case AutoTunePhase::BINARY_SEARCH:
                snprintf(status, sizeof(status), "Testing %.0fms pulse", progress.current_pulse_ms);
                break;
            case AutoTunePhase::VERIFICATION:
                snprintf(status, sizeof(status), "Verifying %.0fms (round %d)",
                         progress.current_pulse_ms, progress.verification_round + 1);
                break;
            default:
                status[0] = '\0';
                break;
        }
        if (status[0] != '\0') {
            lv_label_set_text(status_label, status);
        }
    }
}

void AutoTuneScreen::update_live_stats(float weight_g, float noise_std_dev_g) {
    if (!live_stats_label || current_state != AutoTuneScreenState::CONSOLE) {
        return;
    }

    // Noise is the same 500ms std dev the settling gate checks, so a reading far
    // above the settling tolerance immediately explains a stuck/failed tune.
    // ("+/-" spelled out: the built-in Montserrat fonts have no U+00B1 glyph.)
    char buffer[48];
    snprintf(buffer, sizeof(buffer), "Scale %.2fg  +/-%.3fg", weight_g, noise_std_dev_g);
    lv_label_set_text(live_stats_label, buffer);

    // Color the noise hint: green when settleable, warning when it is not
    bool settleable = noise_std_dev_g <= GRIND_SCALE_SETTLING_TOLERANCE_G;
    lv_obj_set_style_text_color(live_stats_label,
                                lv_color_hex(settleable ? THEME_COLOR_SUCCESS : THEME_COLOR_WARNING), 0);
}
