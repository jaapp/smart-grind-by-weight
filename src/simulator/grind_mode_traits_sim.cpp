#include "../controllers/grind_mode_traits.h"

#include <cstdio>

#include "../config/constants.h"

namespace {
constexpr GrindModeTraits kWeightTraits = {
    "Weight",
    "g",
    "Target: ",
    " / ",
    USER_FINE_WEIGHT_ADJUSTMENT_G
};
}

const GrindModeTraits& get_grind_mode_traits(GrindMode) {
    return kWeightTraits;
}

float get_profile_target(const ProfileController&, GrindMode, int) {
    return 0.0f;
}

void set_profile_target(ProfileController&, GrindMode, int, float) {}

float get_current_profile_target(const ProfileController&, GrindMode) {
    return 0.0f;
}

void update_current_profile_target(ProfileController&, GrindMode, float) {}

float clamp_profile_target(const ProfileController&, GrindMode, float value) {
    return value;
}

void format_ready_value(char* buffer, std::size_t buffer_len, GrindMode mode, float value) {
    if (!buffer || buffer_len == 0) {
        return;
    }
    if (mode == GrindMode::TIME) {
        std::snprintf(buffer, buffer_len, "%.1fs", value);
        return;
    }
    std::snprintf(buffer, buffer_len, SYS_WEIGHT_DISPLAY_FORMAT, value);
}
