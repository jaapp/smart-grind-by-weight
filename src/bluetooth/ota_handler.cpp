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
    BLE_LOG("OTA: Handler initialized\n");
    
    // Get current firmware build number
    current_firmware_build_number = String(BUILD_NUMBER);
    
    // Log initial power state
    BLE_LOG("OTA Power: Initial state - CPU: %luMHz, Power mode: %s\n",
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
    
    BLE_LOG("OTA Power: Switching from %luMHz to %dMHz\n", 
                 (unsigned long)normal_cpu_freq_mhz, BLE_REDUCED_CPU_FREQ_MHZ);
    
    // Lower CPU frequency for power savings
    if (!setCpuFrequencyMhz(BLE_REDUCED_CPU_FREQ_MHZ)) {
        BLE_LOG("OTA Power: WARNING - Failed to reduce CPU frequency\n");
    }
    
    // Verify the frequency change
    uint32_t actual_freq = getCpuFrequencyMhz();
    if (actual_freq != BLE_REDUCED_CPU_FREQ_MHZ) {
        BLE_LOG("OTA Power: WARNING - CPU frequency is %luMHz, expected %dMHz\n", 
                     (unsigned long)actual_freq, BLE_REDUCED_CPU_FREQ_MHZ);
    }
    
    power_state = BLE_REDUCED_POWER;
    BLE_LOG("OTA Power: Reduced power mode enabled\n");
}

void OTAHandler::restore_normal_power() {
    if (power_state == NORMAL_POWER) return;
    
    BLE_LOG("OTA Power: Restoring CPU to %luMHz\n", 
                 (unsigned long)normal_cpu_freq_mhz);
    
    // Restore original CPU frequency
    if (!setCpuFrequencyMhz(normal_cpu_freq_mhz)) {
        BLE_LOG("OTA Power: WARNING - Failed to restore CPU frequency\n");
        // Try to set to default frequency as fallback
        if (!setCpuFrequencyMhz(BLE_NORMAL_CPU_FREQ_MHZ)) {
            BLE_LOG("OTA Power: ERROR - Failed to set fallback CPU frequency\n");
        }
    }
    
    // Verify the frequency change
    uint32_t actual_freq = getCpuFrequencyMhz();
    if (actual_freq != normal_cpu_freq_mhz) {
        BLE_LOG("OTA Power: WARNING - CPU frequency is %luMHz, expected %luMHz\n", 
                     (unsigned long)actual_freq, (unsigned long)normal_cpu_freq_mhz);
    }
    
    power_state = NORMAL_POWER;
    BLE_LOG("OTA Power: Normal power mode restored\n");
}

bool OTAHandler::start_ota(uint32_t size, const String& expected_build_number, bool is_full_update) {
    OTA_DEBUG_LOG("start_ota() called - size=%lu, build=%s, full=%d\n", 
                  (unsigned long)size, expected_build_number.c_str(), is_full_update);
    
    if (ota_in_progress) {
        BLE_LOG("OTA: Update already in progress\n");
        OTA_DEBUG_LOG("start_ota() FAILED - already in progress\n");
        return false;
    }
    
    patch_size = size;
    received_size = 0;
    this->is_full_update = is_full_update;
    
    BLE_LOG("OTA: Starting %s update (%lu KB)\n", is_full_update ? "full" : "delta", (unsigned long)patch_size / 1024);
    OTA_DEBUG_LOG("patch_size=%lu, received_size=%lu, is_full_update=%d\n", 
                  (unsigned long)patch_size, (unsigned long)received_size, this->is_full_update);
    
    // Store expected build number for post-reboot verification
    if (!expected_build_number.isEmpty() && preferences) {
        preferences->putString("new_build_nr", expected_build_number);
        OTA_DEBUG_LOG("Stored expected build number: %s\n", expected_build_number.c_str());
    } else {
        OTA_DEBUG_LOG("No expected build number to store\n");
    }
    
    // Reconfigure task watchdog for OTA process with extended timeout
    // This is a CPU and flash-intensive operation that can starve other tasks
    BLE_LOG("OTA: Reconfiguring task watchdog timer for OTA process (300s timeout)...\n");
    OTA_DEBUG_LOG("Configuring watchdog - timeout_ms=300000, cores=0x3\n");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 300000,
        .idle_core_mask = (1 << 0) | (1 << 1), // Watch idle tasks on both cores
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);
    OTA_DEBUG_LOG("Watchdog reconfigured successfully\n");

    OTA_DEBUG_LOG("Calling start_update()...\n");
    if (!start_update()) {
        current_status = BLE_OTA_ERROR;
        OTA_DEBUG_LOG("start_update() FAILED\n");
        return false;
    }
    OTA_DEBUG_LOG("start_update() SUCCESS\n");
    
    ota_in_progress = true;
    current_status = BLE_OTA_RECEIVING;
    OTA_DEBUG_LOG("OTA started successfully - status=BLE_OTA_RECEIVING\n");
    return true;
}

