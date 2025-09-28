#include "WeightSensor.h"
#include "../config/constants.h"
#include "hx711_driver.h"
#if DEBUG_ENABLE_LOADCELL_MOCK
#include "mock_hx711_driver.h"
#endif
#include <Arduino.h>
#include <math.h>

/*
 * WeightSensor Implementation
 * 
 * Hardware-abstracted weight sensor implementation supporting multiple ADC types.
 * Uses factory pattern to create appropriate ADC driver based on hardware configuration.
 * Maintains backward compatibility with existing HX711-based code.
 */

// General ADC timing constants
#define SIGNAL_TIMEOUT 100             // Signal timeout in ms
#define TARE_TIMEOUT_MS 2000           // Tare operation timeout

WeightSensor::WeightSensor() {
    // Initialize calibration parameters
#if DEBUG_ENABLE_LOADCELL_MOCK
    cal_factor = DEBUG_MOCK_CAL_FACTOR;
#else
    cal_factor = USER_DEFAULT_CALIBRATION_FACTOR;
#endif
    tare_offset = 0;
    
    // Initialize current readings
    current_weight = 0.0;
    current_temperature = NAN;
    current_raw_adc = 0;
    last_update = 0;
    data_available = false;
    prefs = nullptr;
    
    // Initialize tare state
    doTare = false;
    tareTimes = 0;
    tareStatus = false;
    tareTimeoutFlag = false;
    tareTimeOut = 0;
    
    // Initialize weight activity tracking
    last_significant_weight = 0.0f;
    last_weight_activity_time = millis();
    
    // RealtimeController integration removed - handled by WeightSamplingTask

#if ENABLE_REALTIME_HEARTBEAT
    // Initialize SPS tracking
    sps_buffer_index = 0;
    sps_sample_count = 0;
    memset(sps_timestamps, 0, sizeof(sps_timestamps));
#endif
}

WeightSensor::~WeightSensor() {
    // WeightSensor cleanup
}

void WeightSensor::init(Preferences* preferences) {
    prefs = preferences;
    
    LOG_BLE("Initializing WeightSensor configuration and filters...\n");
    
    // Create load cell driver instance based on configuration
#if DEBUG_ENABLE_LOADCELL_MOCK
    adc_driver = std::make_unique<MockHX711Driver>();
#else
    adc_driver = std::make_unique<HX711Driver>(HW_LOADCELL_SCK_PIN, HW_LOADCELL_DOUT_PIN);
#endif
    if (!adc_driver) {
        LOG_BLE("ERROR: Failed to create ADC driver\n");
        return;
    }
    
    LOG_BLE("Created %s ADC driver\n", adc_driver->get_driver_name());
    
    // Don't load calibration data here - WeightSamplingTask will handle it on Core 0
    // This avoids NVS threading issues between Core 1 init and Core 0 hardware access
    
    // Reset state variables
    current_weight = 0.0;
    current_temperature = NAN;
    current_raw_adc = 0;
    data_available = false;
    last_update = 0;
    doTare = false;
    tareTimes = 0;
    tareStatus = false;
    
    LOG_BLE("WeightSensor configuration initialized - hardware will be initialized by WeightSamplingTask\n");
}

bool WeightSensor::begin() {
    return begin(128);  // Default gain
}

bool WeightSensor::begin(uint8_t gain_value) {
    if (!adc_driver) {
        LOG_BLE("ERROR: ADC driver not initialized\n");
        return false;
    }
    
    LOG_BLE("Initializing %s ADC hardware...\n", adc_driver->get_driver_name());
    
    bool success = adc_driver->begin(gain_value);
    if (success) {
        data_available = true;
        LOG_BLE("%s ADC initialized successfully\n", adc_driver->get_driver_name());
    } else {
        LOG_BLE("ERROR: Failed to initialize %s ADC\n", adc_driver->get_driver_name());
    }
    
    return success;
}

void WeightSensor::set_gain(uint8_t gain_value) {
    if (adc_driver) {
        adc_driver->set_gain(gain_value);
    }
}

// Remove this function - it's now handled by ADC driver

bool WeightSensor::data_waiting_async() {
    return adc_driver ? adc_driver->data_waiting_async() : false;
}

bool WeightSensor::update_async() {
    return adc_driver ? adc_driver->update_async() : false;
}

// conversion_24bit() method removed - now handled by ADC driver

void WeightSensor::power_down() {
    if (adc_driver) {
        adc_driver->power_down();
    }
}

void WeightSensor::power_up() {
    if (adc_driver) {
        adc_driver->power_up();
    }
}

// power_down_sequence() method removed - now handled by ADC driver

