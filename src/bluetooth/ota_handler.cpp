#include "ota_handler.h"
#include "../config/build_info.h"
#include "../config/logging.h"
#include "../hardware/touch_driver.h"
#include "../hardware/hardware_manager.h"
#include "../tasks/task_manager.h"
#include <Arduino.h>
#include <BLEDevice.h>

OTAHandler::OTAHandler() 
    : ota_in_progress(false)
    , patch_size(0)
    , received_size(0)
    , current_status(BLE_OTA_IDLE)
    , current_firmware_build_number("")
    , is_full_update(false)
    , power_state(NORMAL_POWER)
    , normal_cpu_freq_mhz(BLE_NORMAL_CPU_FREQ_MHZ) {
}

OTAHandler::~OTAHandler() {
    if (ota_in_progress) {
        abort_ota();
    }
    restore_normal_power_mode();
}

void OTAHandler::init(Preferences* prefs) {
    preferences = prefs;
    LOG_BLE("OTA: Handler initialized\n");
    
    // Get current firmware build number
    current_firmware_build_number = String(BUILD_NUMBER);
    
    // Log initial power state
    LOG_BLE("OTA Power: Initial state - CPU: %luMHz, Power mode: %s\n",
                 (unsigned long)getCpuFrequencyMhz(),
                 (power_state == NORMAL_POWER) ? "NORMAL" : "REDUCED");
}

void OTAHandler::enable_ble_power_mode() {
    reduce_power_for_ble();
}

void OTAHandler::restore_normal_power_mode() {
    restore_normal_power();
}

void OTAHandler::reduce_power_for_ble() {
    if (power_state == BLE_REDUCED_POWER) return;
    
    // Store current CPU frequency
    normal_cpu_freq_mhz = getCpuFrequencyMhz();
    
    LOG_BLE("OTA Power: Switching from %luMHz to %dMHz\n", 
                 (unsigned long)normal_cpu_freq_mhz, BLE_REDUCED_CPU_FREQ_MHZ);
    
    // Lower CPU frequency for power savings
    if (!setCpuFrequencyMhz(BLE_REDUCED_CPU_FREQ_MHZ)) {
        LOG_BLE("OTA Power: WARNING - Failed to reduce CPU frequency\n");
    }
    
    // Verify the frequency change
    uint32_t actual_freq = getCpuFrequencyMhz();
    if (actual_freq != BLE_REDUCED_CPU_FREQ_MHZ) {
        LOG_BLE("OTA Power: WARNING - CPU frequency is %luMHz, expected %dMHz\n", 
                     (unsigned long)actual_freq, BLE_REDUCED_CPU_FREQ_MHZ);
    }
    
    power_state = BLE_REDUCED_POWER;
    LOG_BLE("OTA Power: Reduced power mode enabled\n");
}

void OTAHandler::restore_normal_power() {
    if (power_state == NORMAL_POWER) return;
    
    LOG_BLE("OTA Power: Restoring CPU to %luMHz\n", 
                 (unsigned long)normal_cpu_freq_mhz);
    
    // Restore original CPU frequency
    if (!setCpuFrequencyMhz(normal_cpu_freq_mhz)) {
        LOG_BLE("OTA Power: WARNING - Failed to restore CPU frequency\n");
        // Try to set to default frequency as fallback
        if (!setCpuFrequencyMhz(BLE_NORMAL_CPU_FREQ_MHZ)) {
            LOG_BLE("OTA Power: ERROR - Failed to set fallback CPU frequency\n");
        }
    }
    
    // Verify the frequency change
    uint32_t actual_freq = getCpuFrequencyMhz();
    if (actual_freq != normal_cpu_freq_mhz) {
        LOG_BLE("OTA Power: WARNING - CPU frequency is %luMHz, expected %luMHz\n", 
                     (unsigned long)actual_freq, (unsigned long)normal_cpu_freq_mhz);
    }
    
    power_state = NORMAL_POWER;
    LOG_BLE("OTA Power: Normal power mode restored\n");
}

