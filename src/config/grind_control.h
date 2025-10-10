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
#define GRIND_ACCURACY_TOLERANCE_G 0.03f                                  // Final target accuracy tolerance
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
#define GRIND_SCALE_SETTLING_TOLERANCE_G 0.010f                           // Maximum standard deviation for settled reading. Used to determine if scale is settled. Increase value if you have a noisy load cell.

//------------------------------------------------------------------------------
// TIME MODE PULSE SETTINGS
//------------------------------------------------------------------------------
#define GRIND_TIME_PULSE_DURATION_MS 100                                        // Duration of additional pulses in time mode (milliseconds)



//------------------------------------------------------------------------------
// FLOW RATE PARAMETERS
//------------------------------------------------------------------------------
#define GRIND_FLOW_RATE_MIN_SANE_GPS 1.0f                                         // Minimum reasonable flow rate
#define GRIND_FLOW_RATE_MAX_SANE_GPS 3.0f                                         // Maximum reasonable flow rate
#define GRIND_PULSE_FLOW_RATE_FALLBACK_GPS 1.5f                                   // Fallback pulse flow rate when measured rate is invalid or too low

//------------------------------------------------------------------------------
// TIMING CONSTRAINTS (Hardware-dependent)
//------------------------------------------------------------------------------
// Motor response latency - runtime configurable via auto-tune
#define GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS 75.0f                             // Safe default motor response latency
#define GRIND_MOTOR_RESPONSE_LATENCY_MIN_MS 30.0f                                 // Lower search bound for auto-tune
#define GRIND_MOTOR_RESPONSE_LATENCY_MAX_MS 200.0f                                // Upper search bound for auto-tune
#define GRIND_MOTOR_MAX_PULSE_EXTENSION_MS 225.0f                                 // Extension above latency for max pulse (latency + 225ms)

// Motor timing
#define GRIND_MOTOR_SETTLING_TIME_MS 200                                          // Motor vibration settling time

// Mechanical instability detection
#define GRIND_MECHANICAL_DROP_THRESHOLD_G 0.4f                                    // Weight drop considered mechanical instability
#define GRIND_MECHANICAL_EVENT_COOLDOWN_MS 200                                    // Minimum time between detecting drops
#define GRIND_MECHANICAL_EVENT_REQUIRED_COUNT 3                                   // Events required to flag diagnostic

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

//------------------------------------------------------------------------------
// MOTOR RESPONSE AUTO-TUNE ALGORITHM
//------------------------------------------------------------------------------
#define GRIND_AUTOTUNE_PRIMING_PULSE_MS 500                                       // Initial chute priming pulse
#define GRIND_AUTOTUNE_TARGET_ACCURACY_MS 5.0f                                   // Target resolution (10ms steps)
#define GRIND_AUTOTUNE_SUCCESS_RATE 0.80f                                         // 80% success threshold (4/5 pulses)
#define GRIND_AUTOTUNE_VERIFICATION_PULSES 5                                      // Verification attempts per candidate
#define GRIND_AUTOTUNE_MAX_ITERATIONS 50                                          // Hard stop safety limit
#define GRIND_AUTOTUNE_COLLECTION_DELAY_MS 1500                                   // Minimum wait after pulse for grounds to drop
#define GRIND_AUTOTUNE_SETTLING_TIMEOUT_MS 5000                                   // Max wait per pulse for scale settling
#define GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G GRIND_SCALE_SETTLING_TOLERANCE_G        // 0.010g detection threshold
