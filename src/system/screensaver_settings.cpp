#include "screensaver_settings.h"

#include <Preferences.h>
#include <algorithm>

namespace {

constexpr const char* kPrefsNamespace = "screensaver";
constexpr const char* kIdleTimeoutKey = "idle_timeout_s";
constexpr const char* kStartupTimeoutKey = "startup_s";
constexpr const char* kStartupEnabledKey = "startup";
constexpr const char* kSleepEnabledKey = "sleep";

bool get_bool_setting(const char* key, bool default_value) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true)) {
        return default_value;
    }
    bool value = prefs.getBool(key, default_value);
    prefs.end();
    return value;
}

}  // namespace

namespace ScreensaverSettings {

bool is_valid_idle_timeout(uint16_t idle_timeout_s) {
    return idle_timeout_s >= kMinIdleTimeoutS && idle_timeout_s <= kMaxIdleTimeoutS;
}

bool is_valid_startup_timeout(uint8_t startup_timeout_s) {
    return startup_timeout_s >= kMinStartupTimeoutS && startup_timeout_s <= kMaxStartupTimeoutS;
}

bool is_startup_enabled() {
    return get_bool_setting(kStartupEnabledKey, false);
}

bool is_sleep_enabled() {
    return get_bool_setting(kSleepEnabledKey, false);
}

ScreensaverTimingSettings load_timing() {
    Preferences prefs;
    ScreensaverTimingSettings settings{
        kDefaultIdleTimeoutS,
        kDefaultStartupTimeoutS,
    };

    if (!prefs.begin(kPrefsNamespace, true)) {
        return settings;
    }

    settings.idle_timeout_s = prefs.getUShort(kIdleTimeoutKey, kDefaultIdleTimeoutS);
    settings.startup_timeout_s = prefs.getUChar(kStartupTimeoutKey, kDefaultStartupTimeoutS);
    prefs.end();

    if (!is_valid_idle_timeout(settings.idle_timeout_s)) {
        settings.idle_timeout_s = kDefaultIdleTimeoutS;
    }
    if (!is_valid_startup_timeout(settings.startup_timeout_s)) {
        settings.startup_timeout_s = kDefaultStartupTimeoutS;
    }

    return settings;
}

bool save_timing(uint16_t idle_timeout_s, uint8_t startup_timeout_s) {
    if (!is_valid_idle_timeout(idle_timeout_s) ||
        !is_valid_startup_timeout(startup_timeout_s)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }

    size_t idle_written = prefs.putUShort(kIdleTimeoutKey, idle_timeout_s);
    size_t startup_written = prefs.putUChar(kStartupTimeoutKey, startup_timeout_s);
    prefs.end();

    return idle_written == sizeof(uint16_t) && startup_written == sizeof(uint8_t);
}

uint32_t idle_timeout_ms(const ScreensaverTimingSettings& settings) {
    return static_cast<uint32_t>(settings.idle_timeout_s) * 1000U;
}

uint32_t weight_activity_window_ms(const ScreensaverTimingSettings& settings) {
    return std::min(idle_timeout_ms(settings), kWeightActivityWindowCapMs);
}

}  // namespace ScreensaverSettings