// power_up_sequence() method removed - now handled by ADC driver

bool WeightSensor::validate_hardware() {
    return adc_driver ? adc_driver->validate_hardware() : false;
}

// New hardware abstraction methods
int32_t WeightSensor::get_raw_adc_data() const {
    return adc_driver ? adc_driver->get_raw_data() : 0;
}

const char* WeightSensor::get_adc_driver_name() const {
    return adc_driver ? adc_driver->get_driver_name() : "Unknown";
}

bool WeightSensor::supports_temperature_sensor() const {
    return adc_driver ? adc_driver->supports_temperature_sensor() : false;
}

float WeightSensor::get_temperature() const {
    return adc_driver ? adc_driver->get_temperature() : NAN;
}

uint32_t WeightSensor::get_max_sample_rate() const {
    return adc_driver ? adc_driver->get_max_sample_rate() : 0;
}

// Hardware abstraction helper methods
void WeightSensor::update_temperature_if_available() {
    if (adc_driver && adc_driver->supports_temperature_sensor()) {
        current_temperature = adc_driver->get_temperature();
    }
}

void WeightSensor::tare() {
    LOG_LOADCELL_DEBUG("[DEBUG %lums] BLOCKING_TARE_START: Beginning blocking tare operation using HX711_ADC exact implementation\n", millis());
    
    // Use exact HX711_ADC non-blocking implementation
    tareNoDelay();
    
    // Wait for tare completion with timeout
    unsigned long start_time = millis();
    while (doTare && millis() - start_time < HW_TARE_TIMEOUT_MS) {
        // Call update to ensure fresh samples are collected and tare state is updated
        update();
        
        // Use load cell update interval from constants.h
        delay(SYS_TASK_LOADCELL_INTERVAL_MS);
    }
    
    if (doTare) {
        LOG_BLE("ERROR: Blocking tare operation failed or timed out\n");
    } else {
        // Clear buffer after tare completes for clean measurements
        raw_filter.clear_all_samples();
        raw_filter.reset_display_filter();
    }
    
    LOG_LOADCELL_DEBUG("[DEBUG %lums] BLOCKING_TARE_COMPLETE: Tare operation completed\n", millis());
}

void WeightSensor::calibrate(float known_weight) {
#if DEBUG_ENABLE_LOADCELL_MOCK
    LOG_BLE("Mock load cell: calibration skipped (fixed factor %.2f)\n", cal_factor);
    return;
#endif
    if (known_weight <= 0) {
        LOG_BLE("ERROR: Invalid calibration weight\n");
        return;
    }
    
    LOG_BLE("Starting calibration with %.3fg weight...\n", known_weight);
    
    // First, wait for weight to settle (user just placed calibration weight)
    LOG_CALIBRATION_DEBUG("Waiting for calibration weight to settle...\n");
    get_precision_settled_weight(); // Use precision settling for calibration
    
    LOG_CALIBRATION_DEBUG("Weight settled, performing calibration...");
    
    // Now perform calibration with high accuracy - CircularBufferMath handles all filtering
    
    unsigned long cal_start = millis();
    while(!update_async() && millis() - cal_start < HW_CALIBRATION_TIMEOUT_MS) {
        delay(10);
    }
    
    // Calibration using raw ADC data - more precise than calibrated data
    int32_t raw_reading = raw_filter.get_raw_high_latency(); // Use high-latency for precision
    
    // Calculate new calibration factor: raw_change / weight_change
    // We know the weight change is known_weight (from 0 after taring)
    float new_cal_factor = (float)(raw_reading - tare_offset) / known_weight;
    cal_factor = new_cal_factor;
    
    save_calibration();
    save_calibration_weight(known_weight);
    
    // Clear buffer after calibration operation for clean measurements
    raw_filter.clear_all_samples();
    raw_filter.reset_display_filter();
    
    LOG_BLE("Calibration completed. New factor: %.2f\n", cal_factor);
}

void WeightSensor::set_calibration_factor(float factor) {
#if DEBUG_ENABLE_LOADCELL_MOCK
    cal_factor = DEBUG_MOCK_CAL_FACTOR;
    LOG_BLE("Mock load cell: ignoring calibration update, using fixed factor: %.2f\n", cal_factor);
#else
    cal_factor = factor;
#endif
}

void WeightSensor::set_zero_offset(int32_t offset) {
    tare_offset = offset;
}

