#pragma once
#include <lvgl.h>
#include "../assets/screensaver_assets.h"

enum class ScreensaverStyle {
    WAVE = 0,
    CAT = 1,
    FLOWER = 2,
};

// Full-screen idle overlay. Wave renders an animated dot-matrix ripple with a
// custom draw callback; Cat and Flower show embedded JPEG media decoded into
// PSRAM while the overlay is visible. Any press dismisses the overlay and is
// swallowed before it reaches the widgets underneath.

class ScreensaverOverlay {
public:
    void create();
    void show(ScreensaverStyle style);
    void hide();
    bool is_visible() const { return visible_; }

private:
    static constexpr int kDotSpacingPx = 16;
    static constexpr int kDotCols = 17;
    static constexpr int kDotRows = 28;
    static constexpr int kShadeCount = 16;
    static constexpr int kGridOriginX =
        (SCREENSAVER_MEDIA_WIDTH_PX - (kDotCols - 1) * kDotSpacingPx) / 2;
    static constexpr int kGridOriginY =
        (SCREENSAVER_MEDIA_HEIGHT_PX - (kDotRows - 1) * kDotSpacingPx) / 2;

    struct DecodedFrame {
        void* buffer = nullptr;
        lv_image_dsc_t dsc = {};
    };

    static void draw_cb(lv_event_t* e);
    static void pressed_cb(lv_event_t* e);
    static void tick_cb(lv_timer_t* timer);

    void draw_wave(lv_layer_t* layer);
    bool decode_media(ScreensaverStyle style);
    bool decode_jpeg(const uint8_t* data, uint32_t len, DecodedFrame& frame);
    void free_media();

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    ScreensaverStyle style_ = ScreensaverStyle::WAVE;
    bool visible_ = false;
    float phase_ = 0.0f;
    uint16_t dot_distance_px_[kDotCols * kDotRows];
    lv_color_t shade_lut_[kShadeCount];
    DecodedFrame frames_[SCREENSAVER_CAT_FRAME_COUNT];
    int frame_count_ = 0;
    int current_frame_ = 0;
};
