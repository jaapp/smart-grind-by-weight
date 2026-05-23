#include "screensaver_controller.h"
#include "../../config/constants.h"
#include "../../config/logging.h"
#include "../../system/screensaver_settings.h"
#include <algorithm>
#include <cstring>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

ScreensaverController::ScreensaverController()
    : overlay_screen_(nullptr)
    , previous_screen_(nullptr)
    , image_widget_(nullptr)
    , image_buffer_(nullptr)
    , visible_(false) {
    memset(&image_dsc_, 0, sizeof(image_dsc_));
}

ScreensaverController::~ScreensaverController() {
    hide();
}

bool ScreensaverController::has_image() const {
    return LittleFS.exists(BLE_IMAGE_FILENAME);
}

bool ScreensaverController::is_startup_enabled() const {
    Preferences prefs;
    prefs.begin("screensaver", true);
    bool enabled = prefs.getBool("startup", false);
    prefs.end();
    return enabled;
}

bool ScreensaverController::is_sleep_enabled() const {
    Preferences prefs;
    prefs.begin("screensaver", true);
    bool enabled = prefs.getBool("sleep", false);
    prefs.end();
    return enabled;
}

uint32_t ScreensaverController::get_startup_timeout_ms() const {
    auto settings = ScreensaverSettings::load_timing();
    return static_cast<uint32_t>(settings.startup_timeout_s) * 1000U;
}

void ScreensaverController::show() {
    if (visible_) return;
    if (!has_image()) return;

    if (!load_image()) {
        return;
    }

    // Save the current active screen so we can restore it on hide().
    // Screens in this project are created once and never deleted, so
    // this pointer remains valid for the lifetime of the application.
    previous_screen_ = lv_scr_act();

    // Create a new screen for the overlay
    overlay_screen_ = lv_obj_create(nullptr);
    if (!overlay_screen_) {
        free_image();
        return;
    }
    lv_obj_set_style_bg_color(overlay_screen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_screen_, LV_OPA_COVER, 0);

    // Create image widget filling the screen
    image_widget_ = lv_img_create(overlay_screen_);
    if (!image_widget_) {
        lv_obj_delete(overlay_screen_);
        overlay_screen_ = nullptr;
        free_image();
        return;
    }

    // Set up image descriptor for raw RGB565 data
    image_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    image_dsc_.header.w = HW_DISPLAY_WIDTH_PX;
    image_dsc_.header.h = HW_DISPLAY_HEIGHT_PX;
    image_dsc_.data_size = BLE_IMAGE_EXPECTED_SIZE;
    image_dsc_.data = image_buffer_;

    lv_img_set_src(image_widget_, &image_dsc_);
    lv_obj_center(image_widget_);

    // Load the overlay screen
    lv_screen_load(overlay_screen_);
    visible_ = true;
}

void ScreensaverController::hide() {
    if (!visible_) return;

    // Restore the previous active screen before deleting the overlay.
    if (previous_screen_) {
        lv_screen_load(previous_screen_);
        previous_screen_ = nullptr;
    }

    if (overlay_screen_) {
        lv_obj_delete(overlay_screen_);
        overlay_screen_ = nullptr;
        image_widget_ = nullptr;
    }

    free_image();
    visible_ = false;
}

bool ScreensaverController::load_image() {
    image_buffer_ = (uint8_t*)heap_caps_malloc(BLE_IMAGE_EXPECTED_SIZE,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!image_buffer_) {
        LOG_BLE("Screensaver: PSRAM allocation failed (%d bytes)\n", BLE_IMAGE_EXPECTED_SIZE);
        return false;
    }

    File f = LittleFS.open(BLE_IMAGE_FILENAME, "r");
    if (!f) {
        LOG_BLE("Screensaver: Failed to open image file\n");
        free_image();
        return false;
    }

    size_t total_read = 0;
    const size_t chunk_size = 4096;
    while (total_read < BLE_IMAGE_EXPECTED_SIZE) {
        size_t to_read = std::min(chunk_size, (size_t)(BLE_IMAGE_EXPECTED_SIZE - total_read));
        size_t bytes_read = f.read(image_buffer_ + total_read, to_read);
        if (bytes_read == 0) break;
        total_read += bytes_read;
    }
    f.close();

    if (total_read != BLE_IMAGE_EXPECTED_SIZE) {
        LOG_BLE("Screensaver: Image file size mismatch (%u != %d)\n",
                (unsigned)total_read, BLE_IMAGE_EXPECTED_SIZE);
        free_image();
        return false;
    }

    return true;
}

void ScreensaverController::free_image() {
    if (image_buffer_) {
        heap_caps_free(image_buffer_);
        image_buffer_ = nullptr;
    }
}
