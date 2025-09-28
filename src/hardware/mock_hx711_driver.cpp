#include "mock_hx711_driver.h"
#include "../config/constants.h"
#include <algorithm>
#include <cstdint>
#ifdef ESP_PLATFORM
#include <esp_system.h>
#endif

MockHX711Driver* MockHX711Driver::instance = nullptr;

MockHX711Driver::MockHX711Driver() {
    instance = this;
    reset_state();
}

MockHX711Driver::~MockHX711Driver() {
    if (instance == this) {
        instance = nullptr;
    }
}

void MockHX711Driver::reset_state() {
    last_raw_data = static_cast<int32_t>(DEBUG_MOCK_BASELINE_RAW);
    last_sample_time_ms = 0;
    next_sample_due_ms = 0;
    simulated_mass_g = 0.0f;
    pending_pulse_mass_g = 0.0f;
    data_ready_flag = false;

    continuous_commanded = false;
    continuous_started = false;
    continuous_stop_pending = false;
    continuous_start_ms = 0;
    continuous_stop_ms = 0;
    continuous_ramp_start_ms = 0;

    pulse_command_active = false;
    pulse_started = false;
    pulse_stop_pending = false;
    pulse_start_ms = 0;
    pulse_end_ms = 0;
    pulse_stop_ms = 0;
    pulse_ramp_start_ms = 0;
}

bool MockHX711Driver::begin() {
    return begin(128);
}

bool MockHX711Driver::begin(uint8_t gain_value) {
    (void)gain_value;
    reset_state();
    unsigned long now = millis();
    last_sample_time_ms = now;
    next_sample_due_ms = now + HW_LOADCELL_SAMPLE_INTERVAL_MS;
    last_raw_data = static_cast<int32_t>(DEBUG_MOCK_BASELINE_RAW);
    data_ready_flag = false;
    LOG_BLE("MockHX711Driver: initialized (flow=%.2fg/s, cal=%.1f)\n",
            DEBUG_MOCK_FLOW_RATE_GPS, DEBUG_MOCK_CAL_FACTOR);
    return true;
}

void MockHX711Driver::set_gain(uint8_t gain_value) {
    (void)gain_value; // Gain has no effect on the simulated driver
}

void MockHX711Driver::power_up() {
    // No hardware to control; keep state intact
}

void MockHX711Driver::power_down() {
    // No hardware to control; keep state intact
}

bool MockHX711Driver::data_waiting_async() {
    unsigned long now = millis();
    if (now >= next_sample_due_ms) {
        data_ready_flag = true;
        return true;
    }
    return false;
}

bool MockHX711Driver::is_ready() {
    return data_waiting_async();
}

bool MockHX711Driver::update_async() {
    if (!data_waiting_async()) {
        return false;
    }

    unsigned long now = millis();
    last_sample_time_ms = now;
    next_sample_due_ms = now + HW_LOADCELL_SAMPLE_INTERVAL_MS;

    float mass_increment = 0.0f;
    bool continuous_flow = false;
    bool pulse_flow = false;
    process_motor_state(now, mass_increment, continuous_flow, pulse_flow);

    if (mass_increment > 0.0f) {
        simulated_mass_g += mass_increment;
    }

    if (simulated_mass_g < 0.0f) {
        simulated_mass_g = 0.0f;
    } else if (simulated_mass_g > 500.0f) {
        simulated_mass_g = 500.0f;
    }

    float raw_value = static_cast<float>(DEBUG_MOCK_BASELINE_RAW);
    raw_value += simulated_mass_g * DEBUG_MOCK_CAL_FACTOR;

    float noise_peak = DEBUG_MOCK_IDLE_NOISE_RAW;
    if (pulse_flow) {
        noise_peak = DEBUG_MOCK_GRIND_NOISE_RAW * 3.0f;
    } else if (continuous_flow) {
        noise_peak = DEBUG_MOCK_GRIND_NOISE_RAW;
    }
    raw_value += random_noise(noise_peak);

    raw_value = std::max(0.0f, std::min(raw_value, 16777215.0f)); // Clamp to 24-bit range
    last_raw_data = static_cast<int32_t>(raw_value);
    data_ready_flag = false;
    return true;
}

int32_t MockHX711Driver::get_raw_data() const {
    return last_raw_data;
}

bool MockHX711Driver::validate_hardware() {
    // Always succeeds for the mock driver
    return true;
}

void MockHX711Driver::process_motor_state(unsigned long now_ms, float& total_mass_add,
                                          bool& continuous_flow, bool& pulse_flow) {
    continuous_flow = false;
    pulse_flow = false;

    float continuous_add = process_continuous_state(now_ms, continuous_flow);
    float pulse_add = process_pulse_state(now_ms, pulse_flow);

    total_mass_add = continuous_add + pulse_add;
    if (total_mass_add < 0.0f) {
        total_mass_add = 0.0f;
    }
}

