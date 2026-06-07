#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using String = std::string;

inline uint32_t millis() {
    static const auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct SerialStub {
    void begin(uint32_t) {}
    void println(const char* text) { std::puts(text ? text : ""); }
    void print(const char* text) { std::fputs(text ? text : "", stdout); }
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::vprintf(fmt, args);
        va_end(args);
    }
    void flush() {}
};

inline SerialStub Serial;
