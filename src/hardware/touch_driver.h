#pragma once
#include <driver/i2c_master.h>
#include "../config/constants.h"

struct TouchData {
    uint16_t x;
    uint16_t y;
    bool pressed;
};

class TouchDriver {
private:
    TouchData last_touch;
    bool initialized;
    bool disabled;
    
    // Touch activity tracking
    uint32_t last_touch_time;

public:
    void init();
    void update();
    void disable();
    void enable();
    TouchData get_touch_data() const { return last_touch; }
    bool is_pressed() const { return last_touch.pressed; }
    
    // Touch activity timing
    uint32_t get_ms_since_last_touch() const;

private:
    i2c_master_bus_handle_t bus_handle = nullptr;
    i2c_master_dev_handle_t device_handle = nullptr;
};
