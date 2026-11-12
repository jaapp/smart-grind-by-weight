#pragma once
#include <cstdint>
#include <algorithm>

namespace oled {
struct Point { int x = 0; int y = 0; };
struct Blit {
    int x = 0, y = 0, width = 0, height = 0;
    int source_x = 0, source_y = 0;
};
class PixelShift {
    uint32_t last_ = 0;
    unsigned step_ = 0;
public:
    static constexpr uint32_t interval_ms = 60000;
    static constexpr Point positions[9] = {
        {0, 0}, {2, 0}, {2, 2}, {0, 2}, {-2, 2},
        {-2, 0}, {-2, -2}, {0, -2}, {2, -2}
    };
    void reset(uint32_t now) { last_ = now; step_ = 0; }
    Point offset() const { return positions[step_]; }
    bool update(uint32_t now, bool allowed) {
        if (!allowed || uint32_t(now - last_) < interval_ms) return false;
        last_ = now;
        step_ = (step_ + 1) % 9;
        return true;
    }
};
inline Blit clip(int x, int y, int w, int h, Point shift, int screen_w, int screen_h) {
    const int left = x + shift.x, top = y + shift.y;
    Blit out;
    out.x = std::max(0, left); out.y = std::max(0, top);
    out.width = std::max(0, std::min(screen_w, left + w) - out.x);
    out.height = std::max(0, std::min(screen_h, top + h) - out.y);
    out.source_x = out.x - left; out.source_y = out.y - top;
    return out;
}
inline bool logical_touch(int x, int y, Point shift, int w, int h, Point& out) {
    out = {x - shift.x, y - shift.y};
    return out.x >= 0 && out.x < w && out.y >= 0 && out.y < h;
}
}
