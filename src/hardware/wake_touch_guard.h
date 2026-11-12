#pragma once

// Consume the whole wake gesture, including its release. A profile change must
// never turn a touch intended for the old screen into a press on the Grind button.
class WakeTouchGuard {
    bool dimmed_ = false;
    bool protect_wake_ = false;
    bool held_ = false;
public:
    void set_dimmed(bool dimmed, bool protect_wake) {
        dimmed_ = dimmed;
        protect_wake_ = protect_wake;
    }
    void suppress_current_touch() { held_ = true; }
    bool allow(bool pressed) {
        if (held_) {
            if (!pressed) held_ = false;
            return false;
        }
        if (pressed && dimmed_ && protect_wake_) {
            held_ = true;
            return false;
        }
        return true;
    }
};