bool OTAHandler::start_ota(uint32_t size, const String& expected_build_number, bool is_full_update) {
    LOG_OTA_DEBUG("start_ota() called - size=%lu, build=%s, full=%d\n", 
                  (unsigned long)size, expected_build_number.c_str(), is_full_update);
    
    if (ota_in_progress) {
        LOG_BLE("OTA: Update already in progress\n");
        LOG_OTA_DEBUG("start_ota() FAILED - already in progress\n");
        return false;
    }
    
    patch_size = size;
    received_size = 0;
    this->is_full_update = is_full_update;
    
    LOG_BLE("OTA: Starting %s update (%lu KB)\n", is_full_update ? "full" : "delta", (unsigned long)patch_size / 1024);
    LOG_OTA_DEBUG("patch_size=%lu, received_size=%lu, is_full_update=%d\n", 
                  (unsigned long)patch_size, (unsigned long)received_size, this->is_full_update);
    
    // Store expected build number for post-reboot verification
    if (!expected_build_number.isEmpty() && preferences) {
        preferences->putString("new_build_nr", expected_build_number);
        LOG_OTA_DEBUG("Stored expected build number: %s\n", expected_build_number.c_str());
    } else {
        LOG_OTA_DEBUG("No expected build number to store\n");
    }
    
    // Reconfigure task watchdog for OTA process with extended timeout
    // This is a CPU and flash-intensive operation that can starve other tasks
    LOG_BLE("OTA: Reconfiguring task watchdog timer for OTA process (300s timeout)...\n");
    LOG_OTA_DEBUG("Configuring watchdog - timeout_ms=300000, cores=0x3\n");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 300000,
        .idle_core_mask = (1 << 0) | (1 << 1), // Watch idle tasks on both cores
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);
    LOG_OTA_DEBUG("Watchdog reconfigured successfully\n");

    LOG_OTA_DEBUG("Calling start_update()...\n");
    if (!start_update()) {
        current_status = BLE_OTA_ERROR;
        LOG_OTA_DEBUG("start_update() FAILED\n");
        return false;
    }
    LOG_OTA_DEBUG("start_update() SUCCESS\n");
    
    ota_in_progress = true;
    current_status = BLE_OTA_RECEIVING;
    LOG_OTA_DEBUG("OTA started successfully - status=BLE_OTA_RECEIVING\n");
    return true;
}

bool OTAHandler::process_data_chunk(const uint8_t* data, size_t size) {
    if (!ota_in_progress) {
        return false;
    }
    
    // Write patch data to patch partition
    if (delta_partition_write(&patch_writer, (const char*)data, size) != ESP_OK) {
        LOG_BLE("OTA: Patch write failed at offset %lu\n", (unsigned long)received_size);
        current_status = BLE_OTA_ERROR;
        return false;
    }
    
    received_size += size;
    
    // Progress logging every 16KB for better visibility, plus at start and end
    if (received_size % 16384 == 0 || received_size == size || received_size == patch_size) {
        LOG_BLE("OTA: Transfer %lu KB / %lu KB (%.1f%%)\n", 
                     (unsigned long)received_size / 1024, (unsigned long)patch_size / 1024, 
                     get_progress());
    }
    
    return true;
}

bool OTAHandler::complete_ota() {
    LOG_OTA_DEBUG("complete_ota() called\n");
    
    if (!ota_in_progress) {
        LOG_BLE("OTA: No update in progress\n");
        LOG_OTA_DEBUG("complete_ota() FAILED - no update in progress\n");
        return false;
    }
    
    LOG_BLE("OTA: Finalizing update...\n");
    LOG_OTA_DEBUG("patch_size=%lu, received_size=%lu\n", 
                  (unsigned long)patch_size, (unsigned long)received_size);
    
    // Kamikaze mode: Disable all non-essential systems before flash operations
    LOG_BLE("OTA: Entering kamikaze mode - disabling non-essential systems...\n");
    LOG_OTA_DEBUG("Starting kamikaze mode shutdown sequence...\n");
    
    // Disable I2C operations (TouchDriver) - access through hardware_manager
    extern HardwareManager hardware_manager;
    LOG_OTA_DEBUG("Disabling TouchDriver I2C operations...\n");
    hardware_manager.get_display()->get_touch_driver()->disable();
    LOG_OTA_DEBUG("TouchDriver disabled\n");
    
    // Skip BLE deinitialization - causes hang in kamikaze mode
    // BLE stack will be destroyed during system restart anyway
    // BLEDevice::deinit(true);
    LOG_OTA_DEBUG("Skipping BLE deinit (causes hang) - kamikaze restart will handle cleanup\n");
    
    // Stop Core 0 tasks (HX711 sampling, grind controller - not needed during OTA)
    LOG_OTA_DEBUG("Suspending hardware tasks for OTA...\n");
    task_manager.suspend_hardware_tasks();
    LOG_OTA_DEBUG("Hardware tasks suspended for OTA\n");
    
    LOG_OTA_DEBUG("Calling finalize_update()...\n");
    bool success = finalize_update();
    if (success) {
        current_status = BLE_OTA_SUCCESS;
        LOG_OTA_DEBUG("finalize_update() SUCCESS\n");
        LOG_BLE("OTA: Update complete (%lu KB)\n", (unsigned long)received_size / 1024);
        LOG_BLE("OTA: Starting restart sequence...\n");
        
        // Restart device
        LOG_OTA_DEBUG("Flushing Serial before restart...\n");
        Serial.flush();
        delay(100);
        
        // Kamikaze restart - no graceful cleanup needed
        LOG_BLE("OTA: Kamikaze restart in 3...2...1\n");
        LOG_OTA_DEBUG("Final countdown before esp_restart()...\n");
        Serial.flush();
        delay(100);
        
        LOG_OTA_DEBUG("Calling esp_restart()...\n");
        Serial.flush();
        esp_restart();
        
        // Fallback restart methods
        LOG_OTA_DEBUG("esp_restart() failed, trying ESP.restart()...\n");
        Serial.flush();
        ESP.restart();
        
        LOG_OTA_DEBUG("ESP.restart() failed, entering infinite loop...\n");
        Serial.flush();
        while(true) delay(1000);
    } else {
        current_status = BLE_OTA_ERROR;
        LOG_BLE("OTA: Finalization failed\n");
        LOG_OTA_DEBUG("finalize_update() FAILED\n");
    }
    
    ota_in_progress = false;
    LOG_OTA_DEBUG("complete_ota() returning %s\n", success ? "SUCCESS" : "FAILED");
    return success;
}

