#pragma once

//==============================================================================
// HARDWARE CONFIGURATION CONSTANTS
//==============================================================================
// This file contains hardware-specific configuration values that define the
// physical characteristics and constraints of the ESP32-S3 coffee grinder system.
// These values are tied to the specific hardware implementation and should only
// be modified when changing hardware components.
//
// Categories:
// - Pin Assignments (GPIO mappings for all peripherals)
// - Hardware Timing Constraints (pulse durations, settling times)
// - Sensor Specifications (load cell rates, touch controller settings)
// - Display Hardware Settings (dimensions, rotation, pin connections)
// - Motor Control Parameters (pulse limits, relay control)
//
// All constants use the HW_ prefix to indicate hardware-specific parameters.

//------------------------------------------------------------------------------
// GPIO PIN ASSIGNMENTS
//------------------------------------------------------------------------------
// Touch Controller (I2C)
#define HW_TOUCH_I2C_SDA_PIN 47                                                // I2C data pin for capacitive touch controller
#define HW_TOUCH_I2C_SCL_PIN 48                                                // I2C clock pin for capacitive touch controller
#define HW_TOUCH_I2C_ADDRESS 0x38                                              // I2C address of FT3168 touch controller

// Display Controller (QSPI)
#define HW_DISPLAY_CS_PIN 9                                                    // SPI chip select for display controller
#define HW_DISPLAY_SCK_PIN 10                                                  // SPI clock for display controller
#define HW_DISPLAY_D0_PIN 11                                                   // SPI data line 0 (QSPI mode)
#define HW_DISPLAY_D1_PIN 12                                                   // SPI data line 1 (QSPI mode)
#define HW_DISPLAY_D2_PIN 13                                                   // SPI data line 2 (QSPI mode)
#define HW_DISPLAY_D3_PIN 14                                                   // SPI data line 3 (QSPI mode)
#define HW_DISPLAY_RESET_PIN 21                                                // Display reset pin

// Load Cell ADC Configuration (HX711 only)

// Mock driver configuration (compile-time selectable)
#define HW_ENABLE_LOADCELL_MOCK 0                                            // 0=use physical HX711, 1=use simulated mock driver
#define HW_MOCK_FLOW_RATE_GPS 1.9f                                           // Simulated continuous flow rate in grams per second
#define HW_MOCK_CAL_FACTOR -7050.0f                                          // Fixed calibration factor used during mocking
#define HW_MOCK_BASELINE_RAW 0x700000                                        // Baseline raw count around mid-scale for tare offset
#define HW_MOCK_IDLE_NOISE_RAW 60.0f                                         // Peak raw noise when idle (counts)
#define HW_MOCK_GRIND_NOISE_RAW 400.0f                                       // Peak raw noise when grinding (counts)
#define HW_MOCK_FLOW_RAMP_MS 350                                             // Ramp duration for simulated flow transitions
#define HW_MOCK_START_DELAY_MS 500                                           // Delay from motor start command to weight increase
#define HW_MOCK_STOP_DELAY_MS 400                                            // Delay from motor stop command to weight stop

// Load Cell ADC Pins
#define HW_LOADCELL_DOUT_PIN 3                                                 // HX711 data output pin
#define HW_LOADCELL_SCK_PIN 2                                                  // HX711 serial clock pin

// Motor Control
#define HW_MOTOR_RELAY_PIN 18                                                  // GPIO pin for grinder motor control relay

//------------------------------------------------------------------------------
// LOAD CELL ADC SPECIFICATIONS
//------------------------------------------------------------------------------

// Sample rate configuration
// HX711: Only supports 10 SPS or 80 SPS
#define HW_LOADCELL_SAMPLE_RATE_SPS 10                                         // Current sample rate setting

#define HW_LOADCELL_SAMPLE_INTERVAL_MS (1000 / HW_LOADCELL_SAMPLE_RATE_SPS)   // Calculated sample interval
#define HW_LOADCELL_STABILIZATION_READINGS 5                                   // Readings to discard after power-on

// Hardware capability flags
#define HW_LOADCELL_HAS_TEMPERATURE_SENSOR 0                                   // HX711 has no temperature sensor

// Single-Pole IIR Filter for noise reduction (compile-time optional)
#define HW_LOADCELL_ENABLE_IIR_FILTER 0                                        // Enable/disable IIR filter (0=disabled, 1=enabled). Disable for 10SPS mode
#define HW_LOADCELL_IIR_ALPHA 0.10f                                            // IIR filter coefficient (lower = more filtering)

