#pragma once

#include "circular_buffer_math/circular_buffer_math.h"
#include "../config/hardware_config.h"
#include "hx711_driver.h"
#include "../config/pins.h"
#include "../config/constants.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <memory>


/*
 * WeightSensor - Hardware-Abstracted Weight Processing System
 * 
 * Combines HX711 load cell ADC hardware with weight processing and filtering.
 * 
 * Architecture:
 * - Directly integrates HX711 driver (no HAL)
 * - Maintains same public API for backward compatibility
 * - Hardware-specific logic handled by ADC drivers
 * - Supports temperature compensation for capable ADCs
 * 
 * Enhanced for ESP32-S3 coffee scale with:
 * - Real-time Core 0 integration
 * - Advanced CircularBufferMath filtering
 * - Flow rate analysis and predictive grinding
 * - Multi-ADC hardware support
 */
class WeightSensor {
private:
    // HX711 hardware driver
    std::unique_ptr<HX711Driver> adc_driver;
    
    // CircularBufferMath for advanced filtering and analysis
    CircularBufferMath raw_filter;
    
    // Calibration parameters
    float cal_factor;
    int32_t tare_offset;
    
    // Current readings (cached)
    float current_weight;
    float current_temperature;  // For ADCs with temperature sensors
    int32_t current_raw_adc;
    unsigned long last_update;
    Preferences* prefs;
    
    bool data_available;
    
    // Tare implementation (hardware-independent)
    static const uint8_t DATA_SET = 16 + 1 + 1;  // SAMPLES + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE
    bool doTare;
    uint8_t tareTimes;
    bool tareStatus;
    bool tareTimeoutFlag;
    unsigned long tareTimeOut;
    
    // Weight activity tracking
    float last_significant_weight;
    uint32_t last_weight_activity_time;
    
    // WeightSamplingTask integration (RealtimeController removed)
    
    // Single calibration conversion point
    float raw_to_weight(int32_t raw_adc_value) const {
        return (float)(raw_adc_value - tare_offset) / cal_factor;
    }
    
    // Raw settling threshold calculation - use absolute value since StdDev is always positive
    int32_t weight_to_raw_threshold(float weight_threshold) const {
        return (int32_t)(abs(weight_threshold * cal_factor));
    }
    
    
    // Hardware abstraction helpers
    bool initialize_adc_hardware();
    void update_temperature_if_available();
    
#if ENABLE_REALTIME_HEARTBEAT
    // SPS tracking for performance monitoring
    static const int SPS_TRACKING_BUFFER_SIZE = 160;  // ~2 seconds at 80 SPS (ample headroom at lower SPS)
    uint32_t sps_timestamps[SPS_TRACKING_BUFFER_SIZE];
    int sps_buffer_index;
    int sps_sample_count;
#endif
    
public:
    WeightSensor(); 
    ~WeightSensor();
    
    // Initialization and configuration
    void init(Preferences* preferences);
    bool begin();
    bool begin(uint8_t gain_value);
    void set_gain(uint8_t gain_value = 128);
    
    // Power management
    void power_up();
    void power_down();
    
    // Tare operations
    void tare();                          // Blocking tare
    void tareNoDelay();                   // Exact HX711_ADC method
    bool getTareStatus();                 // Exact HX711_ADC method
    
    // Legacy wrapper methods for compatibility
    bool start_nonblocking_tare() { tareNoDelay(); return true; }
    bool is_tare_in_progress() const { return doTare; }
    
    // Calibration
    void calibrate(float known_weight);
    void set_calibration_factor(float factor);
    void set_zero_offset(int32_t offset);
    
    // Core data acquisition and processing
    void update();
    bool is_settled(uint32_t window_ms = HW_SCALE_PRECISION_SETTLING_TIME_MS);
    
    // Non-blocking HX711 data acquisition (merged from HX711Core)
    bool data_waiting_async();
    bool update_async();
    
    // Unified settling methods with window_ms
    bool check_settling_complete(uint32_t window_ms, float* settled_weight_out = nullptr);
    void cancel_settling();
    
    // Raw ADC data access (for advanced filtering and diagnostics)
    int32_t get_raw_adc_instant() const;                    // Latest raw ADC reading
    int32_t get_raw_adc_smoothed(uint32_t window_ms) const; // Smoothed raw ADC over time window
    int32_t get_raw_adc_data() const;  // Direct hardware access
    
    // Primary weight readings using CircularBufferMath with single conversion point
    float get_instant_weight() const;                        // Latest single sample converted to weight
    float get_weight_low_latency() const;                    // 50ms window - for real-time control
    float get_display_weight();                              // 250ms + asymmetric filter - for UI
    float get_weight_high_latency() const;                   // 250ms window - for final measurements
    
    // Flow rate analysis using CircularBufferMath
    float get_flow_rate(uint32_t window_ms = 200) const;     // Flow rate calculation (default 200ms window)
    float get_flow_rate_95th_percentile(uint32_t window_ms = 200) const; // 95th percentile flow rate for dynamic pulse algorithm
    bool is_flow_rate_stable(uint32_t window_ms = 100) const; // Check if flow rate has stabilized
    
    // Settling methods - WARNING: These methods block execution!
    float get_motor_settled_weight(float* settle_time_out = nullptr);     // Motor settling (300ms window) - for after motor vibrations
    float get_precision_settled_weight(float* settle_time_out = nullptr); // Precision settling (500ms window) - for tare/calibration/final measurements
    
    // Legacy method for custom settling
    float get_settled_weight(uint32_t window_ms, float* settle_time_out = nullptr);
    
    // Status and information methods
    int get_sample_count() const;                            // Returns filter sample count
    float get_calibration_factor();                          
    int32_t get_zero_offset() const { return tare_offset; }
    bool is_initialized();                                   
    bool data_ready();
    bool is_data_ready() const;
    
    // Calibration persistence
    void save_calibration();
    void save_calibration_weight(float weight);
    float get_saved_calibration_weight();
    void load_calibration();
    void clear_calibration_data();
    
    // Hardware validation and information
    bool validate_hardware();
    const char* get_adc_driver_name() const;
    bool supports_temperature_sensor() const;
    float get_temperature() const;  // Returns NaN if not supported
    uint32_t get_max_sample_rate() const;
    
    // WeightSamplingTask integration interface
    bool sample_and_feed_filter();                                   // Core 0 sampling method for WeightSamplingTask
    
    // Hardware access for WeightSamplingTask Core 0
    CircularBufferMath* get_raw_filter() { return &raw_filter; }     // Direct access to raw data math helper
    float get_saved_calibration_factor();                            // Get calibration factor from preferences
    void set_hardware_initialized() { data_available = true; }       // Mark hardware as ready
    
    // Weight activity timing (for screen timeout reset)
    uint32_t get_ms_since_last_weight_activity() const;
    
#if ENABLE_REALTIME_HEARTBEAT
    // SPS performance monitoring
    void record_sample_timestamp();                                  // Record when a sample was taken (called by Core 0)
    float get_current_sps() const;                                   // Get samples per second over last 2 seconds
#endif

};
