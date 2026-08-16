#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../network/train_data_client.h"

// Full-screen idle overlay: a horizontal tileview of three pages - an animated
// dot-matrix ripple (Wave), subway arrivals from the train gateway grouped per
// tracked watch (Trains grouped), and a flat time-sorted arrivals board
// (Trains board). Tapping the screen's left or right edge slides to the
// neighboring page (the shown page is persisted); a tap anywhere else
// dismisses the overlay. All touches are swallowed before they reach the
// widgets underneath.
// The grouped view fits four watches per screen; more than that are split
// across balanced pages that auto-rotate every 5s with a fade - no manual
// scrolling.

enum class ScreensaverVariant {
    WAVE = 0,
    TRAINS_GROUPED = 1,
    TRAINS_BOARD = 2,
};

class ScreensaverOverlay {
public:
    void create();
    void show();
    void hide();
    bool is_visible() const { return visible_; }

private:
    // Odd counts on both axes place one dot exactly at screen center,
    // so the ripple emanates from a single dot
    static constexpr int kDotSpacingPx = 16;
    static constexpr int kDotCols = 17;
    static constexpr int kDotRows = 27;
    static constexpr int kShadeCount = 16;
    static constexpr int kGridOriginX =
        (HW_DISPLAY_WIDTH_PX - (kDotCols - 1) * kDotSpacingPx) / 2;
    static constexpr int kGridOriginY =
        (HW_DISPLAY_HEIGHT_PX - (kDotRows - 1) * kDotSpacingPx) / 2;

    static void draw_cb(lv_event_t* e);
    static void pressed_cb(lv_event_t* e);
    static void clicked_cb(lv_event_t* e);
    static void tick_cb(lv_timer_t* timer);

    void start_variant();
    void stop_variant();
    void set_variant(ScreensaverVariant variant);
    void step_variant(int direction);
    void draw_wave(lv_layer_t* layer);
    void refresh_trains(bool force);
    void rebuild_trains_views(const TrainArrivals& arrivals, bool have_data,
                              uint32_t elapsed_min, bool device_stale,
                              bool fade_grouped);
    int build_grouped_rows(lv_obj_t* parent, const TrainArrivals& arrivals,
                           uint32_t elapsed_min);
    int build_board_rows(lv_obj_t* parent, const TrainArrivals& arrivals,
                         uint32_t elapsed_min, bool stale);

    lv_obj_t* overlay_ = nullptr;           // lv_tileview
    lv_obj_t* wave_tile_ = nullptr;
    lv_obj_t* grouped_tile_ = nullptr;
    lv_obj_t* board_tile_ = nullptr;
    lv_obj_t* grouped_container_ = nullptr;
    lv_obj_t* board_container_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    bool visible_ = false;
    ScreensaverVariant variant_ = ScreensaverVariant::WAVE;
    lv_point_t press_point_ = {0, 0};
    float phase_ = 0.0f;
    uint32_t rendered_fetch_ms_ = 0;
    NetworkState rendered_state_ = NetworkState::UNCONFIGURED;
    uint32_t rendered_elapsed_min_ = 0;
    uint8_t rendered_staleness_ = 0;
    uint8_t grouped_page_ = 0;
    uint8_t grouped_page_count_ = 1;
    uint32_t grouped_page_shown_ms_ = 0;
    uint16_t dot_distance_px_[kDotCols * kDotRows];
    lv_color_t shade_lut_[kShadeCount];
};
