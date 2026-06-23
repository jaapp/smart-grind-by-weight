#pragma once
#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

// =============================================================================
// arduino_compat.h — Arduino API shim for ESP-IDF 5.x
//
// Provides drop-in replacements for the most commonly used Arduino primitives
// so that existing source files can compile under pure IDF without modification.
// =============================================================================

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_clk_tree.h"
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdint.h>

// =============================================================================
// Timing
// =============================================================================

inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

inline uint32_t micros() {
    return (uint32_t)(esp_timer_get_time());
}

inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

inline void delayMicroseconds(uint32_t us) {
    esp_rom_delay_us(us);
}

// =============================================================================
// GPIO constants
// =============================================================================

#ifndef HIGH
#define HIGH 1
#endif

#ifndef LOW
#define LOW 0
#endif

#ifndef OUTPUT
#define OUTPUT GPIO_MODE_OUTPUT
#endif

#ifndef INPUT
#define INPUT GPIO_MODE_INPUT
#endif

// INPUT_PULLUP / INPUT_PULLDOWN are handled in pinMode() below;
// these sentinel values let us distinguish them from plain INPUT.
#define INPUT_PULLUP   0x10
#define INPUT_PULLDOWN 0x11

// =============================================================================
// GPIO functions
// =============================================================================

inline void pinMode(int pin, int mode) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.intr_type    = GPIO_INTR_DISABLE;

    switch (mode) {
        case INPUT_PULLUP:
            cfg.mode       = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_PULLDOWN:
            cfg.mode         = GPIO_MODE_INPUT;
            cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case GPIO_MODE_INPUT:
            cfg.mode         = GPIO_MODE_INPUT;
            cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case GPIO_MODE_OUTPUT:
        default:
            cfg.mode         = GPIO_MODE_OUTPUT;
            cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
    }

    gpio_config(&cfg);
}

inline void digitalWrite(int pin, int val) {
    gpio_set_level((gpio_num_t)pin, val);
}

inline int digitalRead(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

// =============================================================================
// Interrupt control
// =============================================================================

#define noInterrupts() taskDISABLE_INTERRUPTS()
#define interrupts()   taskENABLE_INTERRUPTS()

// =============================================================================
// ESP class — system information helpers
// =============================================================================

class _ESP {
public:
    static uint32_t getFreeHeap() {
        return (uint32_t)esp_get_free_heap_size();
    }

    static uint32_t getHeapSize() {
        return (uint32_t)heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    }

    static uint32_t getFlashChipSize() {
        // Return the configured flash size (16 MB for this board).
        // esp_flash_get_size() requires an esp_flash_t pointer; using the
        // compile-time constant is simpler and avoids a driver dependency.
        return 16UL * 1024UL * 1024UL;
    }

    static uint32_t getCpuFreqMHz() {
        uint32_t freq_hz = 0;
        esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,
                                     ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX,
                                     &freq_hz);
        return freq_hz / 1000000UL;
    }

    static void restart() {
        esp_restart();
    }
};

// Global singleton mirroring the Arduino `ESP` object
static _ESP ESP;

// =============================================================================
// CPU frequency helpers (Arduino-style free functions)
// =============================================================================

inline uint32_t getCpuFrequencyMhz() {
    return ESP.getCpuFreqMHz();
}

inline bool setCpuFrequencyMhz(uint32_t /*freq*/) {
    // Full dynamic frequency scaling via esp_pm_configure is board-specific;
    // the device always boots at 240 MHz (see sdkconfig.defaults).
    return true;
}

// =============================================================================
// String class — lightweight wrapper around std::string
// =============================================================================

class String : public std::string {
public:
    String() = default;

    String(const char* s) : std::string(s ? s : "") {}

    String(int n) : std::string(std::to_string(n)) {}

    String(unsigned int n) : std::string(std::to_string(n)) {}

    String(long n) : std::string(std::to_string(n)) {}

    String(unsigned long n) : std::string(std::to_string(n)) {}

    // Construct from a char buffer with an explicit byte length.
    String(const char* buf, size_t len) : std::string(buf ? buf : "", buf ? len : 0) {}

    String(float n, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, (double)n);
        assign(buf);
    }

    String(const std::string& s) : std::string(s) {}

    // --- Query helpers ---

    bool isEmpty() const { return empty(); }

    int length() const { return (int)size(); }

    int toInt() const {
        char* end = nullptr;
        long v = strtol(c_str(), &end, 10);
        return (end != c_str()) ? (int)v : 0;
    }

    float toFloat() const {
        char* end = nullptr;
        float v = strtof(c_str(), &end);
        return (end != c_str()) ? v : 0.0f;
    }

    bool startsWith(const char* prefix) const {
        return find(prefix) == 0;
    }

    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        size_t slen = strlen(suffix);
        if (slen > size()) return false;
        return compare(size() - slen, slen, suffix) == 0;
    }

    String substring(size_t from, size_t to = std::string::npos) const {
        size_t len = (to == std::string::npos) ? std::string::npos : (to - from);
        return String(substr(from, len));
    }

    // Returns the index of the first occurrence of str, or -1 if not found.
    int indexOf(const char* str, size_t fromIndex = 0) const {
        size_t pos = find(str, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    // Single-character overload of indexOf.
    int indexOf(char c, size_t fromIndex = 0) const {
        size_t pos = find(c, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    // Returns the index of the last occurrence of str, or -1 if not found.
    int lastIndexOf(const char* str) const {
        size_t pos = rfind(str);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    // Single-character overload of lastIndexOf.
    int lastIndexOf(char c) const {
        size_t pos = rfind(c);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    // --- Operators ---

    String operator+(const String& rhs) const {
        return String(std::string(*this) + std::string(rhs));
    }

    String operator+(const char* rhs) const {
        return String(std::string(*this) + rhs);
    }

    String& operator+=(const String& rhs) {
        std::string::operator+=(rhs);
        return *this;
    }

    String& operator+=(const char* rhs) {
        std::string::operator+=(rhs);
        return *this;
    }

    bool operator==(const char* rhs) const {
        return std::string(*this) == rhs;
    }

    bool operator!=(const char* rhs) const {
        return std::string(*this) != rhs;
    }
};

// Allow "literal" + String concatenation
inline String operator+(const char* lhs, const String& rhs) {
    return String(lhs) + rhs;
}

// =============================================================================
// Serial — maps to standard printf / IDF log output
// =============================================================================

class _Serial {
public:
    // In IDF the USB Serial/JTAG console is initialised by the framework;
    // Serial.begin() is a no-op.
    void begin(unsigned long /*baud*/) {}

    void printf(const char* fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }

    void println(const char* s) const {
        ::printf("%s\n", s ? s : "");
    }

    void println(const String& s) const {
        ::printf("%s\n", s.c_str());
    }

    void println(int n) const {
        ::printf("%d\n", n);
    }

    void println() const {
        ::printf("\n");
    }

    void print(const char* s) const {
        ::printf("%s", s ? s : "");
    }

    void print(const String& s) const {
        ::printf("%s", s.c_str());
    }

    void print(int n) const {
        ::printf("%d", n);
    }

    // flush() is a no-op: IDF USB CDC / UART flushes automatically.
    void flush() const {}
};

// Global singleton mirroring the Arduino `Serial` object
static _Serial Serial;

// =============================================================================
// Math helpers (Arduino built-ins)
// =============================================================================

#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

// Bring std::min / std::max into the global namespace so code that calls
// min(a,b) / max(a,b) without std:: prefix still compiles.
#include <algorithm>
using std::min;
using std::max;
using std::abs;

#endif // ARDUINO_COMPAT_H
