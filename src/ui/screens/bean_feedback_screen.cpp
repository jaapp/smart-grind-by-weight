#include "bean_feedback_screen.h"

#include "../../config/constants.h"
#include "../../controllers/bean_controller.h"
#include "../event_bridge_lvgl.h"

namespace {

lv_obj_t* make_feedback_button(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color,
                               EventBridgeLVGL::EventType event_type) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 82, 82);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(event_type)));

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_size(label, 78, 58);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_line_space(label, 4, 0);
    lv_obj_center(label);
    return btn;
}

} // namespace

void BeanFeedbackScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* done = lv_obj_create(screen);
    lv_obj_set_size(done, 58, 58);
    lv_obj_align(done, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_radius(done, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(done, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_set_style_border_width(done, 0, 0);
    lv_obj_clear_flag(done, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* done_label = lv_label_create(done);
    lv_label_set_text(done_label, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(done_label, &lv_font_montserrat_24, 0);
    lv_obj_center(done_label);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Next grind size");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 88);

    gs_label = lv_label_create(screen);
    lv_label_set_text(gs_label, "--");
    lv_obj_set_style_text_font(gs_label, &lv_font_montserrat_60, 0);
    lv_obj_set_style_text_color(gs_label, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_align(gs_label, LV_ALIGN_TOP_MID, 0, 116);

    bean_label = lv_label_create(screen);
    lv_label_set_text(bean_label, "");
    lv_obj_set_width(bean_label, 250);
    lv_label_set_long_mode(bean_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(bean_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(bean_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(bean_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(bean_label, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t* row = lv_obj_create(screen);
    lv_obj_set_size(row, 260, 86);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -84);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_feedback_button(row, "-0.5\nFiner", &lv_font_montserrat_16, lv_color_hex(0x1F1F1F),
                         EventBridgeLVGL::EventType::BEAN_FEEDBACK_FINER);
    make_feedback_button(row, "OK", &lv_font_montserrat_24, lv_color_hex(THEME_COLOR_SUCCESS),
                         EventBridgeLVGL::EventType::BEAN_FEEDBACK_OK);
    make_feedback_button(row, "+0.5\nCoarser", &lv_font_montserrat_16, lv_color_hex(0x1F1F1F),
                         EventBridgeLVGL::EventType::BEAN_FEEDBACK_COARSER);

    lv_obj_t* skip = lv_btn_create(screen);
    lv_obj_set_size(skip, 260, 58);
    lv_obj_align(skip, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_color(skip, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_radius(skip, 8, 0);
    lv_obj_set_style_border_width(skip, 0, 0);
    lv_obj_add_event_cb(skip, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::BEAN_FEEDBACK_SKIP)));

    lv_obj_t* skip_label = lv_label_create(skip);
    lv_label_set_text(skip_label, "Skip");
    lv_obj_set_style_text_font(skip_label, &lv_font_montserrat_24, 0);
    lv_obj_center(skip_label);

    hide();
}

void BeanFeedbackScreen::update(const char* bean_name, uint16_t mahlgrad_x2) {
    char gs[12];
    BeanController::format_mahlgrad(gs, sizeof(gs), mahlgrad_x2);
    lv_label_set_text(gs_label, gs);
    lv_label_set_text(bean_label, bean_name ? bean_name : "");
}

void BeanFeedbackScreen::show() {
    if (!screen) return;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void BeanFeedbackScreen::hide() {
    if (!screen) return;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}
