#include "profile_controller.h"
#include <Arduino.h>
#include <string.h>

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
    
    if (current_profile < 0 || current_profile >= USER_PROFILE_COUNT) {
        current_profile = 1;
    }
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
