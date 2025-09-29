#include "hx711_driver.h"
#include "../config/constants.h"
#include <Arduino.h>
#include <driver/gpio.h>

/**
 * HX711 Driver Implementation
 * 
 * HX711 hardware communication algorithms derived from:
 * HX711_ADC library by Olav Kallhovd (olkal)
 * https://github.com/olkal/HX711_ADC
 * 
 * Original HX711_ADC library:
 * Copyright (c) 2018 Olav Kallhovd
 * Licensed under MIT License
 * 
 * Core conversion algorithms, and hardware timing
 * adapted from the original implementation for ESP32-S3 integration.
 */

HX711Driver::HX711Driver(uint8_t sck_pin, uint8_t dout_pin) 
    : sck_pin(sck_pin), dout_pin(dout_pin), gain(1), last_raw_data(0), 
      data_ready_flag(false), conversion_start_time(0), conversion_time(0) {
}

bool HX711Driver::begin() {
    return begin(128);  // Default gain
}

bool HX711Driver::begin(uint8_t gain_value) {
    // Ensure GPIO pins are properly configured for ESP32-S3
    // GPIO 2 is a strapping pin that needs explicit configuration
    gpio_reset_pin((gpio_num_t)sck_pin);
    gpio_reset_pin((gpio_num_t)dout_pin);
    
    pinMode(sck_pin, OUTPUT);
    pinMode(dout_pin, INPUT);
    set_gain(gain_value);
    power_up();
    
    // Wait for HX711 to stabilize - use sample rate appropriate delay
    uint32_t sample_interval_ms = HW_LOADCELL_SAMPLE_INTERVAL_MS;
    delay(sample_interval_ms * 2); // Wait 2 sample intervals for stabilization
    
    // Initial conversion to establish communication - use dynamic timeout
    uint32_t comm_timeout = sample_interval_ms * 2 + 200; // 2 sample intervals + margin
    LOG_BLE("HX711Driver: Waiting for first sample (timeout: %lums)\n", comm_timeout);
    
    unsigned long start_time = millis();
    while (!data_waiting_async() && millis() - start_time < comm_timeout) {
        delay(sample_interval_ms / 4); // Poll at 4x the sample rate
    }
    
    if (data_waiting_async()) {
        update_async();  // Consume first reading
        data_ready_flag = true;
        LOG_BLE("HX711Driver: First sample acquired successfully\n");
        return true;
    }
    
    LOG_BLE("HX711Driver: Timeout waiting for first sample\n");
    return false;
}

void HX711Driver::set_gain(uint8_t gain_value) {
    if (gain_value <= 32) {
        gain = 2;      // 32 gain, channel B
    } else if (gain_value <= 64) {
        gain = 3;      // 64 gain, channel A  
    } else {
        gain = 1;      // 128 gain, channel A
    }
}

void HX711Driver::power_up() {
    power_up_sequence();
}

void HX711Driver::power_down() {
    power_down_sequence();
}

void HX711Driver::power_up_sequence() {
    // Ensure SCK is configured as GPIO output before toggling (may be called before begin())
    pinMode(sck_pin, OUTPUT);
    digitalWrite(sck_pin, LOW);
    delayMicroseconds(100);  // Ensure clean power up
}

void HX711Driver::power_down_sequence() {
    // Ensure SCK is configured as GPIO output before toggling (may be called before begin())
    pinMode(sck_pin, OUTPUT);
    digitalWrite(sck_pin, LOW);
    digitalWrite(sck_pin, HIGH);
    delayMicroseconds(100);  // Hold high for >60μs to enter power down
}

bool HX711Driver::is_ready() {
    return digitalRead(dout_pin) == LOW;
}

bool HX711Driver::data_waiting_async() {
    return is_ready();
}

bool HX711Driver::update_async() {
    if (!is_ready()) {
        return false;
    }
    
    conversion_24bit();
    return true;
}

void HX711Driver::conversion_24bit() {
    // Record conversion timing
    conversion_time = micros() - conversion_start_time;
    conversion_start_time = micros();
    
    uint32_t raw_data = 0;  // Use explicit 32-bit unsigned for ESP32 consistency
    
    // HX711_ADC interrupt protection: Disable interrupts during critical bit-bang conversion
    // This prevents BLE and other interrupts from disrupting the precise HX711 timing
    noInterrupts();
    
    // Read 24 bits of data + gain bits
    for (uint8_t i = 0; i < (24 + gain); i++) {
        digitalWrite(sck_pin, HIGH);
        if (SCK_DELAY) delayMicroseconds(1);
        digitalWrite(sck_pin, LOW);
        
        if (i < 24) {
            raw_data = (raw_data << 1) | digitalRead(dout_pin);
        }
    }
    
    // Re-enable interrupts immediately after conversion
    interrupts();
    
    // HX711_ADC exact data processing: normalize HX711's offset binary output
    // HX711 natural range: 0x800000 to 0x7FFFFF
    // XOR converts to:     0x000000 to 0xFFFFFF
    raw_data = raw_data ^ 0x800000;
    
    // HX711_ADC data validation
    if (raw_data > 0xFFFFFF) {
        // Data out of range - this shouldn't happen with proper 24-bit data
        LOG_BLE("HX711Driver: Data out of range - raw=0x%08lx\n", raw_data);
        return; // Skip this invalid reading
    }
    
    last_raw_data = (int32_t)raw_data;  // Explicit cast to int32_t for consistency
    data_ready_flag = true;
}

int32_t HX711Driver::get_raw_data() const {
    return last_raw_data;
}

bool HX711Driver::validate_hardware() {
    // Simple hardware validation - check if we can get readings
    // Calculate timeout based on sample rate: need time for 3 samples plus margin
    uint32_t sample_interval_ms = HW_LOADCELL_SAMPLE_INTERVAL_MS;
    uint32_t validation_timeout = (sample_interval_ms * 4) + 500; // 4 sample intervals + 500ms margin
    
    LOG_BLE("HX711Driver: Hardware validation timeout = %lums (sample rate: %d SPS)\n", 
           validation_timeout, HW_LOADCELL_SAMPLE_RATE_SPS);
    
    unsigned long start_time = millis();
    int successful_reads = 0;
    
    while (millis() - start_time < validation_timeout && successful_reads < 3) {
        if (data_waiting_async()) {
            if (update_async()) {
                successful_reads++;
                LOG_BLE("HX711Driver: Validation read %d/3 successful\n", successful_reads);
            }
        }
        delay(sample_interval_ms / 4); // Poll at 4x the sample rate
    }
    
    LOG_BLE("HX711Driver: Hardware validation completed - %d/3 successful reads in %lums\n", 
           successful_reads, millis() - start_time);
    
    return successful_reads >= 3;
}

uint8_t HX711Driver::get_current_gain() const {
    switch (gain) {
        case 1: return 128;
        case 2: return 32;
        case 3: return 64;
        default: return 128;
    }
}
