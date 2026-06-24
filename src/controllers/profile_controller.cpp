#include "profile_controller.h"
#include "arduino_compat.h"
#include <string.h>
#include <cmath>
#include "preferences_idf.h"

void ProfileController::init(Preferences* prefs) {
    preferences = prefs;
    
    // Initialize default profiles
    strcpy(profiles[0].name, "SINGLE");
    profiles[0].weight = USER_SINGLE_ESPRESSO_WEIGHT_G;
    profiles[0].time_seconds = USER_SINGLE_ESPRESSO_TIME_S;
    
    strcpy(profiles[1].name, "DOUBLE");
    profiles[1].weight = USER_DOUBLE_ESPRESSO_WEIGHT_G;
    profiles[1].time_seconds = USER_DOUBLE_ESPRESSO_TIME_S;
    
    strcpy(profiles[2].name, "CUSTOM");
    profiles[2].weight = USER_CUSTOM_PROFILE_WEIGHT_G;
    profiles[2].time_seconds = USER_CUSTOM_PROFILE_TIME_S;
    
    // Initialize default grind mode
    current_grind_mode = GrindMode::WEIGHT;
    
    load_profiles();
}

void ProfileController::load_profiles() {
    current_profile = preferences->getInt("profile", 1);
    
    profiles[0].weight = preferences->getFloat("weight0", USER_SINGLE_ESPRESSO_WEIGHT_G);
    profiles[1].weight = preferences->getFloat("weight1", USER_DOUBLE_ESPRESSO_WEIGHT_G);
    profiles[2].weight = preferences->getFloat("weight2", USER_CUSTOM_PROFILE_WEIGHT_G);

    profiles[0].time_seconds = preferences->getFloat("time0", USER_SINGLE_ESPRESSO_TIME_S);
    profiles[1].time_seconds = preferences->getFloat("time1", USER_DOUBLE_ESPRESSO_TIME_S);
    profiles[2].time_seconds = preferences->getFloat("time2", USER_CUSTOM_PROFILE_TIME_S);
    
    // Load grind mode (default to WEIGHT if not set, validate range)
    int stored_mode = preferences->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
    if (stored_mode < 0 || stored_mode > static_cast<int>(GrindMode::CALIBRATED_TIME)) {
        stored_mode = static_cast<int>(GrindMode::WEIGHT);
    }
    current_grind_mode = static_cast<GrindMode>(stored_mode);

    if (current_profile < 0 || current_profile >= USER_PROFILE_COUNT) {
        current_profile = 1;
    }

    load_calibration();
}

void ProfileController::save_profiles() {
    preferences->putFloat("weight0", profiles[0].weight);
    preferences->putFloat("weight1", profiles[1].weight);
    preferences->putFloat("weight2", profiles[2].weight);

    preferences->putFloat("time0", profiles[0].time_seconds);
    preferences->putFloat("time1", profiles[1].time_seconds);
    preferences->putFloat("time2", profiles[2].time_seconds);
}

void ProfileController::save_current_profile() {
    preferences->putInt("profile", current_profile);
    save_profiles();
}

void ProfileController::set_current_profile(int index) {
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        current_profile = index;
        save_current_profile();
    }
}

void ProfileController::set_profile_weight(int index, float weight) {
    if (index >= 0 && index < USER_PROFILE_COUNT && is_weight_valid(weight)) {
        profiles[index].weight = weight;
        save_profiles();
    }
}

float ProfileController::get_profile_weight(int index) const {
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        return profiles[index].weight;
    }
    return 0.0f;
}

const char* ProfileController::get_profile_name(int index) const {
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        return profiles[index].name;
    }
    return "UNKNOWN";
}

void ProfileController::set_profile_time(int index, float seconds) {
    if (index >= 0 && index < USER_PROFILE_COUNT && is_time_valid(seconds)) {
        profiles[index].time_seconds = seconds;
        save_profiles();
    }
}

float ProfileController::get_profile_time(int index) const {
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        return profiles[index].time_seconds;
    }
    return 0.0f;
}

