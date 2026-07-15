#include "grinding_controller.h"

#include "arduino_compat.h"
#include <cstdio>
#include <cstring>

#include "../../config/constants.h"
#include "../../controllers/grind_events.h"
#include "../../controllers/grind_mode.h"
#include "../../logging/grind_logging.h"
#include "../ui_manager.h"

GrindingUIController* GrindingUIController::instance_ = nullptr;

// Vertical offset of the bottom action button(s) from the screen bottom.
// Raised to leave the very bottom edge free for the page-indicator dots.
static constexpr int kButtonBottomY = -34;

GrindingUIController::GrindingUIController(UIManager* manager)
    : ui_manager_(manager) {
    instance_ = this;
}

void GrindingUIController::build_controls() {
    if (!ui_manager_) {
        return;
    }

    grind_button_ = lv_btn_create(lv_scr_act());
    lv_obj_set_size(grind_button_, 100, 100);
    lv_obj_align(grind_button_, LV_ALIGN_BOTTOM_MID, -60, kButtonBottomY);
    lv_obj_set_style_radius(grind_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(grind_button_, 0, 0);
    lv_obj_set_style_shadow_width(grind_button_, 0, 0);

    grind_icon_ = lv_img_create(grind_button_);
    lv_img_set_src(grind_icon_, LV_SYMBOL_PLAY);
    lv_obj_center(grind_icon_);
    lv_obj_set_style_text_font(grind_icon_, &lv_font_montserrat_24, 0);

    pulse_button_ = lv_btn_create(lv_scr_act());
    lv_obj_set_size(pulse_button_, 100, 100);
    lv_obj_align(pulse_button_, LV_ALIGN_BOTTOM_MID, 60, -10);
    lv_obj_set_style_radius(pulse_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(pulse_button_, 0, 0);
    lv_obj_set_style_shadow_width(pulse_button_, 0, 0);

    pulse_icon_ = lv_img_create(pulse_button_);
    lv_img_set_src(pulse_icon_, LV_SYMBOL_PLUS);
    lv_obj_center(pulse_icon_);
    lv_obj_set_style_text_font(pulse_icon_, &lv_font_montserrat_32, 0);

    lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
}

void GrindingUIController::register_events() {
    if (!ui_manager_) {
        return;
    }

    if (grind_button_) {
        lv_obj_add_event_cb(grind_button_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_grind_button();
            }
        }, LV_EVENT_CLICKED, this);
    }

    // The READY tabs' in-page action buttons (play / settings gear) dispatch through
    // the event bridge; route them to the same grind-button logic as the floater.
    EventBridgeLVGL::register_handler(EventBridgeLVGL::EventType::GRIND_BUTTON,
                                      [this](lv_event_t*) { handle_grind_button(); });

    if (pulse_button_) {
        // LV_EVENT_ALL: a tap drives PURGE_CONFIRM (CLICKED), while time-mode top-off
        // is hold-to-grind, driven by PRESSED / RELEASED / PRESS_LOST.
        lv_obj_add_event_cb(pulse_button_, [](lv_event_t* e) {
            auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e));
            if (controller) {
                controller->handle_pulse_button_event(lv_event_get_code(e));
            }
        }, LV_EVENT_ALL, this);
    }

    if (lv_obj_t* arc = ui_manager_->grinding_screen.get_arc_screen_obj()) {
        lv_obj_add_event_cb(arc, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                if (auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e))) {
                    controller->handle_layout_toggle();
                }
            }
        }, LV_EVENT_CLICKED, this);
    }

    if (lv_obj_t* chart = ui_manager_->grinding_screen.get_chart_screen_obj()) {
        lv_obj_add_event_cb(chart, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                if (auto* controller = static_cast<GrindingUIController*>(lv_event_get_user_data(e))) {
                    controller->handle_layout_toggle();
                }
            }
        }, LV_EVENT_CLICKED, this);
    }

    // NOTE: Purge confirm reuses existing grind_button_ (CANCEL) and pulse_button_ (CONTINUE)
    // No additional event registration needed - handle_pulse_button() checks for PURGE_CONFIRM phase
}

