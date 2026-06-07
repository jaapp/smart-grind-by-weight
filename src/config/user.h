#pragma once

//==============================================================================
// USER CONFIGURATION PARAMETERS
//==============================================================================
// This file contains user-configurable parameters that affect coffee grinding
// behavior, UI responsiveness, and device operation. These are the primary
// settings that users might want to modify to customize their grinder.

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
#define USER_MAX_TARGET_WEIGHT_G 1000.0f                                        // Maximum allowed target weight

#define USER_MIN_TARGET_TIME_S 0.5f                                            // Minimum allowed target time
#define USER_MAX_TARGET_TIME_S 25.0f                                           // Maximum allowed target time

//------------------------------------------------------------------------------
// WEIGHT/TIME ADJUSTMENTS
//------------------------------------------------------------------------------
#define USER_FINE_WEIGHT_ADJUSTMENT_G 0.1f                                     // Small weight increment for fine tuning
#define USER_FINE_TIME_ADJUSTMENT_S 0.1f                                       // Fine adjustment step for time editing

// USER_JOG parameters moved to system.h to be near SYS_JOG parameters

//------------------------------------------------------------------------------
// SCALE CALIBRATION
//------------------------------------------------------------------------------
#define USER_CALIBRATION_REFERENCE_WEIGHT_G 100.0f                             // Default reference weight for calibration
#define USER_DEFAULT_CALIBRATION_FACTOR -7050.0f                               // Default load cell calibration factor

//------------------------------------------------------------------------------
// SCREEN AUTO-DIMMING
//------------------------------------------------------------------------------
#define USER_SCREEN_AUTO_DIM_TIMEOUT_DEFAULT_MS 300000                         // Default time before screen dims due to inactivity
#define USER_SCREEN_AUTO_DIM_TIMEOUT_MIN_MS 60000                              // Minimum configurable auto-dim delay
#define USER_SCREEN_AUTO_DIM_TIMEOUT_MAX_MS 3600000                            // Maximum configurable auto-dim delay
#define USER_SCREEN_AUTO_DIM_TIMEOUT_MS USER_SCREEN_AUTO_DIM_TIMEOUT_DEFAULT_MS // Backward-compatible auto-dim default
#define USER_SCREEN_BRIGHTNESS_NORMAL 1.0f                                     // Normal screen brightness
#define USER_SCREEN_BRIGHTNESS_DIMMED 0.35f                                    // Dimmed screen brightness
#define USER_WEIGHT_ACTIVITY_THRESHOLD_G 1.0f                                  // Weight change threshold for screen timeout reset (grams)

#define USER_READY_UI_PREF_NAMESPACE "ready_ui"                                // NVS namespace for ready screen layout
#define USER_READY_UI_PREF_KEY_ADVANCED "advanced"                             // Use compact bean-tracking ready UI
#define USER_READY_UI_ADVANCED_DEFAULT true                                    // Default to the bean-tracking UI for this feature branch

#define USER_SCREENSAVER_PREF_NAMESPACE "screensaver"                          // NVS namespace for screensaver settings
#define USER_SCREENSAVER_PREF_KEY_STARTUP "startup"                            // Show image during startup
#define USER_SCREENSAVER_PREF_KEY_SLEEP "sleep"                                // Show image during screen dim
#define USER_SCREENSAVER_PREF_KEY_STARTUP_DURATION_MS "startup_ms"              // Startup image duration
#define USER_SCREENSAVER_PREF_KEY_SLEEP_DELAY_MS "sleep_ms"                    // Auto-dim/screensaver delay
#define USER_SCREENSAVER_STARTUP_DURATION_DEFAULT_MS 3000                      // Default startup screensaver duration
#define USER_SCREENSAVER_STARTUP_DURATION_MIN_MS 1000                          // Minimum startup screensaver duration
#define USER_SCREENSAVER_STARTUP_DURATION_MAX_MS 30000                         // Maximum startup screensaver duration

//------------------------------------------------------------------------------
// AUTO ACTIONS
//------------------------------------------------------------------------------
#define USER_AUTO_GRIND_TRIGGER_DELTA_G 50.0f                                   // Weight change threshold used for auto actions (grams)
#define USER_AUTO_GRIND_TRIGGER_WINDOW_MS 5000                                  // Time window for delta detection (milliseconds)
#define USER_AUTO_GRIND_TRIGGER_SETTLING_MS 1000                                // Settling period after trigger detection before confirmation (milliseconds)
#define USER_AUTO_GRIND_REARM_DELAY_MS 1500                                     // Minimum delay between auto actions (milliseconds)

// Basket-based profile detection (optional Auto Start extension)
#define USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G 5.0f                          // Default allowed basket weight variance (grams)
#define USER_BASKET_DETECTION_TOLERANCE_MIN_G 1.0f                              // Minimum basket detection tolerance (grams)
#define USER_BASKET_DETECTION_TOLERANCE_MAX_G 30.0f                             // Maximum basket detection tolerance (grams)
#define USER_BASKET_DETECTION_CONFIRM_MS 1200                                   // Confirmation duration before detected grind starts (milliseconds)
#define USER_BASKET_DETECTION_STATUS_MS 2500                                    // Error/status message duration (milliseconds)
#define USER_BASKET_DETECTION_REMOVAL_THRESHOLD_G 2.0f                          // Weight threshold used to re-arm failed basket detection (grams)