bool OTAHandler::process_data_chunk(const uint8_t* data, size_t size) {
    if (!ota_in_progress) {
        return false;
    }
    
    // Write patch data to patch partition
    if (delta_partition_write(&patch_writer, (const char*)data, size) != ESP_OK) {
        BLE_LOG("OTA: Patch write failed at offset %lu\n", (unsigned long)received_size);
        current_status = BLE_OTA_ERROR;
        return false;
    }
    
    received_size += size;
    
    // Progress logging every 16KB for better visibility, plus at start and end
    if (received_size % 16384 == 0 || received_size == size || received_size == patch_size) {
        BLE_LOG("OTA: Transfer %lu KB / %lu KB (%.1f%%)\n", 
                     (unsigned long)received_size / 1024, (unsigned long)patch_size / 1024, 
                     get_progress());
    }
    
    return true;
}

bool OTAHandler::complete_ota() {
    OTA_DEBUG_LOG("complete_ota() called\n");
    
    if (!ota_in_progress) {
        BLE_LOG("OTA: No update in progress\n");
        OTA_DEBUG_LOG("complete_ota() FAILED - no update in progress\n");
        return false;
    }
    
    BLE_LOG("OTA: Finalizing update...\n");
    OTA_DEBUG_LOG("patch_size=%lu, received_size=%lu\n", 
                  (unsigned long)patch_size, (unsigned long)received_size);
    
    // Kamikaze mode: Disable all non-essential systems before flash operations
    BLE_LOG("OTA: Entering kamikaze mode - disabling non-essential systems...\n");
    OTA_DEBUG_LOG("Starting kamikaze mode shutdown sequence...\n");
    
    // Disable I2C operations (TouchDriver) - access through hardware_manager
    extern HardwareManager hardware_manager;
    OTA_DEBUG_LOG("Disabling TouchDriver I2C operations...\n");
    hardware_manager.get_display()->get_touch_driver()->disable();
    OTA_DEBUG_LOG("TouchDriver disabled\n");
    
    // Skip BLE deinitialization - causes hang in kamikaze mode
    // BLE stack will be destroyed during system restart anyway
    // BLEDevice::deinit(true);
    OTA_DEBUG_LOG("Skipping BLE deinit (causes hang) - kamikaze restart will handle cleanup\n");
    
    // Stop Core 0 tasks (HX711 sampling, grind controller - not needed during OTA)
    OTA_DEBUG_LOG("Suspending hardware tasks for OTA...\n");
    task_manager.suspend_hardware_tasks();
    OTA_DEBUG_LOG("Hardware tasks suspended for OTA\n");
    
    OTA_DEBUG_LOG("Calling finalize_update()...\n");
    bool success = finalize_update();
    if (success) {
        current_status = BLE_OTA_SUCCESS;
        OTA_DEBUG_LOG("finalize_update() SUCCESS\n");
        BLE_LOG("OTA: Update complete (%lu KB)\n", (unsigned long)received_size / 1024);
        BLE_LOG("OTA: Starting restart sequence...\n");
        
        // Restart device
        OTA_DEBUG_LOG("Flushing Serial before restart...\n");
        Serial.flush();
        delay(100);
        
        // Kamikaze restart - no graceful cleanup needed
        BLE_LOG("OTA: Kamikaze restart in 3...2...1\n");
        OTA_DEBUG_LOG("Final countdown before esp_restart()...\n");
        Serial.flush();
        delay(100);
        
        OTA_DEBUG_LOG("Calling esp_restart()...\n");
        Serial.flush();
        esp_restart();
        
        // Fallback restart methods
        OTA_DEBUG_LOG("esp_restart() failed, trying ESP.restart()...\n");
        Serial.flush();
        ESP.restart();
        
        OTA_DEBUG_LOG("ESP.restart() failed, entering infinite loop...\n");
        Serial.flush();
        while(true) delay(1000);
    } else {
        current_status = BLE_OTA_ERROR;
        BLE_LOG("OTA: Finalization failed\n");
        OTA_DEBUG_LOG("finalize_update() FAILED\n");
    }
    
    ota_in_progress = false;
    OTA_DEBUG_LOG("complete_ota() returning %s\n", success ? "SUCCESS" : "FAILED");
    return success;
}