void GrindingUIController::on_state_changed(UIState new_state) {
    if (!ui_manager_) {
        return;
    }

    if (new_state != UIState::GRIND_COMPLETE && grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
        grind_complete_timer_ = nullptr;
    }
    if (new_state != UIState::GRIND_TIMEOUT && grind_timeout_timer_) {
        lv_timer_del(grind_timeout_timer_);
        grind_timeout_timer_ = nullptr;
    }

    switch (new_state) {
        case UIState::READY:
            enter_ready_state();
            break;
        case UIState::EDIT:
            enter_edit_state();
            break;
        case UIState::GRINDING:
            enter_grinding_state();
            break;
        case UIState::GRIND_COMPLETE:
            enter_grind_complete_state();
            break;
        case UIState::GRIND_TIMEOUT:
            enter_grind_timeout_state();
            break;
        case UIState::MENU:
        case UIState::CALIBRATION:
        case UIState::CONFIRM:
        case UIState::OTA_UPDATE:
        case UIState::OTA_UPDATE_FAILED:
            enter_menu_state();
            break;
        default:
            break;
    }

    update_grind_button_icon();
}

void GrindingUIController::update(UIState current_state) {
    if (!ui_manager_) {
        return;
    }

    switch (current_state) {
        case UIState::GRIND_COMPLETE: {
            WeightSensor* weight_sensor = ui_manager_->hardware_manager->get_weight_sensor();
            if (weight_sensor) {
                float current_weight = weight_sensor->get_display_weight();
                ui_manager_->grinding_screen.update_current_weight(current_weight);
            }
            ui_manager_->grinding_screen.update_progress(final_grind_progress_);
            break;
        }
        case UIState::GRIND_TIMEOUT: {
            ui_manager_->grinding_screen.update_current_weight(error_grind_weight_);
            ui_manager_->grinding_screen.update_progress(error_grind_progress_);
            char error_display[64];
            const char* message = error_message_[0] ? error_message_ : "Error";
            std::snprintf(error_display, sizeof(error_display), "%s", message);
            ui_manager_->grinding_screen.update_target_weight_text(error_display);
            break;
        }
        default:
            break;
    }
}

void GrindingUIController::handle_grind_button() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    LOG_BLE("[%lums BUTTON_PRESS] Grind button pressed in state: %s\n",
            millis(), ui_manager_->state_machine->is_state(UIState::READY) ? "READY" :
                       ui_manager_->state_machine->is_state(UIState::GRINDING) ? "GRINDING" :
                       ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) ? "GRIND_COMPLETE" :
                       ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT) ? "GRIND_TIMEOUT" :
                       ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM) ? "PURGE_CONFIRM" : "OTHER");

    if (ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM)) {
        // Cancel grind during purge confirmation
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->stop_grind();
        }
    } else if (ui_manager_->state_machine->is_state(UIState::READY)) {
        if (ui_manager_->current_tab == 3) {
            ui_manager_->switch_to_state(UIState::MENU);
            return;
        }

        if (ui_manager_->grind_controller && ui_manager_->profile_controller) {
            ui_manager_->grind_controller->set_grind_profile_id(ui_manager_->profile_controller->get_current_profile());
        }

        LOG_BLE("[%lums GRIND_START] About to call start_grind()\n", millis());
        error_message_[0] = '\0';
        error_grind_weight_ = 0.0f;
        error_grind_progress_ = 0;

        if (ui_manager_->profile_controller && ui_manager_->grind_controller) {
            float target_weight = ui_manager_->profile_controller->get_current_weight();
            float target_time_seconds = ui_manager_->profile_controller->get_current_time();
            uint32_t target_time_ms = static_cast<uint32_t>((target_time_seconds * 1000.0f) + 0.5f);
            ui_manager_->grind_controller->start_grind(target_weight, target_time_ms, ui_manager_->current_mode);
        }
        LOG_BLE("[%lums GRIND_START] start_grind() returned\n", millis());
    } else if (ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->stop_grind();
        }
    } else if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) ||
               ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        if (ui_manager_->grind_controller) {
            ui_manager_->grind_controller->return_to_idle();
        }
    }
}