float MockHX711Driver::process_continuous_state(unsigned long now_ms, bool& continuous_flow) {
    continuous_flow = false;
    float flow_factor = 0.0f;

    if (continuous_commanded) {
        if (!continuous_started) {
            if (now_ms - continuous_start_ms >= DEBUG_MOCK_START_DELAY_MS) {
                continuous_started = true;
                continuous_ramp_start_ms = now_ms;
            }
        }

        if (continuous_started) {
            continuous_flow = true;
            if (DEBUG_MOCK_FLOW_RAMP_MS <= 0) {
                flow_factor = 1.0f;
            } else {
                unsigned long elapsed = now_ms - continuous_ramp_start_ms;
                flow_factor = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(DEBUG_MOCK_FLOW_RAMP_MS));
            }
        }
    } else if (continuous_stop_pending) {
        unsigned long elapsed = now_ms - continuous_stop_ms;
        float ramp_factor = 0.0f;
        if (DEBUG_MOCK_FLOW_RAMP_MS > 0) {
            ramp_factor = std::max(0.0f, 1.0f - static_cast<float>(elapsed) / static_cast<float>(DEBUG_MOCK_FLOW_RAMP_MS));
        }

        if (ramp_factor > 0.0f) {
            continuous_flow = true;
            flow_factor = ramp_factor;
        }

        uint32_t stop_threshold = std::max<uint32_t>(DEBUG_MOCK_STOP_DELAY_MS, DEBUG_MOCK_FLOW_RAMP_MS);
        if (elapsed >= stop_threshold) {
            continuous_stop_pending = false;
            continuous_started = false;
        }
    } else {
        continuous_started = false;
    }

    if (flow_factor < 0.0f) {
        flow_factor = 0.0f;
    }

    return grams_per_sample() * flow_factor;
}

float MockHX711Driver::process_pulse_state(unsigned long now_ms, bool& pulse_flow) {
    pulse_flow = false;
    float flow_factor = 0.0f;

    if (pulse_command_active && !pulse_started) {
        if (now_ms - pulse_start_ms >= DEBUG_MOCK_START_DELAY_MS) {
            pulse_started = true;
            pulse_ramp_start_ms = now_ms;
        }
    }

    if (pulse_started && !pulse_stop_pending && now_ms > pulse_end_ms) {
        pulse_stop_pending = true;
        pulse_stop_ms = now_ms;
    }

    if (pulse_started && pulse_command_active && !pulse_stop_pending) {
        if (DEBUG_MOCK_FLOW_RAMP_MS <= 0) {
            flow_factor = 1.0f;
        } else {
            unsigned long elapsed = now_ms - pulse_ramp_start_ms;
            flow_factor = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(DEBUG_MOCK_FLOW_RAMP_MS));
        }
        if (flow_factor > 0.0f) {
            pulse_flow = true;
        }
    } else if (pulse_started && pulse_stop_pending) {
        unsigned long elapsed = now_ms - pulse_stop_ms;
        if (DEBUG_MOCK_FLOW_RAMP_MS > 0) {
            flow_factor = std::max(0.0f, 1.0f - static_cast<float>(elapsed) / static_cast<float>(DEBUG_MOCK_FLOW_RAMP_MS));
        } else {
            flow_factor = 0.0f;
        }

        if (flow_factor > 0.0f) {
            pulse_flow = true;
        }

        uint32_t stop_threshold = std::max<uint32_t>(DEBUG_MOCK_STOP_DELAY_MS, DEBUG_MOCK_FLOW_RAMP_MS);
        if (elapsed >= stop_threshold) {
            pulse_stop_pending = false;
            pulse_command_active = false;
            pulse_started = false;
            pending_pulse_mass_g = 0.0f;
        }
    }

    float addition = 0.0f;
    if (flow_factor > 0.0f) {
        addition = grams_per_sample() * flow_factor;
        if (pending_pulse_mass_g > 0.0f) {
            addition = std::min(addition, pending_pulse_mass_g);
            pending_pulse_mass_g -= addition;
            if (pending_pulse_mass_g <= 0.0001f) {
                pending_pulse_mass_g = 0.0f;
            }
        }
    } else if (!pulse_command_active && !pulse_stop_pending) {
        pending_pulse_mass_g = 0.0f;
    }

    return addition;
}

float MockHX711Driver::grams_per_sample() const {
    return DEBUG_MOCK_FLOW_RATE_GPS / static_cast<float>(HW_LOADCELL_SAMPLE_RATE_SPS);
}

float MockHX711Driver::random_noise(float peak) const {
    if (peak <= 0.0f) {
        return 0.0f;
    }
#ifdef ESP_PLATFORM
    uint32_t value = esp_random();
#else
    uint32_t value = static_cast<uint32_t>(random());
#endif
    const float normalized = static_cast<float>(value) / static_cast<float>(UINT32_MAX);
    return (normalized * 2.0f - 1.0f) * peak;
}

void MockHX711Driver::handle_grinder_start_request(unsigned long now_ms) {
    continuous_commanded = true;
    continuous_started = false;
    continuous_stop_pending = false;
    continuous_start_ms = now_ms;
    continuous_ramp_start_ms = now_ms;
}

void MockHX711Driver::handle_grinder_stop_request(unsigned long now_ms) {
    if (!continuous_started) {
        continuous_commanded = false;
        continuous_stop_pending = false;
        return;
    }
    continuous_commanded = false;
    continuous_stop_pending = true;
    continuous_stop_ms = now_ms;
}

void MockHX711Driver::handle_pulse_request(unsigned long now_ms, uint32_t duration_ms) {
    pulse_command_active = true;
    pulse_started = false;
    pulse_stop_pending = false;
    pulse_start_ms = now_ms;
    pulse_end_ms = now_ms + duration_ms;
    pending_pulse_mass_g += DEBUG_MOCK_FLOW_RATE_GPS * (static_cast<float>(duration_ms) / 1000.0f);
    pulse_ramp_start_ms = now_ms;
}

void MockHX711Driver::notify_grinder_start() {
    if (instance) {
        instance->handle_grinder_start_request(millis());
    }
}

void MockHX711Driver::notify_grinder_stop() {
    if (instance) {
        instance->handle_grinder_stop_request(millis());
    }
}

void MockHX711Driver::notify_pulse(uint32_t duration_ms) {
    if (instance) {
        instance->handle_pulse_request(millis(), duration_ms);
    }
}

bool MockHX711Driver::is_pulse_active() {
    if (!instance) {
        return false;
    }
    return instance->pulse_command_active || instance->pulse_stop_pending || instance->pending_pulse_mass_g > 0.0001f;
}
