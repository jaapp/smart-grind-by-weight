#pragma once

// UI source files include Arduino.h for the device build, but the screen
// classes used by the desktop simulator only require the standard C runtime.
#include <cstdint>
#include <cstdio>
#include <chrono>

inline unsigned long millis() {
    using namespace std::chrono;
    return static_cast<unsigned long>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

struct SimulatorSerial {
    void println(const char* message) const {
        std::puts(message);
    }

    template <typename... Args>
    void printf(const char* format, Args... args) const {
        std::printf(format, args...);
    }
};

inline SimulatorSerial Serial;
