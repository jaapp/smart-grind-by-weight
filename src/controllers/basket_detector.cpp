#include "basket_detector.h"

#include <cmath>

void BasketDetector::init(Preferences* preferences) {
    preferences_ = preferences;
    load();
}

void BasketDetector::load() {
    if (!preferences_) {
        enabled_ = false;
        single_basket_weight_g_ = 0.0f;
        double_basket_weight_g_ = 0.0f;
        tolerance_g_ = USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G;
        return;
    }

    enabled_ = preferences_->getBool(PREF_KEY_ENABLED, false);
    single_basket_weight_g_ = sanitize_weight(preferences_->getFloat(PREF_KEY_SINGLE_WEIGHT_G, 0.0f));
    double_basket_weight_g_ = sanitize_weight(preferences_->getFloat(PREF_KEY_DOUBLE_WEIGHT_G, 0.0f));
    tolerance_g_ = clamp_tolerance(preferences_->getFloat(PREF_KEY_TOLERANCE_G, USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G));
}

void BasketDetector::save_enabled(bool enabled) {
    enabled_ = enabled;
    if (preferences_) {
        preferences_->putBool(PREF_KEY_ENABLED, enabled_);
    }
}

void BasketDetector::save_single_weight(float weight_g) {
    single_basket_weight_g_ = sanitize_weight(weight_g);
    if (preferences_) {
        preferences_->putFloat(PREF_KEY_SINGLE_WEIGHT_G, single_basket_weight_g_);
    }
}

void BasketDetector::save_double_weight(float weight_g) {
    double_basket_weight_g_ = sanitize_weight(weight_g);
    if (preferences_) {
        preferences_->putFloat(PREF_KEY_DOUBLE_WEIGHT_G, double_basket_weight_g_);
    }
}

void BasketDetector::save_tolerance(float tolerance_g) {
    tolerance_g_ = clamp_tolerance(tolerance_g);
    if (preferences_) {
        preferences_->putFloat(PREF_KEY_TOLERANCE_G, tolerance_g_);
    }
}

BasketDetectionResult BasketDetector::classify(float settled_weight_g) const {
    if (!is_configured()) {
        return BasketDetectionResult::UNCONFIGURED;
    }

    const float weight_g = sanitize_weight(settled_weight_g);
    const bool single_match = std::fabs(weight_g - single_basket_weight_g_) <= tolerance_g_;
    const bool double_match = std::fabs(weight_g - double_basket_weight_g_) <= tolerance_g_;

    if (single_match && double_match) {
        return BasketDetectionResult::AMBIGUOUS;
    }
    if (single_match) {
        return BasketDetectionResult::SINGLE;
    }
    if (double_match) {
        return BasketDetectionResult::DOUBLE;
    }
    return BasketDetectionResult::NO_MATCH;
}

bool BasketDetector::is_configured() const {
    return has_single_weight() && has_double_weight();
}

float BasketDetector::sanitize_weight(float weight_g) {
    if (!std::isfinite(weight_g)) {
        return 0.0f;
    }
    const float absolute_weight_g = std::fabs(weight_g);
    if (absolute_weight_g <= 0.0f) {
        return 0.0f;
    }
    return absolute_weight_g;
}

float BasketDetector::clamp_tolerance(float tolerance_g) {
    if (!std::isfinite(tolerance_g)) {
        return USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G;
    }
    if (tolerance_g < USER_BASKET_DETECTION_TOLERANCE_MIN_G) {
        return USER_BASKET_DETECTION_TOLERANCE_MIN_G;
    }
    if (tolerance_g > USER_BASKET_DETECTION_TOLERANCE_MAX_G) {
        return USER_BASKET_DETECTION_TOLERANCE_MAX_G;
    }
    return tolerance_g;
}
