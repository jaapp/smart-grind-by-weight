#pragma once
#include <lvgl.h>
#include "../../config/constants.h"

// Full-screen idle overlay rendering an animated dot-matrix ripple with a
// custom draw callback. Any press dismisses the overlay and is swallowed
// before it reaches the widgets underneath.

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
    static void tick_cb(lv_timer_t* timer);

    void draw_wave(lv_layer_t* layer);

    lv_obj_t* overlay_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    bool visible_ = false;
    float phase_ = 0.0f;
    uint16_t dot_distance_px_[kDotCols * kDotRows];
    lv_color_t shade_lut_[kShadeCount];
};
