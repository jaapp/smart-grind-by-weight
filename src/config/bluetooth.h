#pragma once

//==============================================================================
// BLUETOOTH LOW ENERGY CONFIGURATION
//==============================================================================
// This file contains all Bluetooth Low Energy (BLE) related configuration
// constants for the ESP32 coffee grinder. This includes service definitions,
// timing parameters, and communication settings for OTA updates and data export.

//------------------------------------------------------------------------------
// BLE OTA (Over-The-Air Update) SERVICE
//------------------------------------------------------------------------------
#define BLE_OTA_SERVICE_UUID "12345678-1234-1234-1234-123456789abc"            // Main OTA service UUID
#define BLE_OTA_DATA_CHAR_UUID "87654321-4321-4321-4321-cba987654321"          // Characteristic for firmware data transfer
#define BLE_OTA_CONTROL_CHAR_UUID "11111111-2222-3333-4444-555555555555"      // Characteristic for OTA commands (start/end/abort)
#define BLE_OTA_STATUS_CHAR_UUID "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"       // Characteristic for OTA status notifications
#define BLE_OTA_BUILD_NUMBER_CHAR_UUID "66666666-7777-8888-9999-000000000000" // Characteristic for current firmware build number

// OTA Device Settings
#define BLE_OTA_DEVICE_NAME "GrindByWeight"                                    // Bluetooth device name when in OTA mode

//------------------------------------------------------------------------------
// BLE DATA EXPORT SERVICE
//------------------------------------------------------------------------------
#define BLE_DATA_SERVICE_UUID "22334455-6677-8899-aabb-ccddeeffffaa"          // Measurement data service UUID
#define BLE_DATA_CONTROL_CHAR_UUID "33445566-7788-99aa-bbcc-ddeeffaabbcc"     // Control characteristic (start/stop data export)
#define BLE_DATA_TRANSFER_CHAR_UUID "44556677-8899-aabb-ccdd-eeffaabbccdd"    // Data transfer characteristic
#define BLE_DATA_STATUS_CHAR_UUID "55667788-99aa-bbcc-ddee-ffaabbccddee"      // Status notifications characteristic
#define BLE_DATA_CHUNK_SIZE_BYTES 512                                          // Per-chunk payload size for data export

//------------------------------------------------------------------------------
// BLE DEBUG SERVICE (Nordic UART Service)
//------------------------------------------------------------------------------
#define BLE_DEBUG_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"         // Nordic UART Service UUID
#define BLE_DEBUG_RX_CHAR_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"         // ESP32 RX (Client -> Device)
#define BLE_DEBUG_TX_CHAR_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"         // ESP32 TX (Device -> Client)

//------------------------------------------------------------------------------
// BLE SYSTEM INFO SERVICE
//------------------------------------------------------------------------------
#define BLE_SYSINFO_SERVICE_UUID "77889900-aabb-ccdd-eeff-112233445566"        // System information service UUID
#define BLE_SYSINFO_SYSTEM_CHAR_UUID "88990011-bbcc-ddee-ff11-223344556677"    // System info (version, uptime, memory)
#define BLE_SYSINFO_PERFORMANCE_CHAR_UUID "99001122-ccdd-eeff-1122-334455667788" // Performance metrics
#define BLE_SYSINFO_HARDWARE_CHAR_UUID "00112233-ddee-ff11-2233-445566778899"  // Hardware status
#define BLE_SYSINFO_SESSIONS_CHAR_UUID "11223344-eeff-1122-3344-556677889900"  // Session statistics

#define BLE_SYSINFO_MAX_PAYLOAD_BYTES 512                                       // Maximum payload size for system info

//------------------------------------------------------------------------------
// BLE TIMEOUT SETTINGS
//------------------------------------------------------------------------------
#define BLE_AUTO_DISABLE_TIMEOUT_MS (30 * 60 * 1000)                          // Auto-disable BLE after inactivity
#define BLE_BOOTUP_AUTO_DISABLE_TIMEOUT_MS (5 * 60 * 1000)                    // Auto-disable BLE after bootup period


//------------------------------------------------------------------------------
// BLE INITIALIZATION TIMING
//------------------------------------------------------------------------------
// Delays for stable BLE stack initialization
#define BLE_INIT_STACK_DELAY_MS 100                                            // Delay after BLE stack initialization
#define BLE_INIT_SERVER_DELAY_MS 50                                            // Delay after server creation
#define BLE_INIT_SERVICE_DELAY_MS 25                                           // Delay after service creation
#define BLE_INIT_CHARACTERISTIC_DELAY_MS 25                                    // Delay after each characteristic setup
#define BLE_INIT_START_DELAY_MS 50                                             // Delay after service start
#define BLE_INIT_ADVERTISING_DELAY_MS 25                                       // Delay after advertising setup

//------------------------------------------------------------------------------
// BLE SHUTDOWN TIMING
//------------------------------------------------------------------------------
#define BLE_SHUTDOWN_ADVERTISING_DELAY_MS 50                                   // Delay to allow advertising to stop cleanly
#define BLE_SHUTDOWN_DEINIT_DELAY_MS 100                                       // Delay to allow BLE stack to deinitialize

//------------------------------------------------------------------------------
// BLE POWER MANAGEMENT
//------------------------------------------------------------------------------
#define BLE_NORMAL_CPU_FREQ_MHZ 240                                            // Normal CPU frequency during BLE operations
#define BLE_REDUCED_CPU_FREQ_MHZ 240                                           // Reduced CPU frequency for BLE mode