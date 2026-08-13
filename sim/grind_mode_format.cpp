#include "controllers/grind_mode_traits.h"

#include "config/constants.h"
#include <cstdio>

void format_ready_value(char* buffer, std::size_t buffer_len, GrindMode mode, float value) {
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (mode == GrindMode::TIME) {
        std::snprintf(buffer, buffer_len, "%.1fs", value);
    } else {
        std::snprintf(buffer, buffer_len, SYS_WEIGHT_DISPLAY_FORMAT, value);
    }
}
