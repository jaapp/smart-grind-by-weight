#pragma once

// Forward declaration to avoid circular dependency
class BluetoothManager;
extern BluetoothManager g_bluetooth_manager;

// Temporary fallback logging - use Serial instead of BLE to avoid circular dependencies
#include <Arduino.h>

#define BLE_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)

// Replace DEBUG macros to use Serial logging
#if USER_DEBUG_SERIAL_OUTPUT
#define DEBUG_PRINTF(format, ...) Serial.printf(format, ##__VA_ARGS__)
#define DEBUG_PRINTLN(str) Serial.println(str)
#define DEBUG_PRINT(str) Serial.print(str)
#else
#define DEBUG_PRINTF(format, ...)
#define DEBUG_PRINTLN(str)
#define DEBUG_PRINT(str)
#endif

// Conditional debug macros for different subsystems
#if USER_DEBUG_GRIND_CONTROLLER
#define GRIND_DEBUG_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define GRIND_DEBUG_LOG(format, ...)
#endif

#if USER_DEBUG_LOAD_CELL
#define LOADCELL_DEBUG_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define LOADCELL_DEBUG_LOG(format, ...)
#endif

#if USER_DEBUG_UI_SYSTEM
#define UI_DEBUG_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define UI_DEBUG_LOG(format, ...)
#endif

#if USER_DEBUG_CALIBRATION
#define CALIBRATION_DEBUG_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define CALIBRATION_DEBUG_LOG(format, ...)
#endif

#if USER_DEBUG_WEIGHT_SETTLING
#define SETTLING_DEBUG_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define SETTLING_DEBUG_LOG(format, ...)
#endif

#ifdef ENABLE_BLE_DEBUG_VERBOSE
#define BLE_DEBUG_LOG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define BLE_DEBUG_LOG(format, ...)
#endif

#define OTA_DEBUG_LOG(format, ...) Serial.printf("[OTA_DEBUG] " format, ##__VA_ARGS__)