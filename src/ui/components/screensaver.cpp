#include "screensaver.h"

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../../config/constants.h"

// Bold grotesque for route bullets (Arimo Bold, Helvetica-metric); the glyph
// cap height is ~55% of the badge diameter, matching official MTA bullets
LV_FONT_DECLARE(lv_font_bullet_40)

namespace {

constexpr uint32_t kWaveTickMs = 40;                    // 25 fps ripple animation
constexpr uint32_t kTrainsTickMs = 1000;                // Arrivals list refresh check
constexpr float kWavePeriodS = 2.4f;                    // Time for one outward wave cycle
constexpr float kWaveLengthPx = 96.0f;                  // Radial distance between crests
constexpr float kSecondWaveFrequency = 1.7f;            // Detail wave relative frequency
constexpr float kTwoPi = 6.28318530f;

constexpr int kVariantCount = 3;

// Taps starting within this distance of the screen's left/right edge switch
// pages instead of dismissing
constexpr int kEdgeTapZonePx = 48;

constexpr int kBadgeSizePx = 52;
constexpr int kMaxGroupedRows = 4;
constexpr int kMaxBoardRows = 7;

// Watches beyond one grouped page rotate through automatically; no manual
// scrolling since the screensaver only takes taps
constexpr uint32_t kGroupedPageHoldMs = 5000;
constexpr uint32_t kGroupedPageFadeMs = 200;
constexpr int kPageDotSizePx = 7;

// The bullet font's glyphs are all cap-height and sit on the baseline, leaving
// the font's 8px descent as empty space below them; shift down by half of it
// so the glyph is visually centered in the badge
constexpr int kBadgeGlyphNudgePx = 4;

struct WatchEntry {
    const TrainArrivalItem* item;
    uint8_t mins[NET_MAX_ARRIVAL_MINS];
    uint8_t count;
};

struct BoardEntry {
    const TrainArrivalItem* item;
    uint8_t min;
};

// Countdown color for an arrival, judged against the watch's walk-to-platform
// estimate: green when reachable at a normal pace, yellow when only a rushed
// walk (NET_WALK_RUSH_PERCENT of the normal time) still makes it, red when the
// train can't be caught. Watches without an estimate keep the standard color.
uint32_t arrival_countdown_color(uint8_t walk_min, uint8_t mins) {
    if (walk_min == 0) {
        return THEME_COLOR_TEXT_PRIMARY;
    }
    if (mins >= walk_min) {
        return THEME_COLOR_SCREENSAVER_CATCH_OK;
    }
    uint8_t rushed_walk_min =
        static_cast<uint8_t>((walk_min * NET_WALK_RUSH_PERCENT + 99) / 100);
    return mins >= rushed_walk_min ? THEME_COLOR_SCREENSAVER_CATCH_RUSH
                                   : THEME_COLOR_SCREENSAVER_CATCH_MISS;
}

lv_obj_t* make_flex_container(lv_obj_t* parent, lv_flex_flow_t flow, int32_t gap) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(obj, gap, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

// Full-tile column that holds one trains page; non-clickable so taps land on
// the tile underneath
lv_obj_t* make_trains_page(lv_obj_t* tile, int32_t gap) {
    lv_obj_t* page = lv_obj_create(tile);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_ver(page, 16, 0);
    lv_obj_set_style_pad_left(page, 2, 0);
    lv_obj_set_style_pad_right(page, 0, 0);
    lv_obj_set_layout(page, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(page, gap, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
    return page;
}

void make_route_badge(lv_obj_t* parent, const TrainArrivalItem& item) {
    lv_obj_t* badge = lv_obj_create(parent);
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
    lv_obj_set_style_text_font(badge_label, &lv_font_bullet_40, 0);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(item.text_color), 0);
    lv_obj_align(badge_label, LV_ALIGN_CENTER, 0, kBadgeGlyphNudgePx);
}

// Direction + optional station, stacked; grows to fill the space between the
// badge and whatever sits at the row's right edge
void make_watch_text_column(lv_obj_t* row, const TrainArrivalItem& item) {
    lv_obj_t* text_col = make_flex_container(row, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_width(text_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_col, 1);

    lv_obj_t* direction_label = lv_label_create(text_col);
    lv_label_set_text(direction_label, item.direction);
    lv_obj_set_size(direction_label, LV_PCT(100), lv_font_get_line_height(&lv_font_montserrat_24));
    lv_obj_set_style_text_font(direction_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(direction_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_label_set_long_mode(direction_label, LV_LABEL_LONG_DOT);

    if (item.station[0] != '\0') {
        lv_obj_t* station_label = lv_label_create(text_col);
        lv_label_set_text(station_label, item.station);
        lv_obj_set_size(station_label, LV_PCT(100), lv_font_get_line_height(&lv_font_montserrat_20));
        lv_obj_set_style_text_font(station_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(station_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_label_set_long_mode(station_label, LV_LABEL_LONG_DOT);
    }
}

// Page-position dots for the grouped view's rotation, pinned inside the page's
// bottom padding and excluded from the flex layout so a full 4-row page plus
// the stale marker still fits above them
void make_page_dots(lv_obj_t* page, int page_index, int page_count) {
    lv_obj_t* dots = make_flex_container(page, LV_FLEX_FLOW_ROW, 7);
    lv_obj_set_width(dots, LV_SIZE_CONTENT);
    lv_obj_add_flag(dots, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, 12);

    for (int i = 0; i < page_count; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_set_size(dot, kPageDotSizePx, kPageDotSizePx);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        uint32_t color = i == page_index ? THEME_COLOR_TEXT_PRIMARY : THEME_COLOR_SCREENSAVER_PILL_BG;
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
}

// Empty/loading/error message when a page has no rows, or the stale marker
// beneath the rows it does have
void add_trains_status(lv_obj_t* page, bool have_data, int rows, bool stale) {
    if (rows == 0) {
        const char* message = "No upcoming trains";
        NetworkState state = train_data_client.get_state();
        if (!train_data_client.has_config()) {
            message = "WiFi not set up\n\nRun:\ngrinder.py wifi";
        } else if (!have_data && state == NetworkState::ERROR) {
            message = "Gateway\nunreachable";
        } else if (!have_data) {
            message = "Loading trains...";
        }
        lv_obj_t* label = lv_label_create(page);
        lv_label_set_text(label, message);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    if (stale) {
        lv_obj_t* stale_label = lv_label_create(page);
        lv_label_set_text(stale_label, LV_SYMBOL_WARNING " stale data");
        lv_obj_set_style_text_font(stale_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(stale_label, lv_color_hex(THEME_COLOR_WARNING), 0);
    }
}

} // namespace

void ScreensaverOverlay::create() {
    if (overlay_) {
        return;
    }

    overlay_ = lv_tileview_create(lv_layer_top());
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(overlay_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_set_scrollbar_mode(overlay_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    // Tiles are not draggable; edge taps slide between them programmatically
    lv_obj_t** tiles[] = {&wave_tile_, &grouped_tile_, &board_tile_};
    for (int i = 0; i < kVariantCount; i++) {
        lv_obj_t* tile = lv_tileview_add_tile(overlay_, i, 0, LV_DIR_NONE);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_add_event_cb(tile, pressed_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(tile, clicked_cb, LV_EVENT_CLICKED, this);
        *tiles[i] = tile;
    }
    lv_obj_add_event_cb(wave_tile_, draw_cb, LV_EVENT_DRAW_MAIN, this);

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

void ScreensaverOverlay::show() {
    if (!overlay_ || visible_) {
        return;
    }

    Preferences prefs;
    prefs.begin("screensaver", true);
    // Devices provisioned before variants existed carry the legacy style/layout keys
    int fallback = prefs.getInt("style", 0) == 1 ? 1 + prefs.getInt("layout", 0) : 0;
    int variant = prefs.getInt("variant", fallback);
    prefs.end();
    if (variant < 0 || variant >= kVariantCount) {
        variant = 0;
    }
    variant_ = static_cast<ScreensaverVariant>(variant);

    visible_ = true;
    grouped_page_ = 0;
    grouped_page_shown_ms_ = millis();
    lv_tileview_set_tile_by_index(overlay_, variant, 0, LV_ANIM_OFF);
    lv_obj_move_foreground(overlay_);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    // Build both trains pages up front so a page slide never reveals a blank neighbor
    refresh_trains(true);
    start_variant();
}

void ScreensaverOverlay::hide() {
    if (!overlay_ || !visible_) {
        return;
    }

    visible_ = false;
    stop_variant();
    if (grouped_container_) {
        lv_obj_del(grouped_container_);
        grouped_container_ = nullptr;
    }
    if (board_container_) {
        lv_obj_del(board_container_);
        board_container_ = nullptr;
    }
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void ScreensaverOverlay::start_variant() {
    if (variant_ == ScreensaverVariant::WAVE) {
        phase_ = 0.0f;
        timer_ = lv_timer_create(tick_cb, kWaveTickMs, this);
    } else {
        train_data_client.set_polling_active(true);
        grouped_page_shown_ms_ = millis();
        refresh_trains(true);
        timer_ = lv_timer_create(tick_cb, kTrainsTickMs, this);
    }
}

void ScreensaverOverlay::stop_variant() {
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    train_data_client.set_polling_active(false);
}

void ScreensaverOverlay::set_variant(ScreensaverVariant variant) {
    stop_variant();
    variant_ = variant;

    Preferences prefs;
    prefs.begin("screensaver", false);
    prefs.putInt("variant", static_cast<int>(variant));
    prefs.end();

    start_variant();
}

// Slides one page left (-1) or right (+1); taps past either end do nothing
void ScreensaverOverlay::step_variant(int direction) {
    int index = static_cast<int>(variant_) + direction;
    if (index < 0 || index >= kVariantCount) {
        return;
    }
    set_variant(static_cast<ScreensaverVariant>(index));
    lv_tileview_set_tile_by_index(overlay_, index, 0, LV_ANIM_ON);
}

void ScreensaverOverlay::draw_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self || !self->visible_) {
        return;
    }
    self->draw_wave(lv_event_get_layer(e));
}

void ScreensaverOverlay::pressed_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    lv_indev_get_point(lv_indev_active(), &self->press_point_);
}

// Where the finger first landed decides the action, like a button, so a
// touch that wanders after pressing an edge still pages
void ScreensaverOverlay::clicked_cb(lv_event_t* e) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_event_get_user_data(e));
    if (!self || !self->visible_) {
        return;
    }

    if (self->press_point_.x < kEdgeTapZonePx) {
        self->step_variant(-1);
    } else if (self->press_point_.x >= HW_DISPLAY_WIDTH_PX - kEdgeTapZonePx) {
        self->step_variant(1);
    } else {
        self->hide();
    }
}

void ScreensaverOverlay::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<ScreensaverOverlay*>(lv_timer_get_user_data(timer));
    if (!self || !self->visible_) {
        return;
    }

    if (self->variant_ != ScreensaverVariant::WAVE) {
        self->refresh_trains(false);
        return;
    }

    self->phase_ += kTwoPi * (kWaveTickMs / 1000.0f) / kWavePeriodS;
    if (self->phase_ >= kTwoPi) {
        self->phase_ -= kTwoPi;
    }
    lv_obj_invalidate(self->wave_tile_);
}

void ScreensaverOverlay::draw_wave(lv_layer_t* layer) {
    constexpr float kWaveNumber = kTwoPi / kWaveLengthPx;

    // Anchor the grid to the tile's on-screen coordinates so the dots ride
    // along while the tileview slides between pages
    lv_area_t coords;
    lv_obj_get_coords(wave_tile_, &coords);

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
            int cx = coords.x1 + kGridOriginX + col * kDotSpacingPx;
            int cy = coords.y1 + kGridOriginY + row * kDotSpacingPx;

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

    // Minutes are counted down locally between polls, and a snapshot the
    // gateway stopped refreshing is first flagged as stale, then hidden
    uint32_t age_ms = have_data ? millis() - arrivals.fetched_at_ms : 0;
    uint32_t elapsed_min = age_ms / 60000;
    uint8_t staleness = age_ms >= NET_DATA_EXPIRED_MS ? 2 : (age_ms >= NET_DATA_STALE_MS ? 1 : 0);

    // The grouped view rotates through its pages on a fixed cadence; a flip
    // forces a rebuild even when the data itself hasn't changed
    bool flip = grouped_page_count_ > 1 &&
                millis() - grouped_page_shown_ms_ >= kGroupedPageHoldMs;
    if (flip) {
        grouped_page_++;
        grouped_page_shown_ms_ = millis();
    }

    if (!force && !flip && arrivals.fetched_at_ms == rendered_fetch_ms_ && state == rendered_state_ &&
        elapsed_min == rendered_elapsed_min_ && staleness == rendered_staleness_) {
        return;
    }
    rendered_fetch_ms_ = arrivals.fetched_at_ms;
    rendered_state_ = state;
    rendered_elapsed_min_ = elapsed_min;
    rendered_staleness_ = staleness;

    rebuild_trains_views(arrivals, have_data && staleness < 2, elapsed_min, staleness == 1, flip);
}

// Both trains pages are rebuilt together so whichever one a page slide
// reveals is already populated
void ScreensaverOverlay::rebuild_trains_views(const TrainArrivals& arrivals, bool have_data,
                                              uint32_t elapsed_min, bool device_stale,
                                              bool fade_grouped) {
    if (grouped_container_) {
        lv_obj_del(grouped_container_);
        grouped_container_ = nullptr;
    }
    if (board_container_) {
        lv_obj_del(board_container_);
        board_container_ = nullptr;
    }

    grouped_container_ = make_trains_page(grouped_tile_, 16);
    board_container_ = make_trains_page(board_tile_, 8);

    bool stale = arrivals.gateway_stale || device_stale;
    // With no data there is nothing to page through, so stop the rotation
    // rather than letting a stale page count force rebuilds every cadence
    if (!have_data) {
        grouped_page_ = 0;
        grouped_page_count_ = 1;
    }
    int grouped_rows = have_data ? build_grouped_rows(grouped_container_, arrivals, elapsed_min) : 0;
    int board_rows = have_data ? build_board_rows(board_container_, arrivals, elapsed_min, stale) : 0;

    add_trains_status(grouped_container_, have_data, grouped_rows, stale);
    add_trains_status(board_container_, have_data, board_rows, stale);

    if (fade_grouped) {
        lv_obj_fade_in(grouped_container_, kGroupedPageFadeMs, 0);
    }
}

// One entry per watch, in gateway order: bullet + destination/station, then a
// full-width pill row with every arrival. Trains arriving right now show as
// 0m; only trains the local countdown has pushed past due are dropped, and
// watches left empty are hidden.
// Watches beyond one screenful are split across pages that refresh_trains
// rotates through automatically, with dots marking the current page.
int ScreensaverOverlay::build_grouped_rows(lv_obj_t* parent, const TrainArrivals& arrivals,
                                           uint32_t elapsed_min) {
    WatchEntry entries[NET_MAX_ARRIVAL_ITEMS];
    int entry_count = 0;
    for (int i = 0; i < arrivals.item_count; i++) {
        const TrainArrivalItem& item = arrivals.items[i];
        WatchEntry entry = {&item, {}, 0};
        for (int m = 0; m < item.mins_count; m++) {
            if (item.mins[m] < elapsed_min) {
                continue;
            }
            entry.mins[entry.count++] = static_cast<uint8_t>(item.mins[m] - elapsed_min);
        }
        if (entry.count > 0) {
            entries[entry_count++] = entry;
        }
    }

    // Pages are balanced so five watches show as 3+2 rather than 4+1; the page
    // index wraps here both on rotation and when watches drop out of the feed
    int pages = entry_count > 0 ? (entry_count + kMaxGroupedRows - 1) / kMaxGroupedRows : 1;
    grouped_page_count_ = static_cast<uint8_t>(pages);
    if (grouped_page_ >= pages) {
        grouped_page_ = 0;
    }
    int per_page = (entry_count + pages - 1) / pages;
    int first = grouped_page_ * per_page;
    int last = std::min(entry_count, first + per_page);

    for (int i = first; i < last; i++) {
        const WatchEntry& entry = entries[i];
        const TrainArrivalItem& item = *entry.item;

        lv_obj_t* entry_box = make_flex_container(parent, LV_FLEX_FLOW_COLUMN, 4);

        lv_obj_t* row = make_flex_container(entry_box, LV_FLEX_FLOW_ROW, 12);
        make_route_badge(row, item);
        make_watch_text_column(row, item);

        lv_obj_t* pills_row = make_flex_container(entry_box, LV_FLEX_FLOW_ROW, 6);
        for (int m = 0; m < entry.count; m++) {
            char pill_text[8];
            snprintf(pill_text, sizeof(pill_text), "%um", entry.mins[m]);

            lv_obj_t* pill = lv_obj_create(pills_row);
            lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(pill, lv_color_hex(THEME_COLOR_SCREENSAVER_PILL_BG), 0);
            lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pill, 0, 0);
            lv_obj_set_style_pad_hor(pill, 10, 0);
            lv_obj_set_style_pad_ver(pill, 2, 0);
            lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* pill_label = lv_label_create(pill);
            lv_label_set_text(pill_label, pill_text);
            lv_obj_set_style_text_font(pill_label, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(
                pill_label, lv_color_hex(arrival_countdown_color(item.walk_min, entry.mins[m])), 0);
        }
    }

    if (pages > 1) {
        make_page_dots(parent, grouped_page_, pages);
    }
    return last - first;
}

// Flat departure board: one row per upcoming train sorted by arrival time,
// with a big countdown on the right. Trains arriving right now show as 0m;
// only trains the local countdown has pushed past due are dropped.
int ScreensaverOverlay::build_board_rows(lv_obj_t* parent, const TrainArrivals& arrivals,
                                         uint32_t elapsed_min, bool stale) {
    BoardEntry entries[NET_MAX_ARRIVAL_ITEMS * NET_MAX_ARRIVAL_MINS];
    int entry_count = 0;
    for (int i = 0; i < arrivals.item_count; i++) {
        const TrainArrivalItem& item = arrivals.items[i];
        for (int m = 0; m < item.mins_count; m++) {
            if (item.mins[m] < elapsed_min) {
                continue;
            }
            entries[entry_count++] = {&item, static_cast<uint8_t>(item.mins[m] - elapsed_min)};
        }
    }

    std::stable_sort(entries, entries + entry_count,
                     [](const BoardEntry& a, const BoardEntry& b) { return a.min < b.min; });

    int rows = entry_count < kMaxBoardRows ? entry_count : kMaxBoardRows;
    if (stale && rows == kMaxBoardRows) {
        rows--;
    }
    for (int i = 0; i < rows; i++) {
        const TrainArrivalItem& item = *entries[i].item;

        lv_obj_t* row = make_flex_container(parent, LV_FLEX_FLOW_ROW, 12);
        make_route_badge(row, item);
        make_watch_text_column(row, item);

        char mins_text[16];
        snprintf(mins_text, sizeof(mins_text), "%um", entries[i].min);
        lv_obj_t* mins_label = lv_label_create(row);
        lv_label_set_text(mins_label, mins_text);
        lv_obj_set_style_text_font(mins_label, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(
            mins_label, lv_color_hex(arrival_countdown_color(item.walk_min, entries[i].min)), 0);
    }
    return rows;
}