void WeightSensor::update() {
    // Core 0 handles all HX711 sampling and tare logic is now in sample_and_feed_filter
    // This method now only updates timestamp
    unsigned long now = millis();
    last_update = now;
    
    // Note: Tare logic is now handled automatically in sample_and_feed_filter using exact HX711_ADC implementation
    
    // Note: HX711 data sampling, validation, and filter feeding is now handled by
    // the dedicated Core 0 sampling task; actual SPS depends on ADC hardware (e.g., 10 or 80 SPS)
}

bool WeightSensor::is_settled(uint32_t window_ms) {
    // Convert grams threshold to raw threshold and use CircularBufferMath
    int32_t raw_threshold = weight_to_raw_threshold(GRIND_SCALE_SETTLING_TOLERANCE_G);
    
    // Debug output for threshold conversion every 5s to avoid spam
    static uint32_t last_threshold_debug = 0;
    if (millis() - last_threshold_debug > 5000) {
        LOG_LOADCELL_DEBUG("[WeightSensor] Grams threshold: %.4fg -> Raw threshold: %ld (cal_factor: %.2f)\n",
                         GRIND_SCALE_SETTLING_TOLERANCE_G, (long)raw_threshold, cal_factor);
        last_threshold_debug = millis();
    }
    
    return raw_filter.is_settled(window_ms, raw_threshold);
}

// WARNING: This method blocks execution until weight settles or times out!
float WeightSensor::get_motor_settled_weight(float* settle_time_out) {
    return get_settled_weight(HW_MOTOR_SETTLING_TIME_MS, settle_time_out);
}

// WARNING: This method blocks execution until weight settles or times out!
float WeightSensor::get_precision_settled_weight(float* settle_time_out) {
    return get_settled_weight(HW_SCALE_PRECISION_SETTLING_TIME_MS, settle_time_out);
}

// WARNING: This method blocks execution until weight settles or times out!
float WeightSensor::get_settled_weight(uint32_t window_ms, float* settle_time_out) {
    LOG_SETTLING_DEBUG("Waiting for weight to settle (window=%lums, timeout=%lums)...\n", window_ms, HW_SCALE_SETTLING_TIMEOUT_MS);
    
    unsigned long start_time = millis();
    float settled_weight = 0.0f;
    
    // Blocking loop - keep updating and checking until settled or timeout
    while (millis() - start_time < HW_SCALE_SETTLING_TIMEOUT_MS) {
        // Call update to ensure fresh samples are collected
        update();
        
        if (check_settling_complete(window_ms, &settled_weight)) {
            // Calculate settle time
            if (settle_time_out) {
                *settle_time_out = millis() - start_time;
            }
            LOG_SETTLING_DEBUG("Weight settled in %lums\n", millis() - start_time);
            return settled_weight;
        }
        
        // Use load cell update interval from constants.h
        delay(SYS_TASK_LOADCELL_INTERVAL_MS);
    }
    
    // Timeout occurred - return best available measurement
    LOG_SETTLING_DEBUG("Weight settling timed out after %lums\n", HW_SCALE_SETTLING_TIMEOUT_MS);
    if (settle_time_out) {
        *settle_time_out = HW_SCALE_SETTLING_TIMEOUT_MS;
    }
    return raw_to_weight(raw_filter.get_smoothed_raw(window_ms));
}

// Raw ADC data access methods
int32_t WeightSensor::get_raw_adc_instant() const {
    return raw_filter.get_instant_raw();
}

int32_t WeightSensor::get_raw_adc_smoothed(uint32_t window_ms) const {
    return raw_filter.get_smoothed_raw(window_ms);
}

// Primary weight readings using CircularBufferMath with single conversion point
float WeightSensor::get_instant_weight() const {
    return raw_to_weight(raw_filter.get_instant_raw());
}

float WeightSensor::get_weight_low_latency() const {
    return raw_to_weight(raw_filter.get_raw_low_latency());
}

float WeightSensor::get_display_weight() {
    return raw_to_weight(raw_filter.get_display_raw());
}

float WeightSensor::get_weight_high_latency() const {
    return raw_to_weight(raw_filter.get_raw_high_latency());
}

// Flow rate analysis using CircularBufferMath - convert raw flow to weight flow
float WeightSensor::get_flow_rate(uint32_t window_ms) const {
    float raw_flow = raw_filter.get_raw_flow_rate(window_ms);
    return raw_flow / cal_factor;  // Convert raw units per second to grams per second
}

float WeightSensor::get_flow_rate_95th_percentile(uint32_t window_ms) const {
    float raw_flow = raw_filter.get_raw_flow_rate_95th_percentile(window_ms);
    return raw_flow / cal_factor;  // Convert raw units per second to grams per second
}

bool WeightSensor::is_flow_rate_stable(uint32_t window_ms) const {
    return raw_filter.raw_flowrate_is_stable(window_ms);
}