void OTAHandler::abort_ota() {
    if (ota_in_progress) {
        BLE_LOG("OTA: Aborting update\n");
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
        BLE_LOG("OTA: Failed to initialize patch partition\n");
        return false;
    }
    return true;
}

bool OTAHandler::finalize_update() {
    OTA_DEBUG_LOG("finalize_update() called\n");
    
    // Verify received size matches expected
    OTA_DEBUG_LOG("Verifying received size: expected=%lu, got=%lu\n", 
                  (unsigned long)patch_size, (unsigned long)received_size);
    if (received_size != patch_size) {
        BLE_LOG("OTA: Size mismatch - expected %lu, got %lu\n", 
                     (unsigned long)patch_size, (unsigned long)received_size);
        OTA_DEBUG_LOG("Size verification FAILED\n");
        return false;
    }
    OTA_DEBUG_LOG("Size verification SUCCESS\n");
    
    // A/B Partition Update Logic
    OTA_DEBUG_LOG("Getting running partition...\n");
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (!running_partition) {
        BLE_LOG("❌ Could not get running partition!\n");
        OTA_DEBUG_LOG("esp_ota_get_running_partition() FAILED\n");
        return false;
    }
    OTA_DEBUG_LOG("Running partition: %s (addr=0x%x, size=%lu)\n", 
                  running_partition->label, running_partition->address, 
                  (unsigned long)running_partition->size);

    OTA_DEBUG_LOG("Getting next update partition...\n");
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        BLE_LOG("❌ Could not find a valid OTA update partition!\n");
        OTA_DEBUG_LOG("esp_ota_get_next_update_partition() FAILED\n");
        return false;
    }
    OTA_DEBUG_LOG("Update partition: %s (addr=0x%x, size=%lu)\n", 
                  update_partition->label, update_partition->address, 
                  (unsigned long)update_partition->size);
    
    BLE_LOG("OTA Info: Running from '%s', updating to '%s'\n", 
                  running_partition->label, update_partition->label);

    // Set up delta options for the A/B update
    OTA_DEBUG_LOG("Setting up delta options...\n");
    delta_opts_t opts;
    opts.src = running_partition->label;
    opts.dest = update_partition->label;
    opts.patch = "patch";
    opts.is_full_update = this->is_full_update ? 1 : 0;
    OTA_DEBUG_LOG("Delta opts: src=%s, dest=%s, patch=%s, is_full=%d\n", 
                  opts.src, opts.dest, opts.patch, opts.is_full_update);
    
    // Apply the delta patch
    OTA_DEBUG_LOG("Calling delta_check_and_apply() with size=%lu...\n", (unsigned long)patch_size);
    Serial.flush();
    int result = delta_check_and_apply(patch_size, &opts);
    OTA_DEBUG_LOG("delta_check_and_apply() returned: %d\n", result);
    if (result < 0) {
        BLE_LOG("Delta patch failed: %s\n", delta_error_as_string(result));
        OTA_DEBUG_LOG("Delta patch FAILED with error: %s\n", delta_error_as_string(result));
        return false;
    }
    
    OTA_DEBUG_LOG("finalize_update() SUCCESS - delta patch applied\n");
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
        BLE_LOG("OTA: Patch failed - expected build #%d, got #%d\n", 
                     expected_build_num, current_build);
        preferences->remove("new_build_nr");
        return expected_build;
    } else if (current_build >= expected_build_num) {
        preferences->remove("new_build_nr");
    }
    
    return "";
}
