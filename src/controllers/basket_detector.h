#pragma once

#include <Preferences.h>
#include "../config/constants.h"

enum class BasketDetectionResult {
    SINGLE,
    DOUBLE,
    NO_MATCH,
    AMBIGUOUS,
    UNCONFIGURED
};

class BasketDetector {
public:
    static constexpr const char* PREF_KEY_ENABLED = "basket_detect";
    static constexpr const char* PREF_KEY_SINGLE_WEIGHT_G = "basket_single_g";
    static constexpr const char* PREF_KEY_DOUBLE_WEIGHT_G = "basket_double_g";
    static constexpr const char* PREF_KEY_TOLERANCE_G = "basket_tol_g";

    void init(Preferences* preferences);
    void load();

    void save_enabled(bool enabled);
    void save_single_weight(float weight_g);
    void save_double_weight(float weight_g);
    void save_tolerance(float tolerance_g);

    BasketDetectionResult classify(float settled_weight_g) const;
    bool is_configured() const;

    bool is_enabled() const { return enabled_; }
    float get_single_weight() const { return single_basket_weight_g_; }
    float get_double_weight() const { return double_basket_weight_g_; }
    float get_tolerance() const { return tolerance_g_; }
    bool has_single_weight() const { return single_basket_weight_g_ > 0.0f; }
    bool has_double_weight() const { return double_basket_weight_g_ > 0.0f; }

private:
    static float sanitize_weight(float weight_g);
    static float clamp_tolerance(float tolerance_g);

    Preferences* preferences_ = nullptr;
    bool enabled_ = false;
    float single_basket_weight_g_ = 0.0f;
    float double_basket_weight_g_ = 0.0f;
    float tolerance_g_ = USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G;
};
