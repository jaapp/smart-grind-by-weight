#pragma once

//==============================================================================
// HARDWARE CONFIGURATION CONSTANTS
//==============================================================================
// This file contains hardware-specific configuration values that define the
// physical characteristics and constraints of the ESP32-S3 coffee grinder system.
// These values are tied to the specific hardware implementation and should only
// be modified when changing hardware components.

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

// Load Cell ADC Pins
#define HW_LOADCELL_DOUT_PIN 3                                                 // HX711 data output pin
#define HW_LOADCELL_SCK_PIN 2                                                  // HX711 serial clock pin

// Motor Control
#define HW_MOTOR_RELAY_PIN 18                                                  // GPIO pin for grinder motor control relay

//------------------------------------------------------------------------------
// LOAD CELL ADC SPECIFICATIONS
//------------------------------------------------------------------------------
// Sample rate configuration
#define HW_LOADCELL_SAMPLE_RATE_SPS 10                                         // Current sample rate setting
#define HW_LOADCELL_SAMPLE_INTERVAL_MS (1000 / HW_LOADCELL_SAMPLE_RATE_SPS)   // Calculated sample interval

//------------------------------------------------------------------------------
// DISPLAY SPECIFICATIONS  
//------------------------------------------------------------------------------
#define HW_DISPLAY_WIDTH_PX 280                                                // LCD width in pixels
#define HW_DISPLAY_HEIGHT_PX 456                                               // LCD height in pixels
#define HW_DISPLAY_OFFSET_X_PX 0                                               // X offset for display positioning
#define HW_DISPLAY_ROTATION_DEG 0                                              // Display rotation angle
#define HW_DISPLAY_IPS_INVERT_X 180                                            // IPS X-axis inversion setting
#define HW_DISPLAY_IPS_INVERT_Y 24                                             // IPS Y-axis inversion setting
#define HW_DISPLAY_COLOR_ORDER 20                                              // Color channel ordering
#define HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT 15                               // Minimum brightness percentage (to avoid too dim to see)

//------------------------------------------------------------------------------
// SERIAL COMMUNICATION
//------------------------------------------------------------------------------
#define HW_SERIAL_BAUD_RATE 115200                                             // UART baud rate for debug/logging output
