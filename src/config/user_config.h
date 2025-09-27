#pragma once

//==============================================================================
// USER CONFIGURATION PARAMETERS
//==============================================================================
// This file contains user-configurable parameters that affect coffee grinding
// behavior, UI responsiveness, and device operation. These are the primary
// settings that users might want to modify to customize their grinder.
//
// Categories:
// - Coffee Profile Settings (weights, counts, names)
// - Grind Control Parameters (tolerances, timeouts, flow rates)
// - User Interface Responsiveness (jog acceleration, settling tolerances)
// - Calibration and Tare Settings (reference weights, confirmation thresholds)
//
// All constants use the USER_ prefix to indicate user-configurable parameters.

//------------------------------------------------------------------------------
// COFFEE PROFILES
//------------------------------------------------------------------------------
#define USER_PROFILE_COUNT 3                                                   // Number of coffee profiles available
#define USER_PROFILE_NAME_MAX_LENGTH 8                                         // Maximum characters in profile name

// Default target weights for each profile
#define USER_SINGLE_ESPRESSO_WEIGHT_G 9.0f                                     // Single espresso default weight
#define USER_DOUBLE_ESPRESSO_WEIGHT_G 18.0f                                    // Double espresso default weight  
#define USER_CUSTOM_PROFILE_WEIGHT_G 21.5f                                     // Custom profile default weight

#define USER_SINGLE_ESPRESSO_TIME_S 5.0f                                       // Single espresso default grind time
#define USER_DOUBLE_ESPRESSO_TIME_S 10.0f                                      // Double espresso default grind time
#define USER_CUSTOM_PROFILE_TIME_S 12.0f                                       // Custom profile default grind time

// Weight limits
#define USER_MIN_TARGET_WEIGHT_G 5.0f                                          // Minimum allowed target weight
#define USER_MAX_TARGET_WEIGHT_G 500.0f                                        // Maximum allowed target weight

#define USER_MIN_TARGET_TIME_S 0.5f                                            // Minimum allowed target time
#define USER_MAX_TARGET_TIME_S 60.0f                                           // Maximum allowed target time

#define USER_FINE_TIME_ADJUSTMENT_S 0.1f                                       // Fine adjustment step for time editing

//------------------------------------------------------------------------------
// GRIND CONTROL TUNING
//------------------------------------------------------------------------------
// Main accuracy and timeout settings
#define USER_GRIND_ACCURACY_TOLERANCE_G 0.05f                                  // Final target accuracy tolerance
#define USER_GRIND_TIMEOUT_SEC 30                                              // Maximum time for grind operation
#define USER_GRIND_MAX_PULSE_ATTEMPTS 10                                       // Maximum pulse corrections before stopping

// Flow rate detection
#define USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS 0.5f                           // Minimum coffee flow rate to establish grinding
#define USER_GRIND_FLOW_START_DETECTION_THRESHOLD_G 0.15f                      // The minimum weight to detect flow start - used to determine grind latency
#define USER_GRIND_LATENCY_CONFIRMATION_WINDOW_MS 300                          // Time window to confirm flow start after initial detection
#define FLOW_START_CANDIDATE_BUFFER_SIZE 20

// Undershoot strategy  
#define USER_GRIND_UNDERSHOOT_TARGET_G 1.0f                                    // Default conservative undershoot target
#define USER_LATENCY_TO_COAST_RATIO 1.0f                                       // Ratio of expected coast time to measured latency (e.g., 0.8 = 80%)

//------------------------------------------------------------------------------
// WEIGHT ADJUSTMENTS
//------------------------------------------------------------------------------
#define USER_FINE_WEIGHT_ADJUSTMENT_G 0.1f                                     // Small weight increment for fine tuning
#define USER_COARSE_WEIGHT_ADJUSTMENT_G 0.5f                                   // Large weight increment for quick changes

//------------------------------------------------------------------------------
// UI RESPONSIVENESS
//------------------------------------------------------------------------------
// Button jog/hold acceleration settings
#define USER_JOG_STAGE_1_INTERVAL_MS 100                                       // Stage 1: slow jog interval
#define USER_JOG_STAGE_2_THRESHOLD_MS 2000                                     // Time to reach stage 2 acceleration
#define USER_JOG_STAGE_3_THRESHOLD_MS 4000                                     // Time to reach stage 3 acceleration
#define USER_JOG_STAGE_4_THRESHOLD_MS 6000                                     // Time to reach stage 4 acceleration

// Display update rates (lower = more responsive, higher = better performance)
#define USER_UI_NORMAL_UPDATE_INTERVAL_MS 16                                   // UI update frequency during normal operation (60 FPS)
#define USER_UI_GRINDING_UPDATE_INTERVAL_MS 100                                // Reduced UI frequency during grinding (10 FPS)

//------------------------------------------------------------------------------
// SCALE CALIBRATION
//------------------------------------------------------------------------------
#define USER_CALIBRATION_REFERENCE_WEIGHT_G 100.0f                             // Reference weight for calibration
#define USER_DEFAULT_CALIBRATION_FACTOR -7050.0f                               // Default load cell calibration factor

// Tare and settling behavior  
#define USER_TARE_CONFIRM_THRESHOLD_G 0.01f                                    // Weight must be below this after taring
#define USER_SCALE_SETTLING_TOLERANCE_G 0.005f                                  // Maximum standard deviation for settled reading

//------------------------------------------------------------------------------
// DEBUG AND MONITORING
//------------------------------------------------------------------------------
// Enable/disable debug output for different subsystems
#define USER_DEBUG_SERIAL_OUTPUT 1                                             // Enable serial debug output
#define USER_DEBUG_GRIND_CONTROLLER 0                                          // Enable detailed grind debugging
#define USER_DEBUG_LOAD_CELL 0                                                 // Enable detailed load cell debugging  
#define USER_DEBUG_UI_SYSTEM 0                                                 // Enable detailed UI debugging
#define USER_DEBUG_CALIBRATION 0                                               // Enable detailed calibration debugging
#define USER_DEBUG_WEIGHT_SETTLING 0                                           // Enable weight settling debugging

// Performance monitoring
#define USER_ENABLE_PERFORMANCE_MONITORING 0                                   // Enable performance metrics (disable for release)

//------------------------------------------------------------------------------
// SCREEN TIMEOUT
//------------------------------------------------------------------------------
#define USER_SCREEN_TIMEOUT_MS 300000                                          // Screen timeout duration
#define USER_SCREEN_BRIGHTNESS_NORMAL 1.0f                                     // Normal screen brightness
#define USER_SCREEN_BRIGHTNESS_DIMMED 0.35f                                     // Dimmed screen brightness
#define USER_WEIGHT_ACTIVITY_THRESHOLD_G 1.0f                                  // Weight change threshold for screen timeout reset (grams)

//------------------------------------------------------------------------------
// UI VISUAL FEEDBACK
//------------------------------------------------------------------------------
#define USER_ENABLE_GRINDER_BACKGROUND_INDICATOR 0                            // 0=disabled, 1=show background color during grinding/pulses

//------------------------------------------------------------------------------
// TIME MODE PULSE SETTINGS
//------------------------------------------------------------------------------
#define USER_TIME_PULSE_DURATION_MS 100                                       // Duration of additional pulses in time mode (milliseconds)

//------------------------------------------------------------------------------
// BLUETOOTH TIMEOUTS
//------------------------------------------------------------------------------
#define USER_BLE_AUTO_DISABLE_TIMEOUT_MIN 30                                   // Auto-disable BLE after inactivity
#define USER_BLE_BOOTUP_TIMEOUT_MIN 5                                          // Auto-disable BLE after bootup period
