#include "arduino_compat.h"
#include "littlefs_idf.h"
#include <esp_system.h>
#include <nvs_flash.h>
#include "hardware/hardware_manager.h"
#include "system/state_machine.h"
#include "system/statistics_manager.h"
#include "controllers/profile_controller.h"
#include "controllers/grind_controller.h"
#include "ui/ui_manager.h"
#include "config/constants.h"
#include "bluetooth/manager.h"
#include "tasks/task_manager.h"
#include "tasks/weight_sampling_task.h"
#include "tasks/grind_control_task.h"
#include "tasks/file_io_task.h"

HardwareManager hardware_manager;
StateMachine state_machine;
ProfileController profile_controller;
GrindController grind_controller;
UIManager ui_manager;
BluetoothManager g_bluetooth_manager;
BluetoothManager& bluetooth_manager = g_bluetooth_manager;

#if SYS_ENABLE_REALTIME_HEARTBEAT
static uint32_t core1_cycle_count_10s = 0;
static uint32_t core1_cycle_time_sum_ms = 0;
static uint32_t core1_cycle_time_min_ms = UINT32_MAX;
static uint32_t core1_cycle_time_max_ms = 0;
static uint32_t core1_last_heartbeat_time = 0;
#endif

// Main loop task — replaces Arduino loop(), runs on Core 1
static void main_loop_task(void*) {
    for (;;) {
#if SYS_ENABLE_REALTIME_HEARTBEAT
        uint32_t cycle_start_time = millis();
        core1_cycle_count_10s++;
        if (core1_last_heartbeat_time == 0) core1_last_heartbeat_time = cycle_start_time;
#endif

        // Update device uptime statistics every 15 minutes
        static uint32_t last_uptime_update = 0;
        static uint32_t pending_uptime_minutes = 0;
        uint32_t current_time = millis();
        if (last_uptime_update == 0) last_uptime_update = current_time;

        constexpr uint32_t kUptimeIntervalMs = 900000;
        constexpr uint32_t kUptimeIntervalMinutes = 15;
        uint32_t elapsed_ms = current_time - last_uptime_update;
        if (elapsed_ms >= kUptimeIntervalMs) {
            uint32_t intervals = elapsed_ms / kUptimeIntervalMs;
            pending_uptime_minutes += intervals * kUptimeIntervalMinutes;
            last_uptime_update += intervals * kUptimeIntervalMs;
            if (pending_uptime_minutes > 0) {
                statistics_manager.update_uptime(pending_uptime_minutes);
                pending_uptime_minutes = 0;
            }
        }

        // OTA task suspension
        static bool hardware_suspended = false;
        bool ota_active = bluetooth_manager.is_updating();
        if (ota_active && !hardware_suspended) {
            task_manager.suspend_hardware_tasks();
            hardware_suspended = true;
            LOG_BLE("[MAIN] Hardware tasks suspended for OTA\n");
        } else if (!ota_active && hardware_suspended) {
            task_manager.resume_hardware_tasks();
            hardware_suspended = false;
            LOG_BLE("[MAIN] Hardware tasks resumed after OTA\n");
        }

#if SYS_ENABLE_REALTIME_HEARTBEAT
        uint32_t cycle_end_time = millis();
        uint32_t cycle_duration = cycle_end_time - cycle_start_time;
        core1_cycle_time_sum_ms += cycle_duration;
        if (cycle_duration < core1_cycle_time_min_ms) core1_cycle_time_min_ms = cycle_duration;
        if (cycle_duration > core1_cycle_time_max_ms) core1_cycle_time_max_ms = cycle_duration;

        if (cycle_end_time - core1_last_heartbeat_time >= SYS_REALTIME_HEARTBEAT_INTERVAL_MS) {
            uint32_t avg_cycle_time = core1_cycle_count_10s > 0
                ? core1_cycle_time_sum_ms / core1_cycle_count_10s : 0;
            bool is_grinding = grind_controller.is_active();
            const char* ble_state = bluetooth_manager.is_enabled()
                ? (bluetooth_manager.is_connected() ? "CONN" : "ADV") : "OFF";
            const char* tasks_status = task_manager.are_tasks_healthy() ? "HEALTHY" : "ERROR";
            size_t free_heap_kb = esp_get_free_heap_size() / 1024;

            LOG_BLE("[%lums MAIN_LOOP_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | "
                    "Tasks: %s | BLE: %s | Grinder: %s | Mem: %zuKB | Build: #%d\n",
                    (unsigned long)millis(), (unsigned long)core1_cycle_count_10s,
                    (unsigned long)avg_cycle_time,
                    (unsigned long)core1_cycle_time_min_ms,
                    (unsigned long)core1_cycle_time_max_ms,
                    tasks_status, ble_state,
                    is_grinding ? "ACTIVE" : "IDLE",
                    free_heap_kb, BUILD_NUMBER);

            core1_cycle_count_10s = 0;
            core1_cycle_time_sum_ms = 0;
            core1_cycle_time_min_ms = UINT32_MAX;
            core1_cycle_time_max_ms = 0;
            core1_last_heartbeat_time = cycle_end_time;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main() {
    // Initialize NVS flash (required for Preferences / BLE)
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

#ifdef UI_DEBUG_SERIAL_DELAY_MS
    vTaskDelay(pdMS_TO_TICKS(UI_DEBUG_SERIAL_DELAY_MS));
#endif

    esp_reset_reason_t rr = esp_reset_reason();
    const char* rr_str = "UNKNOWN";
    switch (rr) {
        case ESP_RST_POWERON:   rr_str = "POWERON"; break;
        case ESP_RST_EXT:       rr_str = "EXT (Reset Pin)"; break;
        case ESP_RST_SW:        rr_str = "SW (esp_restart)"; break;
        case ESP_RST_PANIC:     rr_str = "PANIC (Exception)"; break;
        case ESP_RST_INT_WDT:   rr_str = "INT_WDT"; break;
        case ESP_RST_TASK_WDT:  rr_str = "TASK_WDT"; break;
        case ESP_RST_WDT:       rr_str = "WDT"; break;
        case ESP_RST_DEEPSLEEP: rr_str = "DEEPSLEEP"; break;
        case ESP_RST_BROWNOUT:  rr_str = "BROWNOUT"; break;
        default: break;
    }
    LOG_BLE("[STARTUP] Reset reason: %s (%d)\n", rr_str, rr);
    LOG_BLE("[STARTUP] Initializing ESP32-S3 Coffee Scale - Build %d\n", BUILD_NUMBER);

    if (!LittleFS.begin(true)) {
        LOG_BLE("ERROR: LittleFS mount failed - continuing without filesystem\n");
    } else {
        LOG_BLE("LittleFS mounted successfully\n");
    }

    hardware_manager.init();
    profile_controller.init(hardware_manager.get_preferences());
    statistics_manager.init(hardware_manager.get_preferences());
    grind_controller.init(hardware_manager.get_load_cell(), hardware_manager.get_grinder(),
                          hardware_manager.get_preferences());
    hardware_manager.set_grind_controller(&grind_controller);

    bluetooth_manager.init(hardware_manager.get_preferences());

    std::string failed_ota_build = bluetooth_manager.check_ota_failure_after_boot();
    bool ota_failed = !failed_ota_build.empty();

    bool is_calibrated = hardware_manager.get_weight_sensor()->is_calibrated();

    if (ota_failed) {
        LOG_BLE("BOOT: Starting in OTA failure state for expected build %s\n",
                failed_ota_build.c_str());
        state_machine.init(UIState::OTA_UPDATE_FAILED);
    } else if (!is_calibrated) {
        LOG_BLE("BOOT: Device not calibrated - starting in CALIBRATION state\n");
        state_machine.init(UIState::CALIBRATION);
    } else {
        state_machine.init(UIState::READY);
    }

    ui_manager.init(&hardware_manager, &state_machine, &profile_controller,
                    &grind_controller, &bluetooth_manager);

    if (ota_failed) {
        if (auto* ota = ui_manager.get_ota_data_export_controller()) {
            ota->set_failure_info(failed_ota_build.c_str());
        }
    }

    bluetooth_manager.set_ui_status_callback([](const char* status) {
        if (auto* ota = ui_manager.get_ota_data_export_controller()) {
            ota->update_status(status);
        }
    });

    bluetooth_manager.enable_during_bootup();

    LOG_BLE("[STARTUP] Initializing task module dependencies...\n");
    weight_sampling_task.init(hardware_manager.get_load_cell(), &grind_logger);
    grind_control_task.init(&grind_controller, hardware_manager.get_load_cell(),
                            hardware_manager.get_grinder(), &grind_logger);
    LOG_BLE("Task module dependencies initialized\n");

    LOG_BLE("[STARTUP] Initializing FreeRTOS Task Architecture...\n");
    bool task_init_success = task_manager.init(&hardware_manager, &state_machine,
                                               &profile_controller, &grind_controller,
                                               &bluetooth_manager, &ui_manager);
    if (!task_init_success) {
        LOG_BLE("ERROR: Failed to initialize TaskManager - system cannot start\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    LOG_BLE("TaskManager initialized successfully\n");

    file_io_task.init(task_manager.get_file_io_queue());
    LOG_BLE("All task modules initialized\n");

    // Spawn main-loop monitoring task on Core 1 (lowest priority)
    xTaskCreatePinnedToCore(main_loop_task, "main_loop", 4096, nullptr, 1, nullptr, 1);

    // app_main returns — the FreeRTOS scheduler keeps running all tasks
}
