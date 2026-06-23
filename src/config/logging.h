#pragma once

// Forward declaration to avoid circular dependency
class BluetoothManager;
extern BluetoothManager g_bluetooth_manager;

// Logging routes to the USB Serial/JTAG console via printf (IDF handles init).
#define LOG_BLE(format, ...) printf(format, ##__VA_ARGS__)

// --- General debug output ---
#if DEBUG_SERIAL_OUTPUT
#define LOG_DEBUG_PRINTF(format, ...)  printf(format, ##__VA_ARGS__)
#define LOG_DEBUG_PRINTLN(str)         printf("%s\n", str)
#define LOG_DEBUG_PRINT(str)           printf("%s", str)
#else
#define LOG_DEBUG_PRINTF(format, ...)
#define LOG_DEBUG_PRINTLN(str)
#define LOG_DEBUG_PRINT(str)
#endif

// --- Grind controller ---
#if DEBUG_GRIND_CONTROLLER
#define LOG_GRIND_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_GRIND_DEBUG(format, ...)
#endif

// --- Load cell ---
#if DEBUG_LOAD_CELL
#define LOG_LOADCELL_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_LOADCELL_DEBUG(format, ...)
#endif

// --- UI system ---
#if DEBUG_UI_SYSTEM
#define LOG_UI_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_UI_DEBUG(format, ...)
#endif

// --- Calibration ---
#if DEBUG_CALIBRATION
#define LOG_CALIBRATION_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_CALIBRATION_DEBUG(format, ...)
#endif

// --- Weight settling ---
#if DEBUG_WEIGHT_SETTLING
#define LOG_SETTLING_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_SETTLING_DEBUG(format, ...)
#endif

// --- BLE verbose ---
#ifdef ENABLE_BLE_DEBUG_VERBOSE
#define LOG_BLE_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_BLE_DEBUG(format, ...)
#endif

// --- OTA ---
#define LOG_OTA_DEBUG(format, ...) printf("[OTA_DEBUG] " format, ##__VA_ARGS__)
