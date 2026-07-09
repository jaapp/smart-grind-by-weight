#include "status_indicator_controller.h"

#include <cstring>
#include "../../config/constants.h"
#include "../../system/diagnostics_controller.h"
#include "../event_bridge_lvgl.h"
#include "../ui_manager.h"

StatusIndicatorController::StatusIndicatorController(UIManager* manager)
    : ui_manager_(manager) {}

void StatusIndicatorController::build() {
    if (!ui_manager_) {
        return;
    }

    if (bar_) {
        return;
    }

    // Persistent top navigation bar spanning the full width. Opaque so screen content
    // shifted beneath it (via layout_below_menubar) never bleeds through.
    bar_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bar_, LV_PCT(100), UI_MENUBAR_HEIGHT_PX);
    lv_obj_align(bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_, 0, 0);
    lv_obj_set_style_radius(bar_, 0, 0);
    lv_obj_set_style_pad_top(bar_, 0, 0);
    lv_obj_set_style_pad_bottom(bar_, 0, 0);
    lv_obj_set_style_pad_left(bar_, 12, 0);
    lv_obj_set_style_pad_right(bar_, 12, 0);
    lv_obj_clear_flag(bar_, LV_OBJ_FLAG_SCROLLABLE);
    // Absolute alignment (not flex) so the status icons stay pinned to the right even
    // when the back arrow is hidden — a hidden flex child would otherwise let the
    // icons collapse to the left.

    // Back arrow (left). Transparent so it reads as an icon, with an enlarged touch area.
    back_button_ = lv_button_create(bar_);
    lv_obj_set_style_bg_opa(back_button_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back_button_, 0, 0);
    lv_obj_set_style_border_width(back_button_, 0, 0);
    lv_obj_set_style_pad_all(back_button_, 4, 0);
    lv_obj_set_ext_click_area(back_button_, 14);
    lv_obj_t* back_label = lv_label_create(back_button_);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(back_label);
    lv_obj_align(back_button_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back_button_, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::MENUBAR_BACK)));
    lv_obj_add_flag(back_button_, LV_OBJ_FLAG_HIDDEN);

    // Screen title, left-aligned next to the back arrow (dots-truncated if long).
    title_label_ = lv_label_create(bar_);
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_DOT);
    lv_label_set_text(title_label_, "");
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    // Right-side status icon cluster (warning left of Bluetooth), transparent container.
    lv_obj_t* icons = lv_obj_create(bar_);
    lv_obj_remove_style_all(icons);
    lv_obj_set_size(icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icons, 12, 0);
    lv_obj_clear_flag(icons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(icons, LV_ALIGN_RIGHT_MID, 0, 0);

    warning_icon_ = lv_label_create(icons);
    lv_label_set_text(warning_icon_, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(warning_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(warning_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);

    ble_status_icon_ = lv_label_create(icons);
    lv_label_set_text(ble_status_icon_, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(ble_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_status_icon_, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_add_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);

    reflow_title();
    update_ble_status_icon();
    update_warning_icon();
}

void StatusIndicatorController::reflow_title() {
    if (!title_label_) return;
    // Title sits just right of the back arrow when it's shown, else at the left edge.
    if (back_visible_ && back_button_) {
        lv_obj_align_to(title_label_, back_button_, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    } else {
        lv_obj_align(title_label_, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

void StatusIndicatorController::set_title(const char* title) {
    if (!title_label_) return;
    const char* text = title ? title : "";
    // Skip no-op updates so the MENU per-tick title mirror doesn't churn the label.
    const char* current = lv_label_get_text(title_label_);
    if (current && strcmp(current, text) == 0) return;
    lv_label_set_text(title_label_, text);
}

void StatusIndicatorController::set_visible(bool visible) {
    if (!bar_) return;
    if (visible) {
        lv_obj_clear_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::set_back_visible(bool visible) {
    back_visible_ = visible;
    if (back_button_) {
        if (visible) {
            lv_obj_clear_flag(back_button_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(back_button_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    reflow_title();
}

void StatusIndicatorController::bring_to_front() {
    if (bar_) lv_obj_move_foreground(bar_);
}

void StatusIndicatorController::update() {
    update_ble_status_icon();
    update_warning_icon();
}

void StatusIndicatorController::update_ble_status_icon() {
    if (!ui_manager_ || !ble_status_icon_) {
        return;
    }

    auto* bluetooth = ui_manager_->bluetooth_manager;
    if (bluetooth && bluetooth->is_enabled()) {
        lv_obj_clear_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(ble_status_icon_,
                                    bluetooth->is_connected() ? lv_color_hex(THEME_COLOR_SUCCESS)
                                                              : lv_color_hex(THEME_COLOR_ACCENT),
                                    0);
    } else {
        lv_obj_add_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
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