void OTAHandler::abort_ota() {
    if (ota_in_progress) {
        LOG_BLE("OTA: Aborting update\n");
        ota_in_progress = false;
        received_size = 0;
        patch_size = 0;
        current_status = BLE_OTA_ERROR;
    }
}

float OTAHandler::get_progress() const {
    if (patch_size == 0) return 0.0f;
    return 100.0f * received_size / patch_size;
}

bool OTAHandler::start_update() {
    // Initialize patch partition for writing
    if (delta_partition_init(&patch_writer, "patch", patch_size) != ESP_OK) {
        LOG_BLE("OTA: Failed to initialize patch partition\n");
        return false;
    }
    return true;
}

bool OTAHandler::finalize_update() {
    LOG_OTA_DEBUG("finalize_update() called\n");
    
    // Verify received size matches expected
    LOG_OTA_DEBUG("Verifying received size: expected=%lu, got=%lu\n", 
                  (unsigned long)patch_size, (unsigned long)received_size);
    if (received_size != patch_size) {
        LOG_BLE("OTA: Size mismatch - expected %lu, got %lu\n", 
                     (unsigned long)patch_size, (unsigned long)received_size);
        LOG_OTA_DEBUG("Size verification FAILED\n");
        return false;
    }
    LOG_OTA_DEBUG("Size verification SUCCESS\n");
    
    // A/B Partition Update Logic
    LOG_OTA_DEBUG("Getting running partition...\n");
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (!running_partition) {
        LOG_BLE("❌ Could not get running partition!\n");
        LOG_OTA_DEBUG("esp_ota_get_running_partition() FAILED\n");
        return false;
    }
    LOG_OTA_DEBUG("Running partition: %s (addr=0x%x, size=%lu)\n", 
                  running_partition->label, running_partition->address, 
                  (unsigned long)running_partition->size);

    LOG_OTA_DEBUG("Getting next update partition...\n");
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        LOG_BLE("❌ Could not find a valid OTA update partition!\n");
        LOG_OTA_DEBUG("esp_ota_get_next_update_partition() FAILED\n");
        return false;
    }
    LOG_OTA_DEBUG("Update partition: %s (addr=0x%x, size=%lu)\n", 
                  update_partition->label, update_partition->address, 
                  (unsigned long)update_partition->size);
    
    LOG_BLE("OTA Info: Running from '%s', updating to '%s'\n", 
                  running_partition->label, update_partition->label);

    // Set up delta options for the A/B update
    LOG_OTA_DEBUG("Setting up delta options...\n");
    delta_opts_t opts;
    opts.src = running_partition->label;
    opts.dest = update_partition->label;
    opts.patch = "patch";
    opts.is_full_update = this->is_full_update ? 1 : 0;
    LOG_OTA_DEBUG("Delta opts: src=%s, dest=%s, patch=%s, is_full=%d\n", 
                  opts.src, opts.dest, opts.patch, opts.is_full_update);
    
    // Apply the delta patch
    LOG_OTA_DEBUG("Calling delta_check_and_apply() with size=%lu...\n", (unsigned long)patch_size);
    Serial.flush();
    int result = delta_check_and_apply(patch_size, &opts);
    LOG_OTA_DEBUG("delta_check_and_apply() returned: %d\n", result);
    if (result < 0) {
        LOG_BLE("Delta patch failed: %s\n", delta_error_as_string(result));
        LOG_OTA_DEBUG("Delta patch FAILED with error: %s\n", delta_error_as_string(result));
        return false;
    }
    
    LOG_OTA_DEBUG("finalize_update() SUCCESS - delta patch applied\n");
    return true;
}

String OTAHandler::check_ota_failure_after_boot() {
    if (!preferences) {
        return "";
    }
    
    String expected_build = preferences->getString("new_build_nr", "");
    if (expected_build.isEmpty()) {
        return "";
    }
    
    int current_build = BUILD_NUMBER;
    int expected_build_num = expected_build.toInt();
    
    if (current_build < expected_build_num) {
        LOG_BLE("OTA: Patch failed - expected build #%d, got #%d\n", 
                     expected_build_num, current_build);
        preferences->remove("new_build_nr");
        return expected_build;
    } else if (current_build >= expected_build_num) {
        preferences->remove("new_build_nr");
    }
    
    return "";
}