void GrindingUIController::handle_pulse_button_event(lv_event_code_t code) {
    if (!ui_manager_ || !ui_manager_->grind_controller) {
        return;
    }

    // PURGE_CONFIRM: the button acts as CONTINUE on a tap (click)
    if (ui_manager_->purge_confirm_screen.is_visible()) {
        if (code == LV_EVENT_CLICKED) {
            handle_purge_confirm_continue();
        }
        return;
    }

    // Time-mode top-off: hold-to-grind. Run the motor while the button is held.
    if (code == LV_EVENT_PRESSED) {
        if (ui_manager_->grind_controller->can_pulse()) {
            LOG_BLE("[UIManager] Top-off hold started\n");
            ui_manager_->grind_controller->start_additional_pulse();
            reset_grind_complete_timer();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (ui_manager_->grind_controller->is_additional_pulse_active()) {
            LOG_BLE("[UIManager] Top-off hold released\n");
            ui_manager_->grind_controller->stop_additional_pulse();
            reset_grind_complete_timer();
        }
    }
}

void GrindingUIController::handle_layout_toggle() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::GRINDING) ||
        ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) ||
        ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        GrindScreenLayout current_layout = ui_manager_->grinding_screen.get_layout();
        if (current_layout == GrindScreenLayout::MINIMAL_ARC) {
            ui_manager_->grinding_screen.set_layout(GrindScreenLayout::NERDY_CHART);
        } else {
            ui_manager_->grinding_screen.set_layout(GrindScreenLayout::MINIMAL_ARC);
        }
    }
}

void GrindingUIController::handle_purge_confirm_continue() {
    if (!ui_manager_ || !ui_manager_->grind_controller) {
        return;
    }

    // Hopper refill pause: the button acts as PLAY - restart the grind
    if (ui_manager_->grind_controller->is_awaiting_refill()) {
        LOG_BLE("[%lums REFILL] User resumed grind after hopper refill\n", millis());
        ui_manager_->purge_confirm_screen.hide();
        ui_manager_->switch_to_state(UIState::GRINDING);
        ui_manager_->grind_controller->resume_from_refill();
        return;
    }

    // Check if "Keep purge grinds from now on" checkbox is checked
    if (ui_manager_->purge_confirm_screen.is_checkbox_checked()) {
        LOG_BLE("[%lums PURGE] User chose to keep grinds - switching to Prime mode\n", millis());

        // Switch grinder purge mode from Purge to Prime in preferences
        auto* hardware = ui_manager_->get_hardware_manager();
        Preferences* prefs = hardware ? hardware->get_preferences() : nullptr;
        if (prefs) {
            prefs->putInt(GrindController::PREF_KEY_GRINDER_MODE, static_cast<int>(GrinderPurgeMode::PRIME));
        }
    }

    // Hide the purge confirmation screen and continue grinding
    ui_manager_->purge_confirm_screen.hide();
    ui_manager_->switch_to_state(UIState::GRINDING);

    // Tell the grind controller to continue from PURGE_CONFIRM to PREDICTIVE
    ui_manager_->grind_controller->continue_from_purge();
}

void GrindingUIController::update_grind_button_icon() {
    if (!ui_manager_ || !grind_button_ || !grind_icon_) {
        return;
    }

    // In READY every tab carries its own in-page action button (sliding with the
    // swipe), so the persistent floater is never shown there.
    if (ui_manager_->state_machine->is_state(UIState::READY)) {
        lv_obj_add_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
        if (pulse_button_) {
            lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::PURGE_CONFIRM)) {
        // During purge confirm, show STOP icon (user can cancel the grind)
        lv_img_set_src(grind_icon_, LV_SYMBOL_STOP);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_ERROR), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::GRINDING)) {
        lv_img_set_src(grind_icon_, LV_SYMBOL_STOP);
        lv_obj_set_style_bg_color(grind_button_,
                                  ui_manager_->current_mode == GrindMode::TIME
                                      ? lv_color_hex(THEME_COLOR_ACCENT)
                                      : lv_color_hex(THEME_COLOR_PRIMARY),
                                  0);
    } else if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE)) {
        lv_img_set_src(grind_icon_, LV_SYMBOL_OK);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    } else if (ui_manager_->state_machine->is_state(UIState::GRIND_TIMEOUT)) {
        lv_img_set_src(grind_icon_, LV_SYMBOL_CLOSE);
        lv_obj_set_style_bg_color(grind_button_, lv_color_hex(THEME_COLOR_WARNING), 0);
    } else {
        lv_img_set_src(grind_icon_, LV_SYMBOL_PLAY);
        lv_obj_set_style_bg_color(grind_button_,
                                  ui_manager_->current_mode == GrindMode::TIME
                                      ? lv_color_hex(THEME_COLOR_ACCENT)
                                      : lv_color_hex(THEME_COLOR_PRIMARY),
                                  0);
    }

    update_button_layout();
}

