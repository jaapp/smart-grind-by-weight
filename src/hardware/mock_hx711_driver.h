#pragma once

#include "load_cell_driver.h"
#include "../config/constants.h"
#include <Arduino.h>

/**
 * MockHX711Driver provides a compile-time selectable simulated implementation of
 * the HX711 ADC. It generates synthetic raw ADC readings with configurable flow
 * rate, noise, and motor latency characteristics to support on-device UI and
 * control development without physical hardware.
 */
class MockHX711Driver : public LoadCellDriver {
public:
    MockHX711Driver();
    ~MockHX711Driver() override;

    bool begin() override;
    bool begin(uint8_t gain_value) override;
    void set_gain(uint8_t gain_value) override;

    void power_up() override;
    void power_down() override;

    bool is_ready() override;
    bool data_waiting_async() override;
    bool update_async() override;
    int32_t get_raw_data() const override;

    bool validate_hardware() override;

    bool supports_temperature_sensor() const override { return false; }
    float get_temperature() const override { return NAN; }
    uint32_t get_max_sample_rate() const override { return HW_LOADCELL_SAMPLE_RATE_SPS; }

    const char* get_driver_name() const override { return "HX711_MOCK"; }

    // Grinder interaction helpers
    static void notify_grinder_start();
    static void notify_grinder_stop();
    static void notify_pulse(uint32_t duration_ms);
    static bool is_pulse_active();

private:
    static MockHX711Driver* instance;

    // Internal helpers
    void reset_state();
    void process_motor_state(unsigned long now_ms, float& total_mass_add, bool& continuous_flow, bool& pulse_flow);
    float process_continuous_state(unsigned long now_ms, bool& continuous_flow);
    float process_pulse_state(unsigned long now_ms, bool& pulse_flow);
    float grams_per_sample() const;
    float random_noise(float peak) const;

    void handle_grinder_start_request(unsigned long now_ms);
    void handle_grinder_stop_request(unsigned long now_ms);
    void handle_pulse_request(unsigned long now_ms, uint32_t duration_ms);

    // Sample generation
    int32_t last_raw_data;
    unsigned long last_sample_time_ms;
    unsigned long next_sample_due_ms;

    float simulated_mass_g;
    float pending_pulse_mass_g;

    bool data_ready_flag;

    // Continuous grinding command state
    bool continuous_commanded;
    bool continuous_started;
    bool continuous_stop_pending;
    unsigned long continuous_start_ms;
    unsigned long continuous_stop_ms;
    unsigned long continuous_ramp_start_ms;

    // Pulse command state
    bool pulse_command_active;
    bool pulse_started;
    bool pulse_stop_pending;
    unsigned long pulse_start_ms;
    unsigned long pulse_end_ms;
    unsigned long pulse_stop_ms;
    unsigned long pulse_ramp_start_ms;
};
