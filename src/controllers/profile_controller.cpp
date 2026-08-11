#include "profile_controller.h"
#include <Arduino.h>
#include <Preferences.h>

// Mirrors the tab constants in ReadyScreen
namespace {
constexpr int kTabManual = 0;
constexpr int kTabTime = 1;
constexpr int kTabWeight = 2;
}

void ProfileController::init(Preferences* prefs) {
    preferences = prefs;

    target_weight = USER_DEFAULT_TARGET_WEIGHT_G;
    target_time_s = USER_DEFAULT_TARGET_TIME_S;
    current_grind_mode = GrindMode::WEIGHT;
    active_tab = kTabWeight;

    migrate_legacy_profiles();
    load_targets();
}

void ProfileController::migrate_legacy_profiles() {
    if (preferences->isKey("target_w")) {
        return;
    }

    // Earlier firmware stored three profiles (weight0..2/time0..2) plus the
    // selected index; carry the selected profile's targets over.
    if (preferences->isKey("profile") || preferences->isKey("weight0")) {
        int index = preferences->getInt("profile", 1);
        if (index < 0 || index > 2) {
            index = 1;
        }

        const float weight_defaults[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G,
                                          USER_DOUBLE_ESPRESSO_WEIGHT_G,
                                          USER_CUSTOM_PROFILE_WEIGHT_G};
        const float time_defaults[3] = {USER_SINGLE_ESPRESSO_TIME_S,
                                        USER_DOUBLE_ESPRESSO_TIME_S,
                                        USER_CUSTOM_PROFILE_TIME_S};

        char key[12];
        snprintf(key, sizeof(key), "weight%d", index);
        float migrated_weight = preferences->getFloat(key, weight_defaults[index]);
        snprintf(key, sizeof(key), "time%d", index);
        float migrated_time = preferences->getFloat(key, time_defaults[index]);

        preferences->putFloat("target_w", migrated_weight);
        preferences->putFloat("target_s", migrated_time);

        preferences->remove("profile");
        for (int i = 0; i < 3; i++) {
            snprintf(key, sizeof(key), "weight%d", i);
            preferences->remove(key);
            snprintf(key, sizeof(key), "time%d", i);
            preferences->remove(key);
        }

        // The vertical-swipe mode toggle is gone; drop its namespace.
        Preferences swipe_prefs;
        swipe_prefs.begin("swipe", false);
        swipe_prefs.clear();
        swipe_prefs.end();
    }
}

void ProfileController::load_targets() {
    target_weight = preferences->getFloat("target_w", USER_DEFAULT_TARGET_WEIGHT_G);
    target_time_s = preferences->getFloat("target_s", USER_DEFAULT_TARGET_TIME_S);

    int stored_mode = preferences->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
    current_grind_mode = static_cast<GrindMode>(stored_mode);

    int default_tab = (current_grind_mode == GrindMode::TIME) ? kTabTime : kTabWeight;
    active_tab = preferences->getInt("active_tab", default_tab);
    if (active_tab < kTabManual || active_tab > kTabWeight) {
        active_tab = kTabWeight;
    }
    if (active_tab == kTabTime) {
        current_grind_mode = GrindMode::TIME;
    } else if (active_tab == kTabWeight) {
        current_grind_mode = GrindMode::WEIGHT;
    }
}

void ProfileController::save_targets() {
    preferences->putFloat("target_w", target_weight);
    preferences->putFloat("target_s", target_time_s);
}

void ProfileController::update_current_weight(float weight) {
    if (is_weight_valid(weight)) {
        target_weight = weight;
    }
}

void ProfileController::update_current_time(float seconds) {
    if (is_time_valid(seconds)) {
        target_time_s = seconds;
    }
}

void ProfileController::set_active_tab(int tab) {
    // The menu tab is transient; the device never boots into it.
    if (tab < kTabManual || tab > kTabWeight || tab == active_tab) {
        return;
    }
    active_tab = tab;
    preferences->putInt("active_tab", active_tab);
}

bool ProfileController::is_weight_valid(float weight) const {
    return weight >= USER_MIN_TARGET_WEIGHT_G && weight <= USER_MAX_TARGET_WEIGHT_G;
}

float ProfileController::clamp_weight(float weight) const {
    if (weight < USER_MIN_TARGET_WEIGHT_G) return USER_MIN_TARGET_WEIGHT_G;
    if (weight > USER_MAX_TARGET_WEIGHT_G) return USER_MAX_TARGET_WEIGHT_G;
    return weight;
}

bool ProfileController::is_time_valid(float seconds) const {
    return seconds >= USER_MIN_TARGET_TIME_S && seconds <= USER_MAX_TARGET_TIME_S;
}

float ProfileController::clamp_time(float seconds) const {
    if (seconds < USER_MIN_TARGET_TIME_S) return USER_MIN_TARGET_TIME_S;
    if (seconds > USER_MAX_TARGET_TIME_S) return USER_MAX_TARGET_TIME_S;
    return seconds;
}

void ProfileController::set_grind_mode(GrindMode mode) {
    if (mode == current_grind_mode) {
        return;
    }
    current_grind_mode = mode;
    save_grind_mode();
}

void ProfileController::save_grind_mode() {
    preferences->putInt("grind_mode", static_cast<int>(current_grind_mode));
}
