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
// SCREEN AUTO-DIMMING / SCREENSAVER
//------------------------------------------------------------------------------
// Two-stage screensaver: after the dim timeout the display dims (plain dim or the
// boot logo on black, per mode); after the off timeout the backlight turns fully off.
#define USER_SCREEN_DIM_TIMEOUT_MS 60000                                       // Default inactivity before stage 1 (dim / logo)
#define USER_SCREEN_OFF_TIMEOUT_MS 300000                                      // Default inactivity before stage 2 (display off)
#define USER_SCREEN_BRIGHTNESS_NORMAL 1.0f                                     // Normal screen brightness
#define USER_SCREEN_BRIGHTNESS_DIMMED 0.35f                                    // Dimmed screen brightness
#define USER_WEIGHT_ACTIVITY_THRESHOLD_G 1.0f                                  // Weight change threshold for screen timeout reset (grams)

// Stage-1 screensaver style: plain dim, or the boot logo centered on black
#define USER_SCREEN_SAVER_MODE_DIM 0                                           // Dim the current screen to the configured brightness
#define USER_SCREEN_SAVER_MODE_LOGO 1                                          // Show the boot logo on black (at dimmed brightness)
#define USER_SCREEN_SAVER_MODE_DEFAULT USER_SCREEN_SAVER_MODE_DIM             // Default screensaver style
#define USER_SCREEN_SAVER_TIMEOUT_NEVER_MS 0                                   // Sentinel timeout meaning "never"

// Grinding progress style (Menu -> Display): centered arc, or an Apple Watch style
// ring tracing the outer edge of the screen. Preference key "prog_style" (grinder ns).
#define USER_PROGRESS_STYLE_STANDARD 0                                         // Centered progress arc (default)
#define USER_PROGRESS_STYLE_EDGE 1                                             // Border-tracing edge ring
#define USER_PROGRESS_STYLE_DEFAULT USER_PROGRESS_STYLE_STANDARD

//------------------------------------------------------------------------------
// BOOT SPLASH
//------------------------------------------------------------------------------
// Placement of the boot logo relative to screen center (pixels). Positive X moves
// right, positive Y moves down. Default 0,0 = centered.
#define USER_BOOT_LOGO_OFFSET_X 0
#define USER_BOOT_LOGO_OFFSET_Y 0

//------------------------------------------------------------------------------
// AUTO ACTIONS
//------------------------------------------------------------------------------
#define USER_AUTO_GRIND_TRIGGER_DELTA_G 50.0f                                   // Weight change threshold used for auto actions (grams)
#define USER_AUTO_GRIND_TRIGGER_WINDOW_MS 5000                                  // Time window for delta detection (milliseconds)
#define USER_AUTO_GRIND_TRIGGER_SETTLING_MS 1000                                // Settling period after trigger detection before confirmation (milliseconds)
#define USER_AUTO_GRIND_REARM_DELAY_MS 1500                                     // Minimum delay between auto actions (milliseconds)
