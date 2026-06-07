#include "bean_list_screen.h"

#include <cstdio>

#include "../../config/constants.h"
#include "../../controllers/bean_controller.h"
#include "../event_bridge_lvgl.h"

namespace {

void clear_box_style(lv_obj_t* obj) {
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void set_label_frame(lv_obj_t* label, int32_t width, int32_t height, const lv_font_t* font,
                     lv_color_t color) {
    lv_obj_set_size(label, width, height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

} // namespace

void BeanListScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    clear_box_style(screen);

    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_set_size(header, 280, 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    clear_box_style(header);

    lv_obj_t* back = lv_btn_create(header);
    lv_obj_set_size(back, 64, 50);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 2);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_add_event_cb(back, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::BEAN_BACK)));

    lv_obj_t* back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, 0);
    lv_obj_center(back_label);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "BEANS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 4, 1);

    count_label = lv_label_create(header);
    lv_label_set_text(count_label, "0/8");
    lv_obj_set_style_text_font(count_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(count_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(count_label, LV_ALIGN_RIGHT_MID, -10, 1);

    list = lv_obj_create(screen);
    lv_obj_set_size(list, 280, 406);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_left(list, 10, 0);
    lv_obj_set_style_pad_right(list, 10, 0);
    lv_obj_set_style_pad_top(list, 4, 0);
    lv_obj_set_style_pad_bottom(list, 16, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    hide();
}

lv_obj_t* BeanListScreen::create_row(lv_obj_t* parent, const BeanController* beans, uint8_t index) {
    const BeanRecord* bean = beans ? beans->get_bean_at(index) : nullptr;
    if (!bean) {
        return nullptr;
    }

    const bool active = beans->get_active_id() == bean->id;
    lv_obj_t* row = lv_btn_create(parent);
    lv_obj_set_size(row, 260, 74);
    lv_obj_set_style_bg_color(row, lv_color_hex(active ? 0x181818 : 0x111111), 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, active ? 2 : 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(active ? THEME_COLOR_ACCENT : 0x2A2A2A), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(bean->id)));
    lv_obj_add_event_cb(row, EventBridgeLVGL::dispatch_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(EventBridgeLVGL::EventType::BEAN_SELECT)));

    if (active) {
        lv_obj_t* marker = lv_obj_create(row);
        lv_obj_set_size(marker, 4, 44);
        lv_obj_align(marker, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(THEME_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_set_style_pad_all(marker, 0, 0);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t* name = lv_label_create(row);
    set_label_frame(name, 156, 24, &lv_font_montserrat_16, lv_color_hex(THEME_COLOR_TEXT_PRIMARY));
    lv_label_set_text(name, bean->name);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 14, 13);

    lv_obj_t* sub = lv_label_create(row);
    set_label_frame(sub, 156, 20, &lv_font_montserrat_14, lv_color_hex(THEME_COLOR_TEXT_SECONDARY));
    lv_label_set_text(sub, bean->roaster[0] ? bean->roaster : "No roaster");
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 14, 39);

    lv_obj_t* divider = lv_obj_create(row);
    lv_obj_set_size(divider, 1, 46);
    lv_obj_align(divider, LV_ALIGN_RIGHT_MID, -77, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(divider, active ? LV_OPA_80 : LV_OPA_50, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    char gs[12];
    BeanController::format_mahlgrad(gs, sizeof(gs), bean->mahlgrad_x2[BeanController::kDoubleProfileIndex]);
    lv_obj_t* gs_label = lv_label_create(row);
    lv_label_set_text(gs_label, gs);
    lv_obj_set_size(gs_label, 68, 40);
    lv_label_set_long_mode(gs_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(gs_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(gs_label, lv_color_hex(active ? THEME_COLOR_ACCENT : THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(gs_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(gs_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    lv_obj_t* caption = lv_label_create(row);
    lv_label_set_text(caption, "Grind");
    lv_obj_set_size(caption, 68, 18);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(caption, LV_ALIGN_TOP_RIGHT, -12, 48);

    return row;
}

void BeanListScreen::update(const BeanController* beans) {
    if (!list) {
        return;
    }
    lv_obj_clean(list);

    char count_text[12];
    const uint8_t count = beans ? beans->count() : 0;
    const uint8_t capacity = beans ? beans->capacity() : BeanController::kMaxBeans;
    std::snprintf(count_text, sizeof(count_text), "%u/%u", count, capacity);
    lv_label_set_text(count_label, count_text);

    if (!beans || count == 0) {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, "Add beans in the web UI");
        lv_obj_set_width(empty, 250);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        create_row(list, beans, i);
    }
}

void BeanListScreen::show() {
    if (!screen) return;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void BeanListScreen::hide() {
    if (!screen) return;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}
