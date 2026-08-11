#pragma once
#include <Preferences.h>
#include "../config/constants.h"
#include "grind_mode.h"

class ProfileController {
private:
    float target_weight;
    float target_time_s;
    GrindMode current_grind_mode;
    int active_tab;
    Preferences* preferences;

    void migrate_legacy_profiles();

public:
    void init(Preferences* prefs);
    void load_targets();
    void save_targets();

    float get_current_weight() const { return target_weight; }
    float get_current_time() const { return target_time_s; }

    void update_current_weight(float weight);
    void update_current_time(float seconds);

    // Active ready-screen tab persistence (menu tab is never persisted)
    void set_active_tab(int tab);
    int get_active_tab() const { return active_tab; }

    // Weight validation methods - single authority for all weight constraints
    bool is_weight_valid(float weight) const;
    float clamp_weight(float weight) const;

    bool is_time_valid(float seconds) const;
    float clamp_time(float seconds) const;

    // Grind mode persistence methods
    void set_grind_mode(GrindMode mode);
    GrindMode get_grind_mode() const { return current_grind_mode; }
    void save_grind_mode();
};
