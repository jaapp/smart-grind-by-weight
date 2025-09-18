#pragma once

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Preferences.h>

#include "../config/bluetooth_config.h"
#include "ota_handler.h"
#include "data_stream.h"

// Forward declaration to avoid circular dependency
class UIManager;

// Callback interface for UI status updates
using UIStatusCallback = std::function<void(const char*)>;

// Small message used to marshal BLE UI status updates to the UI task
struct UIStatusMessage {
    char text[64];
};

// Debug command enums
enum BLEDebugCommand {
    BLE_DEBUG_CMD_ENABLE = 0x01,
    BLE_DEBUG_CMD_DISABLE = 0x02
};

// Data export enums
enum BLEDataCommand {
    BLE_DATA_CMD_STOP_EXPORT = 0x11,
    BLE_DATA_CMD_GET_COUNT = 0x12,
    BLE_DATA_CMD_CLEAR_DATA = 0x13,
    BLE_DATA_CMD_GET_FILE_LIST = 0x14,
    BLE_DATA_CMD_REQUEST_FILE = 0x15
};

enum BLEDataStatus {
    BLE_DATA_IDLE = 0x20,
    BLE_DATA_EXPORTING = 0x21,
    BLE_DATA_COMPLETE = 0x22,
    BLE_DATA_ERROR = 0x23
};

/**
 * BluetoothManager - Central BLE communication manager
 * 
 * Handles BLE connection, characteristic management, and coordinates
 * between OTA updates and data export operations.
 */
class BluetoothManager : public BLEServerCallbacks, public BLECharacteristicCallbacks {
private:
    // BLE Server and services
    BLEServer* ble_server;
    BLEService* ota_service;
    BLEService* data_service;
    BLEService* debug_service;
    BLEService* sysinfo_service;
    
    // OTA characteristics
    BLECharacteristic* ota_data_characteristic;
    BLECharacteristic* ota_control_characteristic;
    BLECharacteristic* ota_status_characteristic;
    BLECharacteristic* build_number_characteristic;
    
    // Data export characteristics
    BLECharacteristic* data_control_characteristic;
    BLECharacteristic* data_transfer_characteristic;
    BLECharacteristic* data_status_characteristic;
    
    // Debug characteristics
    BLECharacteristic* debug_rx_characteristic;
    BLECharacteristic* debug_tx_characteristic;
    
    // System info characteristics
    BLECharacteristic* sysinfo_system_characteristic;
    BLECharacteristic* sysinfo_performance_characteristic;
    BLECharacteristic* sysinfo_hardware_characteristic;
    BLECharacteristic* sysinfo_sessions_characteristic;
    
    // Connection state
    bool device_connected;
    bool ble_enabled;
    bool debug_stream_active;
    unsigned long enable_time;
    unsigned long timeout_ms;
    unsigned long last_disconnect_time;
    
    // Component handlers
    OTAHandler ota_handler;
    DataStreamManager data_stream;
    
    // Data export state
    bool data_export_in_progress;
    BLEDataStatus data_status;
    uint16_t current_chunk;
    unsigned long next_chunk_time;
    uint32_t current_file_session_id;  // For per-file streaming
    
    // UI status callback
    UIStatusCallback ui_status_callback;
    
    // Queue to marshal UI status messages to UI task context
    QueueHandle_t ui_status_queue;

    // Private methods
    void update_ui_status(const char* status);
    void enqueue_ui_status(const char* status);
    void set_ota_status(BLEOTAStatus status);
    void set_data_status(BLEDataStatus status);
    void handle_ota_control_command(BLECharacteristic* characteristic);
    void handle_ota_data_chunk(BLECharacteristic* characteristic);
    void handle_debug_command(BLECharacteristic* characteristic);
    void handle_data_control_command(BLECharacteristic* characteristic);
    void send_next_data_chunk();
    void send_measurement_count();
    void send_log_message(const char* message);
    void clear_measurement_data();
    void send_file_list();
    void send_individual_file(uint32_t session_id);
    void update_system_info();
    void update_performance_info();
    void update_hardware_info(); 
    void update_sessions_info();
    
public:
    BluetoothManager();
    ~BluetoothManager();
    
    /**
     * Initialize Bluetooth manager
     */
    void init(Preferences* prefs);
    
    /**
     * Enable BLE and start advertising
     * @param timeout_ms Optional timeout in milliseconds (defaults to BLE_AUTO_DISABLE_TIMEOUT_MS)
     */
    void enable(unsigned long timeout_ms = 0);
    
    /**
     * Enable BLE during bootup with shorter timeout
     */
    void enable_during_bootup();
    
    /**
     * Disable BLE and cleanup
     */
    void disable();
    
    /**
     * Handle periodic updates (call from main loop)
     */
    void handle();
    
    /**
     * Start/stop advertising
     */
    void start_advertising();
    void stop_advertising();
    
    /**
     * Status queries
     */
    bool is_enabled() const { return ble_enabled; }
    bool is_connected() const { return device_connected; }
    bool is_updating() const { return ota_handler.is_ota_active(); }
    bool is_debug_stream_active() const { return debug_stream_active; }
    
    /**
     * OTA progress information
     */
    float get_ota_progress() const { return ota_handler.get_progress(); }
    unsigned long get_remaining_time_ms() const;
    
    /**
     * General Bluetooth timeout information
     */
    unsigned long get_bluetooth_timeout_remaining_ms() const;
    
    /**
     * Data export methods
     */
    void start_data_export();
    void stop_data_export();
    void update_data_export();
    bool is_data_export_active() const { return data_export_in_progress; }
    float get_data_export_progress() const;
    uint32_t get_data_export_session_count() const;

    /**
     * Log a message to Serial and optionally over BLE
     */
    void log(const char* format, ...);
    
    /**
     * Set UI status callback
     */
    void set_ui_status_callback(UIStatusCallback callback);
    
    /**
     * Update all system information characteristics
     */
    void refresh_system_info();
    
    /**
     * Check if OTA failed after reboot and return expected build number if so
     * @return Expected build number if OTA failed, empty string if no failure
     */
    String check_ota_failure_after_boot();
    
    // BLE Callbacks
    void onConnect(BLEServer* server) override;
    void onDisconnect(BLEServer* server) override;
    void onWrite(BLECharacteristic* characteristic) override;

    // Drain a status message queued from BLE task; called by UI task
    bool dequeue_ui_status(char* out, size_t out_len);
};
