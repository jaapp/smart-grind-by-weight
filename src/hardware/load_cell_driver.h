#pragma once

#include <Arduino.h>

/**
 * Abstract interface for load cell ADC drivers.
 *
 * Enables runtime selection between the physical HX711 driver and compile-time
 * configurable mock implementations used for simulation and testing.
 */
class LoadCellDriver {
public:
    virtual ~LoadCellDriver() = default;

    virtual bool begin() = 0;
    virtual bool begin(uint8_t gain_value) = 0;
    virtual void set_gain(uint8_t gain_value) = 0;

    virtual void power_up() = 0;
    virtual void power_down() = 0;

    virtual bool is_ready() = 0;
    virtual bool data_waiting_async() = 0;
    virtual bool update_async() = 0;
    virtual int32_t get_raw_data() const = 0;

    virtual bool validate_hardware() = 0;

    virtual bool supports_temperature_sensor() const = 0;
    virtual float get_temperature() const = 0;
    virtual uint32_t get_max_sample_rate() const = 0;

    virtual const char* get_driver_name() const = 0;
};
