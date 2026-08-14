#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "../../network/train_data_client.h"

// Full-screen idle overlay with two selectable styles: an animated dot-matrix
// ripple (Wave) and a list of upcoming subway arrivals fetched from the train
// gateway (Trains). Any press dismisses the overlay and is swallowed before it
// reaches the widgets underneath.

enum class ScreensaverStyle {
    WAVE = 0,
    TRAINS = 1,
};

// How the Trains style lays out arrivals: one entry per tracked watch with a
// pill row of upcoming times, or a flat time-sorted board with one row per train
enum class TrainsLayout {
    GROUPED = 0,
    BOARD = 1,
};

class ScreensaverOverlay {
public:
    void create();
    void show();
    void hide();
    void set_style(ScreensaverStyle style);
    void set_trains_layout(TrainsLayout layout);
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
    static void tick_cb(lv_timer_t* timer);

    void draw_wave(lv_layer_t* layer);
    void refresh_trains(bool force);
    void rebuild_trains_view(const TrainArrivals& arrivals, bool have_data,
                             uint32_t elapsed_min, bool device_stale);
    int build_grouped_rows(const TrainArrivals& arrivals, uint32_t elapsed_min);
    int build_board_rows(const TrainArrivals& arrivals, uint32_t elapsed_min, bool stale);

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* trains_container_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    bool visible_ = false;
    ScreensaverStyle style_ = ScreensaverStyle::WAVE;
    TrainsLayout trains_layout_ = TrainsLayout::GROUPED;
    float phase_ = 0.0f;
    uint32_t rendered_fetch_ms_ = 0;
    NetworkState rendered_state_ = NetworkState::UNCONFIGURED;
    uint32_t rendered_elapsed_min_ = 0;
    uint8_t rendered_staleness_ = 0;
    uint16_t dot_distance_px_[kDotCols * kDotRows];
    lv_color_t shade_lut_[kShadeCount];
};