void GrindingUIController::update_button_layout() {
    if (!ui_manager_ || !grind_button_) {
        return;
    }

    // Check if we're in PURGE_CONFIRM phase (show dual buttons for CANCEL + CONTINUE)
    bool in_purge_confirm = ui_manager_->purge_confirm_screen.is_visible();

    bool should_show_pulse = (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) &&
                              ui_manager_->current_mode == GrindMode::TIME);

    if (in_purge_confirm || should_show_pulse) {
        // Dual button layout: left button at -60, right button at +60
        lv_obj_align(grind_button_, LV_ALIGN_BOTTOM_MID, -60, kButtonBottomY);
        if (pulse_button_) {
            lv_obj_align(pulse_button_, LV_ALIGN_BOTTOM_MID, 60, kButtonBottomY);
            lv_obj_clear_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);

            if (in_purge_confirm) {
                // Grind paused: the button acts as CONTINUE (always enabled).
                // Purge confirm shows OK; hopper refill shows PLAY (resume grinding).
                bool awaiting_refill = ui_manager_->grind_controller &&
                                       ui_manager_->grind_controller->is_awaiting_refill();
                lv_img_set_src(pulse_icon_, awaiting_refill ? LV_SYMBOL_PLAY : LV_SYMBOL_OK);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
                lv_obj_clear_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_COVER, 0);
            } else if (ui_manager_->grind_controller &&
                       (ui_manager_->grind_controller->can_pulse() ||
                        ui_manager_->grind_controller->is_additional_pulse_active())) {
                // Time mode top-off: enabled when a top-off can start, and kept
                // enabled while actively holding to grind (so the hold isn't dropped)
                lv_img_set_src(pulse_icon_, LV_SYMBOL_PLUS);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
                lv_obj_clear_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_COVER, 0);
            } else {
                // Time mode pulse: disabled
                lv_img_set_src(pulse_icon_, LV_SYMBOL_PLUS);
                lv_obj_set_style_bg_color(pulse_button_, lv_color_hex(THEME_COLOR_ACCENT), 0);
                lv_obj_add_state(pulse_button_, LV_STATE_DISABLED);
                lv_obj_set_style_bg_opa(pulse_button_, LV_OPA_50, LV_STATE_DISABLED);
            }
        }
    } else {
        // Single button layout: centered at 0
        lv_obj_align(grind_button_, LV_ALIGN_BOTTOM_MID, 0, kButtonBottomY);
        if (pulse_button_) {
            lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void GrindingUIController::update_grinding_targets() {
    if (!ui_manager_ || !ui_manager_->grind_controller || !ui_manager_->profile_controller) {
        return;
    }

    const auto& session = ui_manager_->grind_controller->get_session_descriptor();
    ui_manager_->grinding_screen.update_target_weight(session.target_weight);
    if (session.mode == GrindMode::TIME && session.target_time_ms > 0) {
        float target_time_seconds = static_cast<float>(session.target_time_ms) / 1000.0f;
        ui_manager_->grinding_screen.update_target_time(target_time_seconds);
    }
}

void GrindingUIController::reset_grind_complete_timer() {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE) && grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
        grind_complete_timer_ = nullptr;
        start_grind_complete_timer();
    }
}