int WeightSensor::get_sample_count() const {
    return raw_filter.get_sample_count();
}

float WeightSensor::get_calibration_factor() {
    return cal_factor;
}

bool WeightSensor::is_initialized() {
    // WeightSensor is initialized once configuration is loaded
    // Hardware initialization happens on WeightSamplingTask Core 0
    return prefs != nullptr;
}

bool WeightSensor::data_ready() {
    return is_data_ready() && data_available;
}

bool WeightSensor::is_data_ready() const {
    return adc_driver ? adc_driver->is_ready() : false;
}

void WeightSensor::save_calibration() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    LOG_BLE("Mock load cell: calibration save skipped (fixed factor).\n");
    return;
#endif
    if (prefs) {
        prefs->putFloat("hx_cal", cal_factor);
    }
}

void WeightSensor::save_calibration_weight(float weight) {
#if DEBUG_ENABLE_LOADCELL_MOCK
    LOG_BLE("Mock load cell: calibration weight save skipped.\n");
    return;
#endif
    if (prefs) {
        prefs->putFloat("hx_wt", weight);
    }
}

float WeightSensor::get_saved_calibration_weight() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    return USER_CALIBRATION_REFERENCE_WEIGHT_G;
#endif
    if (prefs) {
        return prefs->getFloat("hx_wt", USER_CALIBRATION_REFERENCE_WEIGHT_G);
    }
    return USER_CALIBRATION_REFERENCE_WEIGHT_G;
}

void WeightSensor::load_calibration() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    cal_factor = DEBUG_MOCK_CAL_FACTOR;
    LOG_BLE("Mock load cell: using fixed calibration factor: %.2f\n", cal_factor);
    return;
#endif
    if (prefs) {
        float saved_factor = prefs->getFloat("hx_cal", USER_DEFAULT_CALIBRATION_FACTOR);
        
        // Check for corrupted/invalid calibration data
        if (isnan(saved_factor) || !isfinite(saved_factor) || saved_factor == 0.0) {
            LOG_BLE("WARNING: Invalid calibration factor detected, using default\n");
            saved_factor = USER_DEFAULT_CALIBRATION_FACTOR;
            // Clear corrupted data and save default
            prefs->putFloat("hx_cal", saved_factor);
        }
        
        cal_factor = saved_factor;
        LOG_BLE("Loaded calibration factor: %.2f\n", saved_factor);
    } else {
        cal_factor = USER_DEFAULT_CALIBRATION_FACTOR;
        LOG_BLE("Using default calibration factor: %.2f\n", USER_DEFAULT_CALIBRATION_FACTOR);
    }
}

void WeightSensor::clear_calibration_data() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    cal_factor = DEBUG_MOCK_CAL_FACTOR;
    LOG_BLE("Mock load cell: calibration data reset to fixed factor.\n");
    return;
#endif
    if (prefs) {
        LOG_BLE("Clearing corrupted calibration data...\n");
        prefs->remove("hx_cal");
        prefs->remove("hx_wt");
        cal_factor = USER_DEFAULT_CALIBRATION_FACTOR;
        LOG_BLE("Calibration data cleared, using defaults\n");
    }
}

// Non-blocking settling check with window_ms parameter
bool WeightSensor::check_settling_complete(uint32_t window_ms, float* settled_weight_out) {
    // Check if data has settled based on gram values
    if (is_settled(window_ms)) {
        if (settled_weight_out) {
            *settled_weight_out = raw_to_weight(raw_filter.get_smoothed_raw(window_ms));
        }
        LOG_SETTLING_DEBUG("[DEBUG %lums] SETTLING_COMPLETE: Weight settled (%.3fg, confidence=%.2f, window=%lums)\n", 
                      millis(), settled_weight_out ? *settled_weight_out : raw_to_weight(raw_filter.get_smoothed_raw(window_ms)), 
                      raw_filter.get_settling_confidence(window_ms), window_ms);
        return true;
    }
    
    return false; // Still settling
}

void WeightSensor::cancel_settling() {
    // Settling cancellation no longer needed with direct window_ms approach
    LOG_SETTLING_DEBUG("[DEBUG %lums] SETTLING_CANCEL: Settling cancelled\n", millis());
}

//==============================================================================
// WEIGHT SAMPLING TASK INTEGRATION
//==============================================================================

