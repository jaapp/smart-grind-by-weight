#pragma once

#include <cstdint>
#include "../config/constants.h"

struct ScreensaverTimingSettings {
    uint16_t idle_timeout_s;
    uint8_t startup_timeout_s;
};

namespace ScreensaverSettings {

constexpr uint16_t kDefaultIdleTimeoutS = USER_SCREEN_AUTO_DIM_TIMEOUT_MS / 1000;
constexpr uint8_t kDefaultStartupTimeoutS = 3;

constexpr uint16_t kMinIdleTimeoutS = 30;
constexpr uint16_t kMaxIdleTimeoutS = 3600;
constexpr uint8_t kMinStartupTimeoutS = 1;
constexpr uint8_t kMaxStartupTimeoutS = 30;

constexpr uint32_t kWeightActivityWindowCapMs = 60000;

ScreensaverTimingSettings load_timing();
bool save_timing(uint16_t idle_timeout_s, uint8_t startup_timeout_s);
bool is_valid_idle_timeout(uint16_t idle_timeout_s);
bool is_valid_startup_timeout(uint8_t startup_timeout_s);
bool is_startup_enabled();
bool is_sleep_enabled();
uint32_t idle_timeout_ms(const ScreensaverTimingSettings& settings);
uint32_t weight_activity_window_ms(const ScreensaverTimingSettings& settings);

}  // namespace ScreensaverSettings
