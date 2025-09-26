#pragma once
#include <Preferences.h>
#include "../config/constants.h"

struct Profile {
    char name[USER_PROFILE_NAME_MAX_LENGTH];
    float weight;
    float time_seconds;
};

class ProfileController {
private:
    Profile profiles[USER_PROFILE_COUNT];
    int current_profile;
    Preferences* preferences;

public:
    void init(Preferences* prefs);
    void load_profiles();
    void save_profiles();
    void save_current_profile();
    
    void set_current_profile(int index);
    int get_current_profile() const { return current_profile; }
    float get_current_weight() const { return profiles[current_profile].weight; }
    float get_current_time() const { return profiles[current_profile].time_seconds; }
    const char* get_current_name() const { return profiles[current_profile].name; }
    
    void set_profile_weight(int index, float weight);
    float get_profile_weight(int index) const;
    const char* get_profile_name(int index) const;
    void set_profile_time(int index, float seconds);
    float get_profile_time(int index) const;
    
    void update_current_weight(float weight);
    void update_current_time(float seconds);
    
    // Weight validation methods - single authority for all weight constraints
    bool is_weight_valid(float weight) const;
    float clamp_weight(float weight) const;

    bool is_time_valid(float seconds) const;
    float clamp_time(float seconds) const;
};