bool WeightSensor::sample_and_feed_filter() {
    // Core 0 ADC sampling and filter feeding (hardware-abstracted)
    // Returns true if new sample was processed, false if no data available
    
    if (!adc_driver) {
        return false; // No ADC driver available
    }
    
    // Non-blocking check for available ADC data
    if (data_waiting_async()) {
        update_async();
        int32_t raw_adc = get_raw_adc_data();  // Get raw ADC data from driver
        uint32_t timestamp = millis();
        
        // Raw ADC validation (24-bit range - valid for all supported ADCs)
        if (raw_adc >= 0 && raw_adc <= 0xFFFFFF) {  // Valid 24-bit range
            // Thread-safe sample feeding (CircularBufferMath is single-producer safe)
            raw_filter.add_sample(raw_adc, timestamp);
            
            // Tare logic (hardware-independent)
            if (doTare) {
                if (tareTimes < DATA_SET) {
                    tareTimes++;
                } else {
                    // Use CircularBufferMath smoothed data instead of original smoothedData()
                    int32_t smoothed_raw = raw_filter.get_smoothed_raw(250); // 250ms window for stability
                    tare_offset = smoothed_raw;  // Set tare offset to smoothed raw ADC value
                    tareTimes = 0;
                    doTare = 0;
                    tareStatus = 1;
                }
            }
            
            // Update instance variables atomically (ESP32 guarantees atomic 32-bit writes)
            current_raw_adc = raw_adc;
            current_weight = raw_to_weight(raw_adc);  // Convert using WeightSensor calibration
            
            // Check for significant weight changes (for screen timeout reset)
            float weight_change = fabs(current_weight - last_significant_weight);
            if (weight_change >= USER_WEIGHT_ACTIVITY_THRESHOLD_G) {
                last_significant_weight = current_weight;
                last_weight_activity_time = timestamp;
            }
            
            // Update temperature if available
            update_temperature_if_available();
            
            data_available = true;
            
            return true; // Successfully processed new sample
        } else {
            // Debug invalid readings
            static uint32_t last_invalid_debug = 0;
            if (timestamp - last_invalid_debug > 5000) {
                LOG_BLE("WeightSensor: Invalid raw ADC reading detected - raw=%ld (expected range: 0x000000 to 0xFFFFFF)\n", 
                       (long)raw_adc);
                last_invalid_debug = timestamp;
            }
        }
    }
    
    return false; // No new data available
}

float WeightSensor::get_saved_calibration_factor() {
    // Return saved calibration factor from preferences, or default if none
    if (prefs && prefs->isKey("hx_cal")) {
        float saved_factor = prefs->getFloat("hx_cal", USER_DEFAULT_CALIBRATION_FACTOR);
        
        // Validate saved factor
        if (isnan(saved_factor) || !isfinite(saved_factor) || saved_factor == 0.0) {
            return USER_DEFAULT_CALIBRATION_FACTOR;
        }
        return saved_factor;
    }
    return USER_DEFAULT_CALIBRATION_FACTOR;
}

#if ENABLE_REALTIME_HEARTBEAT
void WeightSensor::record_sample_timestamp() {
    uint32_t now = millis();
    
    // Store timestamp in circular buffer
    sps_timestamps[sps_buffer_index] = now;
    sps_buffer_index = (sps_buffer_index + 1) % SPS_TRACKING_BUFFER_SIZE;
    
    // Track how many samples we have (up to buffer size)
    if (sps_sample_count < SPS_TRACKING_BUFFER_SIZE) {
        sps_sample_count++;
    }
}

float WeightSensor::get_current_sps() const {
    if (sps_sample_count < 2) {
        return 0.0f; // Need at least 2 samples to calculate rate
    }
    
    uint32_t now = millis();
    int samples_in_window = 0;
    uint32_t window_start = now - 2000; // 2 second window
    
    // Count samples in the last 2 seconds
    for (int i = 0; i < sps_sample_count; i++) {
        if (sps_timestamps[i] >= window_start) {
            samples_in_window++;
        }
    }
    
    // Convert to samples per second
    return samples_in_window / 2.0f;
}
#endif

//==============================================================================
// WEIGHT ACTIVITY TRACKING
//==============================================================================

uint32_t WeightSensor::get_ms_since_last_weight_activity() const {
    uint32_t now = millis();
    // Handle millis() overflow (happens approximately every 50 days)
    if (now >= last_weight_activity_time) {
        return now - last_weight_activity_time;
    } else {
        // millis() has overflowed
        return (UINT32_MAX - last_weight_activity_time) + now + 1;
    }
}

// HX711_ADC exact tare methods
void WeightSensor::tareNoDelay() {
    doTare = 1;
    tareTimes = 0;
    tareStatus = 0;
}

bool WeightSensor::getTareStatus() {
    bool t = tareStatus;
    tareStatus = 0;
    return t;
}