//------------------------------------------------------------------------------
// DISPLAY SPECIFICATIONS  
//------------------------------------------------------------------------------
#define HW_DISPLAY_WIDTH_PX 280                                                // LCD width in pixels
#define HW_DISPLAY_HEIGHT_PX 456                                               // LCD height in pixels
#define HW_DISPLAY_OFFSET_X_PX 0                                               // X offset for display positioning
#define HW_DISPLAY_OFFSET_Y_PX 0                                               // Y offset for display positioning
#define HW_DISPLAY_ROTATION_DEG 0                                              // Display rotation angle
#define HW_DISPLAY_IPS_INVERT_X 180                                            // IPS X-axis inversion setting
#define HW_DISPLAY_IPS_INVERT_Y 24                                             // IPS Y-axis inversion setting
#define HW_DISPLAY_COLOR_ORDER 20                                              // Color channel ordering
#define HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT 15                                       // Minimum brightness percentage (to avoid too dim to see)
//------------------------------------------------------------------------------
// SERIAL COMMUNICATION
//------------------------------------------------------------------------------
#define HW_SERIAL_BAUD_RATE 115200                                             // UART baud rate for debug/logging output

//------------------------------------------------------------------------------
// POWER MANAGEMENT
//------------------------------------------------------------------------------
#define HW_CPU_FREQ_NORMAL_MHZ 240                                             // Normal operating CPU frequency
#define HW_CPU_FREQ_BLE_MODE_MHZ 240                                           // CPU frequency during BLE operations

//------------------------------------------------------------------------------
// TIMING CONSTRAINTS (Hardware-dependent)
//------------------------------------------------------------------------------
// Motor timing
#define HW_MOTOR_MIN_PULSE_DURATION_MS 75.0f                                   // Minimum motor pulse duration
#define HW_MOTOR_MAX_PULSE_DURATION_MS 300.0f                                  // Maximum motor pulse duration  
#define HW_MOTOR_SETTLING_TIME_MS 200                                          // Motor vibration settling time

// Scale settling timing
#define HW_SCALE_PRECISION_SETTLING_TIME_MS 500                                // High-precision settling time
#define HW_SCALE_SETTLING_TIMEOUT_MS 10000                                     // Maximum time to wait for settling

// Tare and calibration timing (hardware sample rate dependent)
#define HW_TARE_SAMPLE_WINDOW_MS 500                                           // Time window for tare sampling
// At 10 SPS the HX711-based tare routine needs ~18 samples
// (16 + 2 discard), which is ~1.8s plus scheduling overhead.
// Give comfortable headroom to avoid spurious timeouts.
#define HW_TARE_TIMEOUT_MS 3000                                                // Maximum tare completion time (raised for 10 SPS)
#define HW_CALIBRATION_SAMPLE_WINDOW_MS 800                                    // Time window for calibration sampling  
#define HW_CALIBRATION_TIMEOUT_MS 2000                                         // Maximum calibration completion time

// Calculated sample counts based on hardware rate
#define HW_TARE_SAMPLE_COUNT (HW_TARE_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)
#define HW_CALIBRATION_SAMPLE_COUNT (HW_CALIBRATION_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)

//------------------------------------------------------------------------------
// PULSE DURATION MAPPING
//------------------------------------------------------------------------------
// Motor pulse durations for different error magnitudes
#define HW_PULSE_LARGE_ERROR_MS 240.0f                                         // Motor pulse for errors > 1.0g
#define HW_PULSE_MEDIUM_ERROR_MS 120.0f                                        // Motor pulse for errors > 0.5g
#define HW_PULSE_SMALL_ERROR_MS 80.0f                                          // Motor pulse for errors > 0.1g  
#define HW_PULSE_FINE_ERROR_MS 40.0f                                           // Motor pulse for errors ≤ 0.1g

//------------------------------------------------------------------------------
// SENSOR LIMITS AND RANGES
//------------------------------------------------------------------------------
#define HW_FLOW_RATE_MIN_SANE_GPS 1.0f                                         // Minimum reasonable flow rate
#define HW_FLOW_RATE_MAX_SANE_GPS 3.0f                                         // Maximum reasonable flow rate
#define HW_FLOW_RATE_REFERENCE_GPS 1.5f                                        // Reference flow rate for calibration
