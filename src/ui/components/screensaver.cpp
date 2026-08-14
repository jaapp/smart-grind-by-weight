#include "screensaver.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include "../../config/constants.h"

// Bold grotesque for route bullets (Arimo Bold, Helvetica-metric); the glyph
// cap height is ~55% of the badge diameter, matching official MTA bullets
LV_FONT_DECLARE(lv_font_bullet_46)

namespace {

constexpr uint32_t kWaveTickMs = 40;                    // 25 fps ripple animation
constexpr uint32_t kTrainsTickMs = 1000;                // Arrivals list refresh check
constexpr float kWavePeriodS = 2.4f;                    // Time for one outward wave cycle
constexpr float kWaveLengthPx = 96.0f;                  // Radial distance between crests
constexpr float kSecondWaveFrequency = 1.7f;            // Detail wave relative frequency
constexpr float kTwoPi = 6.28318530f;

constexpr int kBadgeSizePx = 60;
constexpr int kMaxMinsShown = 3;

} // namespace

void ScreensaverOverlay::create() {
    if (overlay_) {
        return;
    }

    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(overlay_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(overlay_, draw_cb, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_event_cb(overlay_, pressed_cb, LV_EVENT_PRESSED, this);

    for (int row = 0; row < kDotRows; row++) {
        for (int col = 0; col < kDotCols; col++) {
            float dx = static_cast<float>(kGridOriginX + col * kDotSpacingPx - HW_DISPLAY_WIDTH_PX / 2);
            float dy = static_cast<float>(kGridOriginY + row * kDotSpacingPx - HW_DISPLAY_HEIGHT_PX / 2);
            dot_distance_px_[row * kDotCols + col] = static_cast<uint16_t>(sqrtf(dx * dx + dy * dy));
        }
    }

    lv_color_t dark = lv_color_hex(THEME_COLOR_SCREENSAVER_DOT_DARK);
    lv_color_t mid = lv_color_hex(THEME_COLOR_SCREENSAVER_DOT);
    lv_color_t bright = lv_color_hex(THEME_COLOR_SCREENSAVER_DOT_BRIGHT);
    for (int i = 0; i < kShadeCount; i++) {
        int half = kShadeCount / 2;
        if (i < half) {
            shade_lut_[i] = lv_color_mix(mid, dark, (255 * i) / (half - 1));
        } else {
            shade_lut_[i] = lv_color_mix(bright, mid, (255 * (i - half)) / (kShadeCount - half - 1));
        }
    }
}

void ScreensaverOverlay::set_style(ScreensaverStyle style) {
    if (visible_) {
        return;
    }
    style_ = style;
}

void ScreensaverOverlay::show() {
    if (!overlay_ || visible_) {
        return;
    }

    phase_ = 0.0f;
    visible_ = true;
    lv_obj_move_foreground(overlay_);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    if (style_ == ScreensaverStyle::TRAINS) {
        train_data_client.set_polling_active(true);
        refresh_trains(true);
        timer_ = lv_timer_create(tick_cb, kTrainsTickMs, this);
    } else {
        timer_ = lv_timer_create(tick_cb, kWaveTickMs, this);
    }
}

void ScreensaverOverlay::hide() {
    if (!overlay_ || !visible_) {
        return;
    }

    visible_ = false;
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    if (trains_container_) {
        lv_obj_del(trains_container_);
        trains_container_ = nullptr;
    }
    train_data_client.set_polling_active(false);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void ScreensaverOverlay::draw_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self || !self->visible_ || self->style_ != ScreensaverStyle::WAVE) {
        return;
    }
    self->draw_wave(lv_event_get_layer(e));
}

void ScreensaverOverlay::pressed_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (self) {
        self->hide();
    }
}

void ScreensaverOverlay::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_timer_get_user_data(timer));
    if (!self || !self->visible_) {
        return;
    }

    if (self->style_ == ScreensaverStyle::TRAINS) {
        self->refresh_trains(false);
        return;
    }

    self->phase_ += kTwoPi * (kWaveTickMs / 1000.0f) / kWavePeriodS;
    if (self->phase_ >= kTwoPi) {
        self->phase_ -= kTwoPi;
    }
    lv_obj_invalidate(self->overlay_);
}

void ScreensaverOverlay::draw_wave(lv_layer_t* layer) {
    constexpr float kWaveNumber = kTwoPi / kWaveLengthPx;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;
    dsc.bg_opa = LV_OPA_COVER;

    for (int row = 0; row < kDotRows; row++) {
        for (int col = 0; col < kDotCols; col++) {
            float distance = static_cast<float>(dot_distance_px_[row * kDotCols + col]);
            float s = 0.5f + 0.35f * sinf(distance * kWaveNumber - phase_) +
                      0.15f * sinf(distance * kWaveNumber * kSecondWaveFrequency - phase_ * 1.3f);
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;

            dsc.bg_color = shade_lut_[static_cast<int>(s * (kShadeCount - 1) + 0.5f)];
            int radius = 1 + static_cast<int>(s * 3.0f + 0.5f);
            int cx = kGridOriginX + col * kDotSpacingPx;
            int cy = kGridOriginY + row * kDotSpacingPx;

            lv_area_t area;
            area.x1 = cx - radius;
            area.y1 = cy - radius;
            area.x2 = cx + radius;
            area.y2 = cy + radius;
            lv_draw_rect(layer, &dsc, &area);
        }
    }
}