bool ProfileController::is_weight_valid(float weight) const {
    return weight >= USER_MIN_TARGET_WEIGHT_G && weight <= USER_MAX_TARGET_WEIGHT_G;
}

float ProfileController::clamp_weight(float weight) const {
    if (weight < USER_MIN_TARGET_WEIGHT_G) return USER_MIN_TARGET_WEIGHT_G;
    if (weight > USER_MAX_TARGET_WEIGHT_G) return USER_MAX_TARGET_WEIGHT_G;
    return weight;
}

void ProfileController::update_current_weight(float weight) {
    if (is_weight_valid(weight)) {
        profiles[current_profile].weight = weight;
    }
}

void ProfileController::update_current_time(float seconds) {
    if (is_time_valid(seconds)) {
        profiles[current_profile].time_seconds = seconds;
    }
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
    current_grind_mode = mode;
    save_grind_mode();
}

void ProfileController::save_grind_mode() {
    preferences->putInt("grind_mode", static_cast<int>(current_grind_mode));
}

// ---------------------------------------------------------------------------
// Calibrated Time mode — per-profile flow rate calibration
// ---------------------------------------------------------------------------

void ProfileController::load_calibration() {
    const char* flow_keys[] = {"cal_flow0", "cal_flow1", "cal_flow2"};
    const char* count_keys[] = {"cal_cnt0", "cal_cnt1", "cal_cnt2"};

    for (int i = 0; i < USER_PROFILE_COUNT; i++) {
        calibration[i].flow_rate_gps = preferences->getFloat(flow_keys[i], GRIND_PULSE_FLOW_RATE_FALLBACK_GPS);
        calibration[i].grind_count = static_cast<uint16_t>(preferences->getInt(count_keys[i], 0));
    }
}

void ProfileController::save_calibration(int index) {
    if (index < 0 || index >= USER_PROFILE_COUNT) return;

    const char* flow_keys[] = {"cal_flow0", "cal_flow1", "cal_flow2"};
    const char* count_keys[] = {"cal_cnt0", "cal_cnt1", "cal_cnt2"};

    preferences->putFloat(flow_keys[index], calibration[index].flow_rate_gps);
    preferences->putInt(count_keys[index], static_cast<int>(calibration[index].grind_count));
}

float ProfileController::get_calibrated_flow_rate(int index) const {
    if (index < 0 || index >= USER_PROFILE_COUNT) return GRIND_PULSE_FLOW_RATE_FALLBACK_GPS;
    return calibration[index].flow_rate_gps;
}

uint16_t ProfileController::get_calibration_count(int index) const {
    if (index < 0 || index >= USER_PROFILE_COUNT) return 0;
    return calibration[index].grind_count;
}

float ProfileController::get_calibrated_time(int index) const {
    if (index < 0 || index >= USER_PROFILE_COUNT) return 0.0f;
    float flow = calibration[index].flow_rate_gps;
    if (flow < GRIND_FLOW_RATE_MIN_SANE_GPS) flow = GRIND_PULSE_FLOW_RATE_FALLBACK_GPS;
    return profiles[index].weight / flow;
}

void ProfileController::update_calibration(int index, float measured_flow_rate) {
    if (index < 0 || index >= USER_PROFILE_COUNT) return;
    if (measured_flow_rate < GRIND_FLOW_RATE_MIN_SANE_GPS ||
        measured_flow_rate > GRIND_FLOW_RATE_MAX_SANE_GPS) return;

    auto& cal = calibration[index];
    constexpr float kMaxSamples = 10.0f;
    float alpha = 1.0f / fminf(static_cast<float>(cal.grind_count + 1), kMaxSamples);
    cal.flow_rate_gps = alpha * measured_flow_rate + (1.0f - alpha) * cal.flow_rate_gps;
    cal.grind_count++;
    save_calibration(index);
}

void ProfileController::reset_calibration(int index) {
    if (index < 0 || index >= USER_PROFILE_COUNT) return;
    calibration[index].flow_rate_gps = GRIND_PULSE_FLOW_RATE_FALLBACK_GPS;
    calibration[index].grind_count = 0;
    save_calibration(index);
}
