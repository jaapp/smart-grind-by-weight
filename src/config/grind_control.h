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
#define GRIND_ACCURACY_TOLERANCE_G 0.05f                                  // Final target accuracy tolerance
#define GRIND_TIMEOUT_SEC 30                                              // Maximum time for grind operation
#define GRIND_MAX_PULSE_ATTEMPTS 10                                       // Maximum pulse corrections before stopping

// Flow rate detection
#define GRIND_FLOW_DETECTION_THRESHOLD_GPS 0.5f                           // Minimum coffee flow rate to establish first grinds reachinig the cup = latency

// Undershoot strategy - determine when to stop grinding during the predictive phase
#define GRIND_UNDERSHOOT_TARGET_G 1.0f                                    // Default conservative undershoot target
#define GRIND_LATENCY_TO_COAST_RATIO 1.0f                                 // Ratio of expected coast time to measured latency (e.g., 0.8 = 80%)

//------------------------------------------------------------------------------
// SCALE CALIBRATION AND SETTLING
//------------------------------------------------------------------------------
// Tare and settling behavior  
#define GRIND_SCALE_SETTLING_TOLERANCE_G 0.005f                           // Maximum standard deviation for settled reading. Used to determine if scale is settled. Increase value if you have a noisy load cell.

//------------------------------------------------------------------------------
// TIME MODE PULSE SETTINGS
//------------------------------------------------------------------------------
#define GRIND_TIME_PULSE_DURATION_MS 100                                        // Duration of additional pulses in time mode (milliseconds)



//------------------------------------------------------------------------------
// FLOW RATE PARAMETERS
//------------------------------------------------------------------------------
#define GRIND_FLOW_RATE_MIN_SANE_GPS 1.0f                                         // Minimum reasonable flow rate
#define GRIND_FLOW_RATE_MAX_SANE_GPS 3.0f                                         // Maximum reasonable flow rate
#define GRIND_FLOW_RATE_REFERENCE_GPS 1.5f                                        // Reference flow rate for calibration

//------------------------------------------------------------------------------
// TIMING CONSTRAINTS (Hardware-dependent)
//------------------------------------------------------------------------------
// Motor timing
#define GRIND_MOTOR_MIN_PULSE_DURATION_MS 75.0f                                   // Minimum motor pulse duration
#define GRIND_MOTOR_MAX_PULSE_DURATION_MS 300.0f                                  // Maximum motor pulse duration  
#define GRIND_MOTOR_SETTLING_TIME_MS 200                                          // Motor vibration settling time

// Scale settling timing
#define GRIND_SCALE_PRECISION_SETTLING_TIME_MS 500                                // High-precision settling time
#define GRIND_SCALE_SETTLING_TIMEOUT_MS 10000                                     // Maximum time to wait for settling

// Tare and calibration timing (hardware sample rate dependent)
#define GRIND_TARE_SAMPLE_WINDOW_MS 500                                           // Time window for tare sampling
#define GRIND_TARE_TIMEOUT_MS 3000                                                // Maximum tare completion time
#define GRIND_CALIBRATION_SAMPLE_WINDOW_MS 800                                    // Time window for calibration sampling  
#define GRIND_CALIBRATION_TIMEOUT_MS 2000                                         // Maximum calibration completion time

// Calculated sample counts based on hardware rate
#define GRIND_TARE_SAMPLE_COUNT (GRIND_TARE_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)
#define GRIND_CALIBRATION_SAMPLE_COUNT (GRIND_CALIBRATION_SAMPLE_WINDOW_MS / HW_LOADCELL_SAMPLE_INTERVAL_MS)