void ScreensaverOverlay::refresh_trains(bool force) {
    TrainArrivals arrivals;
    bool have_data = train_data_client.get_arrivals(arrivals);
    NetworkState state = train_data_client.get_state();

    if (!force && arrivals.fetched_at_ms == rendered_fetch_ms_ && state == rendered_state_) {
        return;
    }
    rendered_fetch_ms_ = arrivals.fetched_at_ms;
    rendered_state_ = state;

    rebuild_trains_view(arrivals, have_data);
}

void ScreensaverOverlay::rebuild_trains_view(const TrainArrivals& arrivals, bool have_data) {
    if (trains_container_) {
        lv_obj_del(trains_container_);
        trains_container_ = nullptr;
    }

    trains_container_ = lv_obj_create(overlay_);
    lv_obj_set_size(trains_container_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(trains_container_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(trains_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trains_container_, 0, 0);
    lv_obj_set_style_pad_ver(trains_container_, 16, 0);
    lv_obj_set_style_pad_hor(trains_container_, 10, 0);
    lv_obj_set_layout(trains_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(trains_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(trains_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(trains_container_, 22, 0);
    lv_obj_clear_flag(trains_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(trains_container_, LV_OBJ_FLAG_CLICKABLE);

    if (!have_data || arrivals.item_count == 0) {
        const char* message = "No upcoming trains";
        NetworkState state = train_data_client.get_state();
        if (!train_data_client.has_config()) {
            message = "WiFi not set up\n\nRun:\ngrinder.py wifi";
        } else if (!have_data && state == NetworkState::ERROR) {
            message = "Gateway\nunreachable";
        } else if (!have_data) {
            message = "Loading trains...";
        }
        lv_obj_t* label = lv_label_create(trains_container_);
        lv_label_set_text(label, message);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (int i = 0; i < arrivals.item_count; i++) {
        const TrainArrivalItem& item = arrivals.items[i];

        lv_obj_t* row = lv_obj_create(trains_container_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(row, 12, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* badge = lv_obj_create(row);
        lv_obj_set_size(badge, kBadgeSizePx, kBadgeSizePx);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(item.color), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* badge_label = lv_label_create(badge);
        lv_label_set_text(badge_label, item.route);
        lv_obj_set_style_text_font(badge_label, &lv_font_bullet_46, 0);
        lv_obj_set_style_text_color(badge_label, lv_color_hex(item.text_color), 0);
        lv_obj_center(badge_label);

        lv_obj_t* text_column = lv_obj_create(row);
        lv_obj_set_size(text_column, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(text_column, 1);
        lv_obj_set_style_bg_opa(text_column, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_column, 0, 0);
        lv_obj_set_style_pad_all(text_column, 0, 0);
        lv_obj_set_layout(text_column, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(text_column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_gap(text_column, 2, 0);
        lv_obj_clear_flag(text_column, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(text_column, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* direction_label = lv_label_create(text_column);
        lv_label_set_text(direction_label, item.direction);
        lv_obj_set_style_text_font(direction_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(direction_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_width(direction_label, LV_PCT(100));
        lv_label_set_long_mode(direction_label, LV_LABEL_LONG_DOT);

        char mins_text[48];
        if (item.mins_count == 0) {
            snprintf(mins_text, sizeof(mins_text), "--");
        } else {
            int shown = item.mins_count < kMaxMinsShown ? item.mins_count : kMaxMinsShown;
            size_t pos = 0;
            for (int m = 0; m < shown && pos < sizeof(mins_text) - 8; m++) {
                pos += snprintf(mins_text + pos, sizeof(mins_text) - pos, "%s%u",
                                m == 0 ? "" : ", ", item.mins[m]);
            }
            snprintf(mins_text + pos, sizeof(mins_text) - pos, " min");
        }
        lv_obj_t* mins_label = lv_label_create(text_column);
        lv_label_set_text(mins_label, mins_text);
        lv_obj_set_style_text_font(mins_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(mins_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_width(mins_label, LV_PCT(100));
        lv_label_set_long_mode(mins_label, LV_LABEL_LONG_DOT);
    }

    if (arrivals.gateway_stale) {
        lv_obj_t* stale_label = lv_label_create(trains_container_);
        lv_label_set_text(stale_label, LV_SYMBOL_WARNING " stale data");
        lv_obj_set_style_text_font(stale_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(stale_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    }
}
