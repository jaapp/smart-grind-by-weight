#include "screensaver.h"

#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <src/libs/tjpgd/tjpgd.h>
#include "../../config/constants.h"

namespace {

constexpr uint32_t kWaveTickMs = 40;                    // 25 fps ripple animation
constexpr float kWavePeriodS = 2.4f;                    // Time for one outward wave cycle
constexpr float kWaveLengthPx = 96.0f;                  // Radial distance between crests
constexpr float kSecondWaveFrequency = 1.7f;            // Detail wave relative frequency
constexpr float kTwoPi = 6.28318530f;

constexpr size_t kJpegWorkBufferSize = 4096;            // Recommended by TJpgDec

// TJpgDec streams input from the embedded array and emits RGB888 blocks,
// which are packed into the RGB565 frame buffer
struct JpegDecodeContext {
    const uint8_t* data;
    uint32_t size;
    uint32_t position;
    uint16_t* pixels;
    int width;
};

size_t jpeg_input_cb(JDEC* jd, uint8_t* buffer, size_t length) {
    auto* ctx = static_cast<JpegDecodeContext*>(jd->device);
    uint32_t remaining = ctx->size - ctx->position;
    if (length > remaining) {
        length = remaining;
    }
    if (buffer) {
        memcpy(buffer, ctx->data + ctx->position, length);
    }
    ctx->position += length;
    return length;
}

int jpeg_output_cb(JDEC* jd, void* bitmap, JRECT* rect) {
    auto* ctx = static_cast<JpegDecodeContext*>(jd->device);
    // LVGL's bundled TJpgDec emits B,G,R byte order (see mcu_output in tjpgd.c)
    const uint8_t* bgr = static_cast<const uint8_t*>(bitmap);
    for (int y = rect->top; y <= rect->bottom; y++) {
        uint16_t* row = ctx->pixels + y * ctx->width + rect->left;
        for (int x = rect->left; x <= rect->right; x++) {
            uint16_t b = *bgr++;
            uint16_t g = *bgr++;
            uint16_t r = *bgr++;
            *row++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
    return 1;
}

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

    image_ = lv_image_create(overlay_);
    lv_obj_center(image_);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);

    for (int row = 0; row < kDotRows; row++) {
        for (int col = 0; col < kDotCols; col++) {
            float dx = static_cast<float>(kGridOriginX + col * kDotSpacingPx - SCREENSAVER_MEDIA_WIDTH_PX / 2);
            float dy = static_cast<float>(kGridOriginY + row * kDotSpacingPx - SCREENSAVER_MEDIA_HEIGHT_PX / 2);
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

void ScreensaverOverlay::show(ScreensaverStyle style) {
    if (!overlay_ || visible_) {
        return;
    }

    // Media styles fall back to the ripple when decoding fails (e.g. PSRAM low)
    style_ = style;
    if (style_ != ScreensaverStyle::WAVE && !decode_media(style_)) {
        free_media();
        style_ = ScreensaverStyle::WAVE;
    }

    if (style_ == ScreensaverStyle::WAVE) {
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    } else {
        show_frame(0);
        lv_image_set_src(image_, &display_dsc_);
        lv_obj_clear_flag(image_, LV_OBJ_FLAG_HIDDEN);
    }

    phase_ = 0.0f;
    visible_ = true;
    lv_obj_move_foreground(overlay_);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    if (style_ == ScreensaverStyle::WAVE) {
        timer_ = lv_timer_create(tick_cb, kWaveTickMs, this);
    } else if (style_ == ScreensaverStyle::CAT) {
        timer_ = lv_timer_create(tick_cb, SCREENSAVER_CAT_FRAME_INTERVAL_MS, this);
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
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    free_media();
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

    if (self->style_ == ScreensaverStyle::WAVE) {
        self->phase_ += kTwoPi * (kWaveTickMs / 1000.0f) / kWavePeriodS;
        if (self->phase_ >= kTwoPi) {
            self->phase_ -= kTwoPi;
        }
        lv_obj_invalidate(self->overlay_);
    } else if (self->style_ == ScreensaverStyle::CAT && self->frame_count_ > 0) {
        self->show_frame((self->current_frame_ + 1) % self->frame_count_);
    }
}

void ScreensaverOverlay::show_frame(int index) {
    if (!display_pixels_ || !frame_pixels_[index]) {
        return;
    }
    current_frame_ = index;
    memcpy(display_pixels_, frame_pixels_[index], display_dsc_.data_size);
    lv_obj_invalidate(image_);
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

bool ScreensaverOverlay::decode_media(ScreensaverStyle style) {
    free_media();

    constexpr uint32_t kFrameBytes =
        SCREENSAVER_MEDIA_WIDTH_PX * SCREENSAVER_MEDIA_HEIGHT_PX * 2;

    // The display buffer outlives the media so the image widget's source
    // stays valid even while hidden
    if (!display_pixels_) {
        display_pixels_ = static_cast<uint16_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM));
        if (!display_pixels_) {
            return false;
        }
        lv_memzero(&display_dsc_, sizeof(display_dsc_));
        display_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        display_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        display_dsc_.header.w = SCREENSAVER_MEDIA_WIDTH_PX;
        display_dsc_.header.h = SCREENSAVER_MEDIA_HEIGHT_PX;
        display_dsc_.header.stride = SCREENSAVER_MEDIA_WIDTH_PX * 2;
        display_dsc_.data = reinterpret_cast<const uint8_t*>(display_pixels_);
        display_dsc_.data_size = kFrameBytes;
    }

    int frames_needed = (style == ScreensaverStyle::FLOWER) ? 1 : SCREENSAVER_CAT_FRAME_COUNT;
    for (int i = 0; i < frames_needed; i++) {
        frame_pixels_[i] = static_cast<uint16_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM));
        if (!frame_pixels_[i]) {
            return false;
        }
        const uint8_t* data = (style == ScreensaverStyle::FLOWER) ? screensaver_flower_jpg
                                                                  : screensaver_cat_frames[i];
        uint32_t len = (style == ScreensaverStyle::FLOWER) ? screensaver_flower_jpg_len
                                                           : screensaver_cat_frame_lens[i];
        if (!decode_jpeg(data, len, frame_pixels_[i])) {
            return false;
        }
        frame_count_ = i + 1;
    }
    return true;
}

bool ScreensaverOverlay::decode_jpeg(const uint8_t* data, uint32_t len, uint16_t* pixels) {
    void* work_buffer = heap_caps_malloc(kJpegWorkBufferSize, MALLOC_CAP_INTERNAL);
    if (!work_buffer) {
        return false;
    }

    JpegDecodeContext ctx = {data, len, 0, pixels, SCREENSAVER_MEDIA_WIDTH_PX};
    JDEC jd;
    JRESULT result = jd_prepare(&jd, jpeg_input_cb, work_buffer, kJpegWorkBufferSize, &ctx);
    if (result == JDR_OK &&
        jd.width == SCREENSAVER_MEDIA_WIDTH_PX && jd.height == SCREENSAVER_MEDIA_HEIGHT_PX) {
        result = jd_decomp(&jd, jpeg_output_cb, 0);
    } else if (result == JDR_OK) {
        result = JDR_FMT1;
    }
    heap_caps_free(work_buffer);
    return result == JDR_OK;
}

void ScreensaverOverlay::free_media() {
    for (int i = 0; i < SCREENSAVER_CAT_FRAME_COUNT; i++) {
        if (frame_pixels_[i]) {
            heap_caps_free(frame_pixels_[i]);
            frame_pixels_[i] = nullptr;
        }
    }
    frame_count_ = 0;
    current_frame_ = 0;
}
