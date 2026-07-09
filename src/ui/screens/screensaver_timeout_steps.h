#pragma once
#include <cstdint>
#include "../../config/constants.h"

// Discrete screensaver "Sleep after" timeout steps mapped to the slider positions (0..count-1).
// The final step is the "Never" sentinel which disables the screensaver entirely.
// Shared by the menu screen (slider setup/restore) and the menu controller (event handling)
// so the mapping lives in exactly one place.
inline constexpr uint32_t kScreensaverTimeoutStepsMs[] = {
    15000, 30000, 60000, 120000, 300000, 600000, 900000, 1800000,
    USER_SCREEN_SAVER_TIMEOUT_NEVER_MS
};
inline constexpr int kScreensaverTimeoutStepCount =
    sizeof(kScreensaverTimeoutStepsMs) / sizeof(kScreensaverTimeoutStepsMs[0]);
inline constexpr int kScreensaverTimeoutDefaultIndex = 4; // 300000 ms (5 min)

// Map a stored timeout (ms) to the nearest slider index, falling back to the default.
inline int screensaver_timeout_ms_to_index(uint32_t timeout_ms) {
    for (int i = 0; i < kScreensaverTimeoutStepCount; i++) {
        if (kScreensaverTimeoutStepsMs[i] == timeout_ms) {
            return i;
        }
    }
    return kScreensaverTimeoutDefaultIndex;
}
