#pragma once

//==============================================================================
// DEBUG AND DEVELOPMENT CONFIGURATION
//==============================================================================
// This file contains all debug-related configuration constants including
// mock hardware settings, debug output controls, and performance monitoring.

//------------------------------------------------------------------------------
// MOST IMPORTANT DEBUG PARAMETERS
//------------------------------------------------------------------------------
// Mock hardware (most critical for testing vs production)
#define HW_ENABLE_LOADCELL_MOCK 1                                              // 0=use physical HX711, 1=use simulated mock driver

// UI visual feedback
#define USER_ENABLE_GRINDER_BACKGROUND_INDICATOR 1                             // 0=disabled, 1=show background color during grinding/pulses

// Core debug logging settings
#define USER_DEBUG_SERIAL_OUTPUT 1                                             // Enable serial debug output
#define USER_DEBUG_GRIND_CONTROLLER 0                                          // Enable detailed grind debugging
#define USER_DEBUG_LOAD_CELL 0                                                 // Enable detailed load cell debugging  
#define USER_DEBUG_UI_SYSTEM 0                                                 // Enable detailed UI debugging
#define USER_DEBUG_CALIBRATION 0                                               // Enable detailed calibration debugging
#define USER_DEBUG_WEIGHT_SETTLING 0                                           // Enable weight settling debugging

//------------------------------------------------------------------------------
// MOCK HARDWARE DETAILED CONFIGURATION
//------------------------------------------------------------------------------
#define HW_MOCK_FLOW_RATE_GPS 1.9f                                             // Simulated continuous flow rate in grams per second
#define HW_MOCK_CAL_FACTOR -7050.0f                                            // Fixed calibration factor used during mocking
#define HW_MOCK_BASELINE_RAW 0x700000                                          // Baseline raw count around mid-scale for tare offset
#define HW_MOCK_IDLE_NOISE_RAW 60.0f                                           // Peak raw noise when idle (counts)
#define HW_MOCK_GRIND_NOISE_RAW 400.0f                                         // Peak raw noise when grinding (counts)
#define HW_MOCK_FLOW_RAMP_MS 350                                               // Ramp duration for simulated flow transitions
#define HW_MOCK_START_DELAY_MS 500                                             // Delay from motor start command to weight increase
#define HW_MOCK_STOP_DELAY_MS 400                                              // Delay from motor stop command to weight stop


