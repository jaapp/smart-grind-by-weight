#include "touch_driver.h"
#include <Arduino.h>
#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr uint32_t kTouchI2CFrequencyHz = 300000;
constexpr int kTouchI2CTimeoutMs = 5;
constexpr char kTag[] = "TouchDriver";

void suppress_touch_i2c_logs() {
#if DEBUG_SUPPRESS_TOUCH_I2C_ERRORS
    static bool configured = false;
    if (configured) {
        return;
    }
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-i2c-ng", ESP_LOG_NONE);
    esp_log_level_set("Wire", ESP_LOG_NONE);
    configured = true;
#endif
}
}

void TouchDriver::init() {
    if (initialized) {
        return;
    }
    suppress_touch_i2c_logs();

    if (bus_handle == nullptr) {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = static_cast<gpio_num_t>(HW_TOUCH_I2C_SDA_PIN);
        bus_config.scl_io_num = static_cast<gpio_num_t>(HW_TOUCH_I2C_SCL_PIN);
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.intr_priority = 0;
        bus_config.trans_queue_depth = 0;
        bus_config.flags.enable_internal_pullup = 1;
        bus_config.flags.allow_pd = 0;

        esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
            disabled = true;
            return;
        }
    }

    if (device_handle == nullptr) {
        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = HW_TOUCH_I2C_ADDRESS;
        device_config.scl_speed_hz = kTouchI2CFrequencyHz;
        device_config.scl_wait_us = 0;
#if DEBUG_SUPPRESS_TOUCH_I2C_ERRORS
        device_config.flags.disable_ack_check = 1;  // Treat idle NACKs as benign when suppression enabled.
#else
        device_config.flags.disable_ack_check = 0;
#endif

        esp_err_t err = i2c_master_bus_add_device(bus_handle, &device_config, &device_handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to attach touch device: %s", esp_err_to_name(err));
            disabled = true;
            return;
        }
    }
    
    last_touch = {0, 0, false};
    initialized = true;
    disabled = false;
    
    // Initialize touch tracking
    last_touch_time = millis();
}

void TouchDriver::update() {
    if (!initialized || disabled || device_handle == nullptr) {
        return;
    }
    
    uint8_t buf[5] = {0};
    uint8_t reg = 0x02; // FT3168_REG_NUM_TOUCHES
    esp_err_t err = i2c_master_transmit_receive(device_handle, &reg, sizeof(reg), buf, sizeof(buf), kTouchI2CTimeoutMs);
    if (err != ESP_OK) {
        // Touch controller NACKs when no touch data - treat as no-touch without logging.
        if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(kTag, "Touch poll failed: %s", esp_err_to_name(err));
        }
        last_touch.pressed = false;
        return;
    }
    
    uint8_t touches = buf[0] & 0x0F;
    if (touches > 0) {
        last_touch.x = ((buf[1] & 0x0F) << 8) | buf[2];
        last_touch.y = ((buf[3] & 0x0F) << 8) | buf[4];
        last_touch.pressed = true;

        // Update last touch time
        last_touch_time = millis();
    } else {
        last_touch.pressed = false;
    }
}

void TouchDriver::disable() {
    disabled = true;
    last_touch.pressed = false;  // Clear any active touch state
}

void TouchDriver::enable() {
    disabled = false;
}

uint32_t TouchDriver::get_ms_since_last_touch() const {
    if (!initialized || disabled) return 0;
    return millis() - last_touch_time;
}