void GrindingUIController::handle_grind_event(const GrindEventData& event_data) {
    if (!ui_manager_ || !ui_manager_->state_machine) {
        return;
    }

    switch (event_data.event) {
        case UIGrindEvent::PHASE_CHANGED: {
            ui_manager_->current_mode = event_data.mode;

            // Grind-paused phases (purge confirm / hopper refill) show the
            // confirmation popup with the dual CANCEL + CONTINUE button layout
            if (event_data.phase == GrindPhase::PURGE_CONFIRM ||
                event_data.phase == GrindPhase::HOPPER_REFILL) {
                LOG_UI_DEBUG("[%lums UI_TRANSITION] Switching to PURGE_CONFIRM state (%s)\n",
                             millis(), event_data.phase_display_text);
                ui_manager_->switch_to_state(UIState::PURGE_CONFIRM);
                update_grind_button_icon();  // Update button icon to STOP and reposition for dual-button layout
            } else if (event_data.phase != GrindPhase::IDLE &&
                       event_data.phase != GrindPhase::TIME_ADDITIONAL_PULSE &&
                       !ui_manager_->state_machine->is_state(UIState::GRINDING)) {
                LOG_UI_DEBUG("[%lums UI_TRANSITION] Switching to GRINDING state due to phase: %s\n",
                             millis(), event_data.phase_display_text);
                WeightSensor* weight_sensor = ui_manager_->hardware_manager->get_weight_sensor();
                ui_manager_->grinding_screen.update_profile_name(ui_manager_->profile_controller->get_current_name());
                ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
                chart_updates_enabled_ = true;
                update_grinding_targets();
                if (weight_sensor) {
                    ui_manager_->grinding_screen.update_current_weight(weight_sensor->get_display_weight());
                }
                ui_manager_->grinding_screen.update_progress(0);
                ui_manager_->switch_to_state(UIState::GRINDING);

                if (event_data.phase == GrindPhase::INITIALIZING) {
                    ui_manager_->grind_controller->ui_acknowledge_phase_transition();
                    LOG_UI_DEBUG("[%lums UI_ACKNOWLEDGMENT] INITIALIZING phase confirmed, ready for SETUP\n", millis());
                }
            }

            if (ui_manager_->state_machine->is_state(UIState::GRIND_COMPLETE)) {
                update_button_layout();
            }

            if (event_data.show_taring_text) {
                ui_manager_->grinding_screen.update_tare_display();
            } else {
                ui_manager_->grinding_screen.update_current_weight(event_data.current_weight);
                ui_manager_->grinding_screen.update_progress(event_data.progress_percent);

                if (chart_updates_enabled_ &&
                    event_data.phase != GrindPhase::IDLE && event_data.phase != GrindPhase::TARING &&
                    event_data.phase != GrindPhase::TARE_CONFIRM && event_data.phase != GrindPhase::INITIALIZING &&
                    event_data.phase != GrindPhase::SETUP && event_data.phase != GrindPhase::COMPLETED &&
                    event_data.phase != GrindPhase::TIMEOUT && event_data.phase != GrindPhase::TIME_ADDITIONAL_PULSE &&
                    event_data.phase != GrindPhase::PURGE_CONFIRM && event_data.phase != GrindPhase::HOPPER_REFILL) {
                    ui_manager_->grinding_screen.add_chart_data_point(event_data.current_weight, event_data.flow_rate, millis());
                }
            }
            break;
        }
        case UIGrindEvent::PROGRESS_UPDATED: {
            if (event_data.show_taring_text) {
                ui_manager_->grinding_screen.update_tare_display();
            } else {
                ui_manager_->current_mode = event_data.mode;
                ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
                ui_manager_->grinding_screen.update_current_weight(event_data.current_weight);
                ui_manager_->grinding_screen.update_progress(event_data.progress_percent);

                if (chart_updates_enabled_ &&
                    event_data.phase != GrindPhase::IDLE && event_data.phase != GrindPhase::TARING &&
                    event_data.phase != GrindPhase::TARE_CONFIRM && event_data.phase != GrindPhase::INITIALIZING &&
                    event_data.phase != GrindPhase::SETUP && event_data.phase != GrindPhase::COMPLETED &&
                    event_data.phase != GrindPhase::TIMEOUT && event_data.phase != GrindPhase::TIME_ADDITIONAL_PULSE &&
                    event_data.phase != GrindPhase::PURGE_CONFIRM && event_data.phase != GrindPhase::HOPPER_REFILL) {
                    ui_manager_->grinding_screen.add_chart_data_point(event_data.current_weight, event_data.flow_rate, millis());
                }
            }
            break;
        }
        case UIGrindEvent::COMPLETED: {
            ui_manager_->current_mode = event_data.mode;
            ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
            final_grind_weight_ = event_data.final_weight;
            final_grind_progress_ = event_data.progress_percent;
            LOG_BLE("GRIND COMPLETE - Final settled weight captured: %.2fg (Progress: %d%%)\n",
                    final_grind_weight_, final_grind_progress_);
            chart_updates_enabled_ = false;
            ui_manager_->switch_to_state(UIState::GRIND_COMPLETE);
            start_grind_complete_timer();
            break;
        }
        case UIGrindEvent::TIMEOUT: {
            ui_manager_->current_mode = event_data.mode;
            ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
            error_grind_weight_ = event_data.error_weight;
            error_grind_progress_ = event_data.error_progress;
            const char* message = event_data.error_message ? event_data.error_message : "Error";
            std::strncpy(error_message_, message, sizeof(error_message_) - 1);
            error_message_[sizeof(error_message_) - 1] = '\0';
            LOG_BLE("GRIND ERROR - %s, Weight: %.2fg (Progress: %d%%)\n",
                    error_message_, error_grind_weight_, error_grind_progress_);
            chart_updates_enabled_ = false;
            ui_manager_->switch_to_state(UIState::GRIND_TIMEOUT);
            start_grind_timeout_timer();
            break;
        }
        case UIGrindEvent::STOPPED: {
            cancel_timers();
            chart_updates_enabled_ = false;
            ui_manager_->switch_to_state(UIState::READY);
            break;
        }
        case UIGrindEvent::BACKGROUND_CHANGE:
#if DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR
        {
            static lv_style_t style_bg;
            static bool style_initialized = false;

            if (!style_initialized) {
                lv_style_init(&style_bg);
                style_initialized = true;
            }

#if defined(DEBUG_ENABLE_LOADCELL_MOCK) && (DEBUG_ENABLE_LOADCELL_MOCK != 0)
            lv_color_t inactive_color = lv_color_hex(THEME_COLOR_BACKGROUND_MOCK);
#else
            lv_color_t inactive_color = lv_color_hex(THEME_COLOR_BACKGROUND);
#endif
            lv_color_t bg_color = event_data.background_active ?
                lv_color_hex(THEME_COLOR_GRINDER_ACTIVE) :
                inactive_color;

            lv_style_set_bg_color(&style_bg, bg_color);
            lv_obj_add_style(lv_scr_act(), &style_bg, 0);

            LOG_UI_DEBUG("[UIManager] Background: %s\n", event_data.background_active ? "ACTIVE" : "INACTIVE");
        }
#endif
            break;
        case UIGrindEvent::PULSE_AVAILABLE:
            LOG_BLE("[UIManager] Pulse available - updating button layout\n");
            update_button_layout();
            break;
        case UIGrindEvent::PULSE_STARTED:
            LOG_BLE("[UIManager] Pulse #%d started (%.1fms)\n",
                    event_data.pulse_count, (float)event_data.pulse_duration_ms);
#if DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR
        {
            static lv_style_t style_bg;
            lv_style_init(&style_bg);
            lv_style_set_bg_color(&style_bg, lv_color_hex(THEME_COLOR_GRINDER_ACTIVE));
            lv_obj_add_style(lv_scr_act(), &style_bg, 0);
        }
#endif
            break;
        case UIGrindEvent::PULSE_COMPLETED:
            LOG_BLE("[UIManager] Pulse #%d completed - weight: %.2fg\n",
                    event_data.pulse_count, event_data.current_weight);
            ui_manager_->grinding_screen.update_current_weight(event_data.current_weight);
            update_button_layout();
            break;
        default:
            break;
    }
}

