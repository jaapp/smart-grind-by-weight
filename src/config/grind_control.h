#pragma once

//==============================================================================
// GRIND CONTROL CONFIGURATION
//==============================================================================
// This file contains all grind control related configuration constants
// including timing parameters, accuracy settings, flow detection, and
// pulse control algorithms.

//------------------------------------------------------------------------------
// GRIND CONTROL TUNING
//------------------------------------------------------------------------------
// Main accuracy and timeout settings
#define USER_GRIND_ACCURACY_TOLERANCE_G 0.05f                                  // Final target accuracy tolerance
#define USER_GRIND_TIMEOUT_SEC 30                                              // Maximum time for grind operation
#define USER_GRIND_MAX_PULSE_ATTEMPTS 10                                       // Maximum pulse corrections before stopping

// Flow rate detection
#define USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS 0.5f                           // Minimum coffee flow rate to establish grinding

// Undershoot strategy  
#define USER_GRIND_UNDERSHOOT_TARGET_G 1.0f                                    // Default conservative undershoot target
#define USER_LATENCY_TO_COAST_RATIO 1.0f                                       // Ratio of expected coast time to measured latency (e.g., 0.8 = 80%)

//------------------------------------------------------------------------------
// SCALE CALIBRATION AND SETTLING
//------------------------------------------------------------------------------
// Tare and settling behavior  
#define USER_SCALE_SETTLING_TOLERANCE_G 0.005f                                 // Maximum standard deviation for settled reading

//------------------------------------------------------------------------------
// TIME MODE PULSE SETTINGS
//------------------------------------------------------------------------------
#define USER_TIME_PULSE_DURATION_MS 100                                        // Duration of additional pulses in time mode (milliseconds)

//------------------------------------------------------------------------------
// PULSE DURATION MAPPING
//------------------------------------------------------------------------------
// Motor pulse durations for different error magnitudes
#define HW_PULSE_LARGE_ERROR_MS 240.0f                                         // Motor pulse for errors > 1.0g
#define HW_PULSE_MEDIUM_ERROR_MS 120.0f                                        // Motor pulse for errors > 0.5g
#define HW_PULSE_SMALL_ERROR_MS 80.0f                                          // Motor pulse for errors > 0.1g  
#define HW_PULSE_FINE_ERROR_MS 40.0f                                           // Motor pulse for errors ≤ 0.1g

//------------------------------------------------------------------------------
// FLOW RATE PARAMETERS
//------------------------------------------------------------------------------
#define HW_FLOW_RATE_MIN_SANE_GPS 1.0f                                         // Minimum reasonable flow rate
#define HW_FLOW_RATE_MAX_SANE_GPS 3.0f                                         // Maximum reasonable flow rate
#define HW_FLOW_RATE_REFERENCE_GPS 1.5f                                        // Reference flow rate for calibration

//------------------------------------------------------------------------------
// SYSTEM ALGORITHM PARAMETERS  
//------------------------------------------------------------------------------
// Error thresholds for pulse duration calculation
#define SYS_GRIND_ERROR_LARGE_THRESHOLD_G 1.0f                                 // Large error threshold
#define SYS_GRIND_ERROR_MEDIUM_THRESHOLD_G 0.5f                                // Medium error threshold  
#define SYS_GRIND_ERROR_SMALL_THRESHOLD_G 0.2f                                 // Small error threshold

// Dynamic pulse algorithm parameters
#define SYS_GRIND_SMALL_ERROR_FACTOR 1.0f                                      // Gentler correction factor for small errors

//------------------------------------------------------------------------------
// SYSTEM TIMEOUTS
//------------------------------------------------------------------------------
#define SYS_TIMEOUT_SHORT_MS 1000                                              // General short timeout for quick operations
#define SYS_TIMEOUT_MEDIUM_MS 2000                                             // Medium timeout for moderate operations  
#define SYS_TIMEOUT_LONG_MS 3000                                               // Long timeout for settling operations
#define SYS_TIMEOUT_EXTENDED_MS 5000                                           // Extended timeout for complex operations

//------------------------------------------------------------------------------
// INTER-TASK COMMUNICATION
//------------------------------------------------------------------------------
#define SYS_QUEUE_UI_TO_GRIND_SIZE 5                                           // UI events to grind controller

//------------------------------------------------------------------------------
// LOGGING CONFIGURATION
//------------------------------------------------------------------------------
#define SYS_LOG_EVERY_N_GRIND_LOOPS 1                                          // Log frequency for grind control loop

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
#define HW_TARE_TIMEOUT_MS 3000                                                // Maximum tare completion time (raised for 10 SPS)
#define HW_CALIBRATION_SAMPLE_WINDOW_MS 800                                    // Time window for calibration sampling  
#define HW_CALIBRATION_TIMEOUT_MS 2000                                         // Maximum calibration completion time

// Calculated sample counts based on hardware rate
#define HW_TARE_SAMPLE_COUNT (HW_TARE_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)
#define HW_CALIBRATION_SAMPLE_COUNT (HW_CALIBRATION_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)



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
#define HW_TARE_TIMEOUT_MS 3000                                                // Maximum tare completion time (raised for 10 SPS)
#define HW_CALIBRATION_SAMPLE_WINDOW_MS 800                                    // Time window for calibration sampling  
#define HW_CALIBRATION_TIMEOUT_MS 2000                                         // Maximum calibration completion time

// Calculated sample counts based on hardware rate
#define HW_TARE_SAMPLE_COUNT (HW_TARE_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)
#define HW_CALIBRATION_SAMPLE_COUNT (HW_CALIBRATION_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)

