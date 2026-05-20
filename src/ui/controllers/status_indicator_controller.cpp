#include "status_indicator_controller.h"

#include "../../config/constants.h"
#include "../../system/diagnostics_controller.h"
#include "../ui_manager.h"

StatusIndicatorController::StatusIndicatorController(UIManager* manager)
    : ui_manager_(manager) {}

void StatusIndicatorController::build() {
    if (!ui_manager_) {
        return;
    }

    if (connectivity_status_icon_) {
        return;
    }

    // Create connectivity status icon (rightmost)
    connectivity_status_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(connectivity_status_icon_, "WiFi");
    lv_obj_set_style_text_font(connectivity_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(connectivity_status_icon_, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_align(connectivity_status_icon_, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(connectivity_status_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(connectivity_status_icon_, LV_OBJ_FLAG_CLICKABLE);

    // Create warning icon (left of connectivity icon)
    warning_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(warning_icon_, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(warning_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(warning_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(warning_icon_, LV_ALIGN_TOP_RIGHT, -45, 10);
    lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_CLICKABLE);

    update_connectivity_status_icon();
    update_warning_icon();
}

void StatusIndicatorController::update() {
    update_connectivity_status_icon();
    update_warning_icon();
}

void StatusIndicatorController::update_connectivity_status_icon() {
    if (!ui_manager_ || !connectivity_status_icon_) {
        return;
    }

    auto* connectivity = ui_manager_->connectivity_manager;
    if (connectivity && connectivity->is_enabled()) {
        lv_obj_clear_flag(connectivity_status_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(connectivity_status_icon_,
                                    connectivity->is_connected() ? lv_color_hex(THEME_COLOR_SUCCESS)
                                                                 : lv_color_hex(THEME_COLOR_ACCENT),
                                    0);
    } else {
        lv_obj_add_flag(connectivity_status_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::update_warning_icon() {
    if (!ui_manager_ || !warning_icon_) {
        return;
    }

    // Check if there are any diagnostic warnings
    if (ui_manager_->diagnostics_controller_) {
        DiagnosticCode diagnostic = ui_manager_->diagnostics_controller_->get_highest_priority_warning();
        if (diagnostic != DiagnosticCode::NONE) {
            lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}
