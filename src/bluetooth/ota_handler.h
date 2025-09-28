#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_app_format.h>
#include <soc/rtc.h>
#include <esp_pm.h>

// Include detools/delta libraries
extern "C" {
#include "delta.h"
#include "detools.h"
}

// Removed config.h - not needed for HX711Core integration
#include "../config/bluetooth_config.h"

// OTA-specific enums
enum BLEOTACommand {
    BLE_OTA_CMD_START = 0x01,
    BLE_OTA_CMD_DATA = 0x02,
    BLE_OTA_CMD_END = 0x03,
    BLE_OTA_CMD_ABORT = 0x04
};

enum BLEOTAStatus {
    BLE_OTA_IDLE = 0x00,
    BLE_OTA_READY = 0x01,
    BLE_OTA_RECEIVING = 0x02,
    BLE_OTA_SUCCESS = 0x03,
    BLE_OTA_ERROR = 0x04,
    BLE_OTA_VALIDATION_ERROR = 0x05
};

// Power management states
enum BLEPowerState {
    NORMAL_POWER = 0,
    BLE_REDUCED_POWER = 1
};

/**
 * OTAHandler - Manages over-the-air firmware updates via BLE
 * 
 * Handles delta patching, power management, and firmware validation
 * for BLE-based firmware updates.
 */
class OTAHandler {
private:
    bool ota_in_progress;
    uint32_t patch_size;
    uint32_t received_size;
    BLEOTAStatus current_status;
    String current_firmware_build_number;
    bool is_full_update;
    
    // OTA tracking
    Preferences* preferences;
    
    // Power management
    BLEPowerState power_state;
    uint32_t normal_cpu_freq_mhz;
    
    // Delta OTA components
    delta_partition_writer_t patch_writer;
    
    void reduce_power_for_ble();
    void restore_normal_power();
    bool start_update();
    bool finalize_update();
    
public:
    OTAHandler();
    ~OTAHandler();
    
    /**
     * Initialize OTA handler
     * @param prefs Preferences instance for storing OTA tracking data
     */
    void init(Preferences* prefs);
    
    /**
     * Start OTA update process
     * @param size Size of the patch data to receive
     * @param expected_build_number Build number we expect after successful update
     * @param is_full_update True for full update, false for delta update
     * @return true if successfully started
     */
    bool start_ota(uint32_t size, const String& expected_build_number = "", bool is_full_update = false);
    
    /**
     * Process received OTA data chunk
     * @param data Pointer to chunk data
     * @param size Size of chunk
     * @return true if chunk processed successfully
     */
    bool process_data_chunk(const uint8_t* data, size_t size);
    
    /**
     * Finalize OTA update and restart device
     * @return true if finalization successful
     */
    bool complete_ota();
    
    /**
     * Abort OTA update
     */
    void abort_ota();
    
    /**
     * Get current OTA status
     */
    BLEOTAStatus get_status() const { return current_status; }
    
    /**
     * Get OTA progress percentage
     */
    float get_progress() const;
    
    /**
     * Check if OTA is in progress
     */
    bool is_ota_active() const { return ota_in_progress; }
    
    /**
     * Get current firmware build number
     */
    const String& get_build_number() const { return current_firmware_build_number; }
    
    /**
     * Enable reduced power mode for BLE operations
     */
    void enable_ble_power_mode();
    
    /**
     * Restore normal power mode
     */
    void restore_normal_power_mode();
    
    /**
     * Check if OTA failed after reboot and return expected build number if so
     * @return Expected build number if OTA failed, empty string if no failure or no expectation
     */
    String check_ota_failure_after_boot();
};