void GrindingUIController::dispatch_event(const GrindEventData& event_data) {
    if (instance_) {
        instance_->handle_grind_event(event_data);
    }
}

void GrindingUIController::enter_ready_state() {
    if (!grind_button_) {
        return;
    }

    // READY tabs carry their own in-page action buttons (play/gear/TARE+GRIND) that
    // slide with the swipe, so the persistent floater stays hidden until a grind runs.
    lv_obj_add_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    if (pulse_button_) {
        lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
    }
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
}

void GrindingUIController::enter_edit_state() {
    if (grind_button_) {
        lv_obj_add_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    if (pulse_button_) {
        lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::enter_grinding_state() {
    WeightSensor* weight_sensor = ui_manager_->hardware_manager->get_weight_sensor();
    ui_manager_->grinding_screen.reset_chart_data();
    ui_manager_->grinding_screen.update_profile_name(ui_manager_->profile_controller->get_current_name());
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    chart_updates_enabled_ = true;
    update_grinding_targets();
    if (weight_sensor) {
        ui_manager_->grinding_screen.update_current_weight(weight_sensor->get_display_weight());
    }
    ui_manager_->grinding_screen.update_progress(0);
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::enter_grind_complete_state() {
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    ui_manager_->grinding_screen.update_profile_name(ui_manager_->profile_controller->get_current_name());
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    ui_manager_->grinding_screen.update_current_weight(final_grind_weight_);
    ui_manager_->grinding_screen.update_progress(final_grind_progress_);
}

void GrindingUIController::enter_grind_timeout_state() {
    if (grind_button_) {
        lv_obj_clear_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    ui_manager_->grinding_screen.set_mode(ui_manager_->current_mode);
    ui_manager_->grinding_screen.update_profile_name("ERROR");
    char error_display[64];
    const char* message = error_message_[0] ? error_message_ : "Error";
    std::snprintf(error_display, sizeof(error_display), "%s", message);
    ui_manager_->grinding_screen.update_target_weight_text(error_display);
    ui_manager_->grinding_screen.update_current_weight(error_grind_weight_);
    ui_manager_->grinding_screen.update_progress(error_grind_progress_);
}

void GrindingUIController::enter_menu_state() {
    if (grind_button_) {
        lv_obj_add_flag(grind_button_, LV_OBJ_FLAG_HIDDEN);
    }
    if (pulse_button_) {
        lv_obj_add_flag(pulse_button_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingUIController::start_grind_complete_timer() {
    if (grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
    }
    grind_complete_timer_ = lv_timer_create(grind_complete_timer_cb, 60000, this);
    lv_timer_set_repeat_count(grind_complete_timer_, 1);
}

void GrindingUIController::start_grind_timeout_timer() {
    if (grind_timeout_timer_) {
        lv_timer_del(grind_timeout_timer_);
    }
    grind_timeout_timer_ = lv_timer_create(grind_timeout_timer_cb, 60000, this);
    lv_timer_set_repeat_count(grind_timeout_timer_, 1);
}

void GrindingUIController::cancel_timers() {
    if (grind_complete_timer_) {
        lv_timer_del(grind_complete_timer_);
        grind_complete_timer_ = nullptr;
    }
    if (grind_timeout_timer_) {
        lv_timer_del(grind_timeout_timer_);
        grind_timeout_timer_ = nullptr;
    }
}

void GrindingUIController::grind_complete_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<GrindingUIController*>(lv_timer_get_user_data(timer));
    if (!controller || !controller->ui_manager_ || !controller->ui_manager_->grind_controller) {
        return;
    }

    controller->ui_manager_->grind_controller->return_to_idle();
    controller->grind_complete_timer_ = nullptr;
}

void GrindingUIController::grind_timeout_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<GrindingUIController*>(lv_timer_get_user_data(timer));
    if (!controller || !controller->ui_manager_ || !controller->ui_manager_->grind_controller) {
        return;
    }

    controller->ui_manager_->grind_controller->return_to_idle();
    controller->grind_timeout_timer_ = nullptr;
}
