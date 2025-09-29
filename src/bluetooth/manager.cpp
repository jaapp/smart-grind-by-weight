#include "manager.h"
#include <cstdarg>
#include <Arduino.h>
#include <esp_system.h>
#include "../system/performance_monitor.h"
#include "../config/constants.h"
#include "../logging/grind_logging.h"

BluetoothManager::BluetoothManager()
    : ble_server(nullptr)
    , ota_service(nullptr)
    , data_service(nullptr)
    , debug_service(nullptr)
    , sysinfo_service(nullptr)
    , ota_data_characteristic(nullptr)
    , ota_control_characteristic(nullptr)
    , ota_status_characteristic(nullptr)
    , build_number_characteristic(nullptr)
    , data_control_characteristic(nullptr)
    , data_transfer_characteristic(nullptr)
    , data_status_characteristic(nullptr)
    , debug_rx_characteristic(nullptr)
    , debug_tx_characteristic(nullptr)
    , sysinfo_system_characteristic(nullptr)
    , sysinfo_performance_characteristic(nullptr)
    , sysinfo_hardware_characteristic(nullptr)
    , sysinfo_sessions_characteristic(nullptr)
    , device_connected(false)
    , ble_enabled(false), debug_stream_active(false)
    , enable_time(0)
    , timeout_ms(BLE_AUTO_DISABLE_TIMEOUT_MS)
    , last_disconnect_time(0)
    , data_export_in_progress(false)
    , data_status(BLE_DATA_IDLE)
    , current_chunk(0)
    , next_chunk_time(0)
    , ui_status_queue(nullptr) {
}

BluetoothManager::~BluetoothManager() {
    disable();
}

void BluetoothManager::init(Preferences* prefs) {
    log("Bluetooth: Manager initialized (enable via Developer Mode)\n");
    ota_handler.init(prefs);
    // Create UI status queue to marshal UI updates to UI task
    if (!ui_status_queue) {
        ui_status_queue = xQueueCreate(8, sizeof(UIStatusMessage));
    }
}

void BluetoothManager::set_ui_status_callback(UIStatusCallback callback) {
    ui_status_callback = callback;
}

void BluetoothManager::update_ui_status(const char* status) {
    // Don't call into UI from BLE task; enqueue for UI task to process
    enqueue_ui_status(status);
}

void BluetoothManager::enqueue_ui_status(const char* status) {
    if (!ui_status_queue) return;
    UIStatusMessage msg;
    msg.text[0] = '\0';
    if (status) {
        strncpy(msg.text, status, sizeof(msg.text) - 1);
        msg.text[sizeof(msg.text) - 1] = '\0';
    }
    // Non-blocking send; drop if full to avoid blocking BLE task
    xQueueSend(ui_status_queue, &msg, 0);
}

bool BluetoothManager::dequeue_ui_status(char* out, size_t out_len) {
    if (!ui_status_queue || !out || out_len == 0) return false;
    UIStatusMessage msg;
    if (xQueueReceive(ui_status_queue, &msg, 0) == pdPASS) {
        strncpy(out, msg.text, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

void BluetoothManager::enable(unsigned long timeout_ms) {
    if (ble_enabled) return;
    
    // Use default timeout if none specified
    if (timeout_ms == 0) {
        timeout_ms = BLE_AUTO_DISABLE_TIMEOUT_MS;
    }
    this->timeout_ms = timeout_ms;
    unsigned long timeout_minutes = timeout_ms / 60000;
    log("Bluetooth: Enabling BLE with reduced power settings (%lum timeout)\n", timeout_minutes);
    
    // Enable reduced power mode for BLE
    ota_handler.enable_ble_power_mode();
    enable_time = millis();
    last_disconnect_time = enable_time; // Start disconnected timeout from enable time
    
    // Initialize BLE with delays for power stability
    BLEDevice::init(BLE_OTA_DEVICE_NAME);
    // Request a larger MTU to improve throughput when the client supports it.
    // Some platforms (e.g., macOS/iOS) may ignore this request and keep a lower MTU.
    // That's fine — we also keep chunk sizes small and paced below.
    BLEDevice::setMTU(517);
    delay(BLE_INIT_STACK_DELAY_MS);
    
    ble_server = BLEDevice::createServer();
    delay(BLE_INIT_SERVER_DELAY_MS);
    ble_server->setCallbacks(this);
    
    // Create OTA service
    ota_service = ble_server->createService(BLE_OTA_SERVICE_UUID);
    delay(BLE_INIT_SERVICE_DELAY_MS);
    
    ota_data_characteristic = ota_service->createCharacteristic(
        BLE_OTA_DATA_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    ota_data_characteristic->setCallbacks(this);
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    ota_control_characteristic = ota_service->createCharacteristic(
        BLE_OTA_CONTROL_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    ota_control_characteristic->setCallbacks(this);
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    ota_status_characteristic = ota_service->createCharacteristic(
        BLE_OTA_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    build_number_characteristic = ota_service->createCharacteristic(
        BLE_OTA_BUILD_NUMBER_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    build_number_characteristic->setValue(ota_handler.get_build_number().c_str());
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    // Create measurement data service
    data_service = ble_server->createService(BLE_DATA_SERVICE_UUID);
    delay(BLE_INIT_SERVICE_DELAY_MS);
    
    data_control_characteristic = data_service->createCharacteristic(
        BLE_DATA_CONTROL_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    data_control_characteristic->setCallbacks(this);
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    data_transfer_characteristic = data_service->createCharacteristic(
        BLE_DATA_TRANSFER_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    data_status_characteristic = data_service->createCharacteristic(
        BLE_DATA_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    // Create debug service (Nordic UART)
    debug_service = ble_server->createService(BLE_DEBUG_SERVICE_UUID);
    delay(BLE_INIT_SERVICE_DELAY_MS);

    debug_rx_characteristic = debug_service->createCharacteristic(
        BLE_DEBUG_RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    debug_rx_characteristic->setCallbacks(this);
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);

    debug_tx_characteristic = debug_service->createCharacteristic(
        BLE_DEBUG_TX_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    // Create system info service
    sysinfo_service = ble_server->createService(BLE_SYSINFO_SERVICE_UUID);
    delay(BLE_INIT_SERVICE_DELAY_MS);
    
    sysinfo_system_characteristic = sysinfo_service->createCharacteristic(
        BLE_SYSINFO_SYSTEM_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    sysinfo_performance_characteristic = sysinfo_service->createCharacteristic(
        BLE_SYSINFO_PERFORMANCE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    sysinfo_hardware_characteristic = sysinfo_service->createCharacteristic(
        BLE_SYSINFO_HARDWARE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    sysinfo_sessions_characteristic = sysinfo_service->createCharacteristic(
        BLE_SYSINFO_SESSIONS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    delay(BLE_INIT_CHARACTERISTIC_DELAY_MS);
    
    ota_service->start();
    delay(BLE_INIT_START_DELAY_MS);
    
    data_service->start();
    delay(BLE_INIT_START_DELAY_MS);
    
    debug_service->start();
    delay(BLE_INIT_START_DELAY_MS);
    
    sysinfo_service->start();
    delay(BLE_INIT_START_DELAY_MS);
    
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_OTA_SERVICE_UUID);
    advertising->addServiceUUID(BLE_DEBUG_SERVICE_UUID);
    advertising->addServiceUUID(BLE_DATA_SERVICE_UUID);
    advertising->addServiceUUID(BLE_SYSINFO_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    delay(BLE_INIT_ADVERTISING_DELAY_MS);
    
    ble_enabled = true;
    set_ota_status(BLE_OTA_READY);
    
    // Initialize system information
    refresh_system_info();
    
    start_advertising();
    log("Bluetooth: Ready - device is advertising (%lum timeout)\n", timeout_minutes);
}

void BluetoothManager::enable_during_bootup() {
    Preferences prefs;
    prefs.begin("bluetooth", true);
    if (prefs.getBool("startup", true)) {
        enable(BLE_BOOTUP_AUTO_DISABLE_TIMEOUT_MS);
    }
    prefs.end();
}

void BluetoothManager::disable() {
    if (!ble_enabled) return;
    
    log("Bluetooth: Disabling BLE and restoring normal power...\n");
    
    if (ota_handler.is_ota_active()) {
        ota_handler.abort_ota();
    }
    
    if (data_export_in_progress) {
        stop_data_export();
    }
    
    stop_advertising();
    delay(BLE_SHUTDOWN_ADVERTISING_DELAY_MS);
    
    log("Bluetooth: Deinitializing BLE stack...\n");
    BLEDevice::deinit(false);
    delay(BLE_SHUTDOWN_DEINIT_DELAY_MS);
    
    ble_enabled = false;
    device_connected = false;
    ble_server = nullptr;
    ota_service = nullptr;
    data_service = nullptr;
    debug_service = nullptr;
    sysinfo_service = nullptr;
    ota_data_characteristic = nullptr;
    ota_control_characteristic = nullptr;
    ota_status_characteristic = nullptr;
    build_number_characteristic = nullptr;
    data_control_characteristic = nullptr;
    data_transfer_characteristic = nullptr;
    data_status_characteristic = nullptr;
    debug_rx_characteristic = nullptr;
    debug_tx_characteristic = nullptr;
    sysinfo_system_characteristic = nullptr;
    sysinfo_performance_characteristic = nullptr;
    sysinfo_hardware_characteristic = nullptr;
    sysinfo_sessions_characteristic = nullptr;
    debug_stream_active = false;
    
    // Restore normal power settings
    ota_handler.restore_normal_power_mode();
    log("Bluetooth: Disable complete\n");
}

void BluetoothManager::handle() {
    if (!ble_enabled) return;
    
    // Only check timeout when no client is connected
    if (!device_connected) {
        unsigned long disconnected_elapsed = millis() - last_disconnect_time;
        
        if (disconnected_elapsed > timeout_ms) {
            log("Bluetooth: Timeout reached (%lu minutes disconnected), disabling BLE\n", timeout_ms / 60000);
            disable();
            return;
        }
    } else {
        // While connected, constantly reset timeout to default for UI display
        timeout_ms = BLE_AUTO_DISABLE_TIMEOUT_MS;
    }
    
    // Handle data export updates
    update_data_export();
    
    // Update system info periodically if connected (every 10 seconds)
    static unsigned long last_sysinfo_update = 0;
    if (device_connected && millis() - last_sysinfo_update > 10000) {
        refresh_system_info();
        last_sysinfo_update = millis();
    }
}

void BluetoothManager::start_advertising() {
    if (ble_enabled && ble_server) {
        BLEDevice::startAdvertising();
    }
}

void BluetoothManager::stop_advertising() {
    if (ble_enabled) {
        BLEDevice::stopAdvertising();
    }
}

unsigned long BluetoothManager::get_remaining_time_ms() const {
    if (!ota_handler.is_ota_active()) return 0;
    
    unsigned long elapsed = millis() - enable_time;
    float progress = ota_handler.get_progress();
    if (progress <= 0) return 0;
    
    unsigned long total_estimated = elapsed * 100.0f / progress;
    return (total_estimated > elapsed) ? (total_estimated - elapsed) : 0;
}

unsigned long BluetoothManager::get_bluetooth_timeout_remaining_ms() const {
    if (!ble_enabled) return 0;
    
    if (device_connected) {
        // When connected, use the full timeout from connection time
        unsigned long elapsed = millis() - enable_time;
        return (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 0;
    } else {
        // When disconnected, use time since last disconnect
        unsigned long disconnected_elapsed = millis() - last_disconnect_time;
        return (disconnected_elapsed < timeout_ms) ? (timeout_ms - disconnected_elapsed) : 0;
    }
}


void BluetoothManager::start_data_export() {
    if (!ble_enabled || !device_connected) {
        log("Bluetooth Data: Cannot start export - BLE not enabled or not connected\n");
        return;
    }
    
    if (data_export_in_progress) {
        log("Bluetooth Data: Export already in progress\n");
        return;
    }
    
    log("Bluetooth Data: Starting data export - sending file list\n");
    send_file_list();
}

void BluetoothManager::stop_data_export() {
    if (!data_export_in_progress) { // No log needed if nothing is happening
        return;
    }
    
    log("Bluetooth Data: Stopping export\n");
    data_export_in_progress = false;
    current_chunk = 0;
    next_chunk_time = 0;
    current_file_session_id = 0;
    
    // Clean shutdown of stream
    data_stream.close_stream();
    
    set_data_status(BLE_DATA_IDLE);
}

void BluetoothManager::update_data_export() {
    // Check if we need to send the next chunk
    if (data_export_in_progress && millis() >= next_chunk_time) {
        send_next_data_chunk();
    }
}

void BluetoothManager::send_next_data_chunk() {
    if (!data_export_in_progress || !data_transfer_characteristic) {
        return;
    }

    // If client dropped mid-transfer, stop cleanly and avoid further notify attempts
    if (!device_connected) {
        stop_data_export();
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    uint8_t buffer[BLE_DATA_CHUNK_SIZE_BYTES];
    size_t actual_size = 0;
    
    // Per-file streaming only
    if (current_file_session_id == 0) {
        log("Bluetooth Data: No file session active for chunk request\n");
        stop_data_export();
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    bool has_data = data_stream.read_file_chunk(buffer, sizeof(buffer), &actual_size);
    
    if (has_data && actual_size > 0) {
        // Send the data chunk
        data_transfer_characteristic->setValue(buffer, actual_size);
        data_transfer_characteristic->notify();
        
        current_chunk++;
        
        // Chunk sent (reduced logging)
        
        // Update progress
        uint8_t progress = data_stream.get_progress_percent();
        uint8_t status_data[2] = { (uint8_t)BLE_DATA_EXPORTING, progress };
        if (data_status_characteristic) {
            data_status_characteristic->setValue(status_data, 2);
            data_status_characteristic->notify();
        }
        
        // Pace notifications to avoid overflowing BLE buffers/OS queues.
        // Combined with smaller chunk size this greatly reduces blocking in notify().
        next_chunk_time = millis() + 25; // ~6.4 KB/s at 160 B per 25 ms
    }
    
    // Check if file transfer is complete
    if (!has_data) {
        log("Bluetooth Data: File transfer complete for session %lu - sent %d chunks.\n", current_file_session_id, current_chunk);
        data_export_in_progress = false;
        current_chunk = 0;
        current_file_session_id = 0;
        
        data_stream.close_stream();
        
        delay(200); // Give the BLE buffer time to clear
        set_data_status(BLE_DATA_COMPLETE);
    }
}

float BluetoothManager::get_data_export_progress() const {
    if (!data_export_in_progress) return 0.0f;
    return static_cast<float>(data_stream.get_progress_percent());
}

uint32_t BluetoothManager::get_data_export_session_count() const {
    return data_stream.get_total_sessions();
}

void BluetoothManager::send_measurement_count() {
    uint16_t count = data_stream.get_total_sessions();
    
    if (data_status_characteristic) {
        // Send as 2-byte little-endian
        uint8_t count_data[2];
        count_data[0] = count & 0xFF;
        count_data[1] = (count >> 8) & 0xFF;
        data_status_characteristic->setValue(count_data, 2);
        data_status_characteristic->notify();
    }
    
    log("Bluetooth Data: Sent measurement count: %d\n", count);
}

void BluetoothManager::send_log_message(const char* message) {
    if (debug_stream_active && device_connected && debug_tx_characteristic) {
        debug_tx_characteristic->setValue((uint8_t*)message, strlen(message));
        debug_tx_characteristic->notify();
    }
}

void BluetoothManager::clear_measurement_data() {
    // Note: GrindLogger doesn't have a clear method, data will be overwritten in circular buffer
    log("Bluetooth Data: Measurement data will be overwritten by new grinds\n");
    set_data_status(BLE_DATA_IDLE);
}

void BluetoothManager::send_file_list() {
    if (!ble_enabled || !device_connected || !data_transfer_characteristic) {
        log("Bluetooth Data: Cannot send file list - BLE not enabled or not connected\n");
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    // Get list of session files
    const uint32_t max_sessions = 100;  // Limit to prevent memory issues
    uint32_t* session_ids = (uint32_t*)malloc(max_sessions * sizeof(uint32_t));
    if (!session_ids) {
        log("ERROR: Failed to allocate memory for session list\n");
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    uint32_t session_count = data_stream.get_session_list(session_ids, max_sessions);
    
    // Send file list: [session_count:4][session_id1:4][session_id2:4]...
    size_t total_size = 4 + (session_count * 4);
    uint8_t* buffer = (uint8_t*)malloc(total_size);
    if (!buffer) {
        free(session_ids);
        log("ERROR: Failed to allocate memory for file list buffer\n");
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    // Write session count (little-endian)
    buffer[0] = session_count & 0xFF;
    buffer[1] = (session_count >> 8) & 0xFF;
    buffer[2] = (session_count >> 16) & 0xFF;
    buffer[3] = (session_count >> 24) & 0xFF;
    
    // Write session IDs
    for (uint32_t i = 0; i < session_count; i++) {
        uint32_t offset = 4 + (i * 4);
        buffer[offset + 0] = session_ids[i] & 0xFF;
        buffer[offset + 1] = (session_ids[i] >> 8) & 0xFF;
        buffer[offset + 2] = (session_ids[i] >> 16) & 0xFF;
        buffer[offset + 3] = (session_ids[i] >> 24) & 0xFF;
    }
    
    // Send the file list
    data_transfer_characteristic->setValue(buffer, total_size);
    data_transfer_characteristic->notify();
    
    log("Bluetooth Data: Sent file list with %lu sessions\n", session_count);
    
    free(session_ids);
    free(buffer);
    
    // Give the BLE buffer time to transmit the data before sending status
    delay(100);
    
    // Mark transfer complete so client stops waiting
    set_data_status(BLE_DATA_COMPLETE);
}

void BluetoothManager::send_individual_file(uint32_t session_id) {
    if (!ble_enabled || !device_connected) {
        log("Bluetooth Data: Cannot start file transfer - BLE not enabled or not connected\n");
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    if (data_export_in_progress) {
        log("❌ FILE REQUEST ATTEMPT - Transfer already in progress! Client sent duplicate REQUEST command.\n");
        return;
    }
    
    // Initialize individual file stream
    if (!data_stream.initialize_file_stream(session_id)) {
        log("Bluetooth Data: Failed to initialize file stream for session %lu\n", session_id);
        set_data_status(BLE_DATA_ERROR);
        return;
    }
    
    log("Bluetooth Data: Starting individual file transfer for session %lu\n", session_id);
    
    data_export_in_progress = true;
    current_file_session_id = session_id;
    current_chunk = 0;
    next_chunk_time = millis(); // Start immediately
    set_data_status(BLE_DATA_EXPORTING);
}

void BluetoothManager::log(const char* format, ...) {
    char buffer[512]; // Increased from 256 to 2048 bytes for large debug messages.
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Note: This is the log method itself, so we print to Serial directly
    Serial.print(buffer);

    // is_debug_stream_active() is the same as debug_stream_active
    if (debug_stream_active) {
        send_log_message(buffer);
    }
}

void BluetoothManager::set_ota_status(BLEOTAStatus status) {
    if (ota_status_characteristic) {
        uint8_t status_value = static_cast<uint8_t>(status);
        ota_status_characteristic->setValue(&status_value, 1);
        ota_status_characteristic->notify();
    }
}

void BluetoothManager::set_data_status(BLEDataStatus status) {
    data_status = status;
    if (data_status_characteristic) {
        uint8_t status_value = static_cast<uint8_t>(status);
        data_status_characteristic->setValue(&status_value, 1);
        data_status_characteristic->notify();
    }
}

void BluetoothManager::handle_ota_control_command(BLECharacteristic* characteristic) {
    String data = characteristic->getValue();
    if (data.length() == 0) return;
    
    uint8_t command = data[0];
    
    switch (command) {
        case BLE_OTA_CMD_START:
            // New protocol: [CMD][patch_size:4][is_full_update:1][build_number_length:1][build_number:N]
            if (data.length() >= 6) {  // 1 + 4 + 1 bytes minimum (cmd + patch_size + full_update_flag)
                uint32_t patch_size = *(uint32_t*)(data.c_str() + 1);
                bool is_full_update = data[5] != 0;
                
                log("Bluetooth OTA: Starting %s update (%lu KB)\n", 
                    is_full_update ? "full" : "delta", (unsigned long)patch_size / 1024);
                update_ui_status("Receiving update...");
                
                // Parse build number if present
                String expected_build = "";
                if (data.length() > 6) {
                    uint8_t build_length = data[6];
                    if (build_length > 0 && data.length() >= 7 + build_length) {
                        expected_build = String(data.c_str() + 7, build_length);
                        log("Bluetooth OTA: Expected build after update: %s\n", expected_build.c_str());
                    }
                }
                
                if (ota_handler.start_ota(patch_size, expected_build, is_full_update)) {
                    set_ota_status(BLE_OTA_RECEIVING);
                } else {
                    set_ota_status(BLE_OTA_ERROR);
                }
            } else {
                log("Bluetooth OTA: ❌ Invalid start command format (need at least 6 bytes)\n");
                set_ota_status(BLE_OTA_ERROR);
            }
            break;
            
        case BLE_OTA_CMD_END:            
            log("Bluetooth OTA: Received END command\n");
            LOG_OTA_DEBUG("BLE_OTA_CMD_END received, checking if OTA active...\n");
            if (ota_handler.is_ota_active()) {
                LOG_OTA_DEBUG("OTA is active, updating UI status...\n");
                update_ui_status("Applying patch...");
                LOG_OTA_DEBUG("UI status updated, calling complete_ota()...\n");
                if (ota_handler.complete_ota()) {
                    LOG_OTA_DEBUG("complete_ota() returned SUCCESS\n");
                    set_ota_status(BLE_OTA_SUCCESS);
                    update_ui_status("Restarting...");
                } else {
                    LOG_OTA_DEBUG("complete_ota() returned FAILED\n");
                    set_ota_status(BLE_OTA_ERROR);
                }
            } else {
                LOG_OTA_DEBUG("OTA is NOT active - ignoring END command\n");
            }
            break;
            
        case BLE_OTA_CMD_ABORT:
            ota_handler.abort_ota();
            set_ota_status(BLE_OTA_ERROR);
            break;
    }
}

void BluetoothManager::handle_ota_data_chunk(BLECharacteristic* characteristic) {
    if (!ota_handler.is_ota_active()) return;
    
    String data = characteristic->getValue();
    size_t chunk_size = data.length();
    if (chunk_size == 0) return;
    
    if (!ota_handler.process_data_chunk((const uint8_t*)data.c_str(), chunk_size)) {
        set_ota_status(BLE_OTA_ERROR);
    }
}

void BluetoothManager::handle_debug_command(BLECharacteristic* characteristic) {
    String value = characteristic->getValue();
    if (value.length() > 0) {
        uint8_t command = value[0];
        switch (command) {
            case BLE_DEBUG_CMD_ENABLE:
                debug_stream_active = true;
                log("BLE_DEBUG: Stream enabled\n");
#if ENABLE_GRIND_DEBUG
                // Print struct layout debug info immediately upon debug stream activation
                log("BLE_DEBUG: Printing struct layout debug info...\n");
                delay(50); // Small delay to ensure previous message is sent
                grind_logger.print_struct_layout_debug();
                log("BLE_DEBUG: Struct layout debug info complete\n");
#else
                log("BLE_DEBUG: Struct layout debug disabled (ENABLE_GRIND_DEBUG=0)\n");
#endif
                break;
            case BLE_DEBUG_CMD_DISABLE:
                log("BLE_DEBUG: Stream disabled\n");
                debug_stream_active = false;
                break;
            case 0x00: // Keepalive from python script
                break;
            default:
                // Echo command for now
                log("BLE_DEBUG: Received '%.*s'\n", (int)value.length(), value.c_str());
                break;
        }
    }
}

void BluetoothManager::handle_data_control_command(BLECharacteristic* characteristic) {
    String data = characteristic->getValue();
    if (data.length() == 0) return;
    
    uint8_t command = data[0];
    log("Bluetooth Data: Received command 0x%02X\n", command);
    
    switch (command) {
        case BLE_DATA_CMD_STOP_EXPORT:
            log("Bluetooth Data: Stopping measurement data export\n");
            stop_data_export();
            break;
            
        case BLE_DATA_CMD_GET_COUNT:
            log("Bluetooth Data: Getting measurement count\n");
            send_measurement_count();
            break;
            
        case BLE_DATA_CMD_CLEAR_DATA:
            log("Bluetooth Data: Clearing measurement data\n");
            clear_measurement_data();
            break;
            
        case BLE_DATA_CMD_GET_FILE_LIST:
            log("Bluetooth Data: Getting file list\n");
            send_file_list();
            break;
            
        case BLE_DATA_CMD_REQUEST_FILE:
            if (data.length() >= 5) {
                uint32_t session_id = 0;
                memcpy(&session_id, data.c_str() + 1, 4);
                log("Bluetooth Data: Requesting file for session %lu\n", session_id);
                send_individual_file(session_id);
            } else {
                log("Bluetooth Data: Invalid REQUEST_FILE command length\n");
                set_data_status(BLE_DATA_ERROR);
            }
            break;
            
        default:
            log("Bluetooth Data: Unknown command: 0x%02X\n", command);
            set_data_status(BLE_DATA_ERROR);
            break;
    }
}

// BLE Callbacks
void BluetoothManager::onConnect(BLEServer* server) {
    device_connected = true;
    log("BLE: Client connected - timeout paused while connected\n");
}

void BluetoothManager::onDisconnect(BLEServer* server) {
    device_connected = false;
    last_disconnect_time = millis(); // Reset timeout countdown from now
    
    log("BLE: Client disconnected - timeout countdown resumed\n");
    
    if (ota_handler.is_ota_active()) {
        ota_handler.abort_ota();
    }
    
    if (data_export_in_progress) {
        stop_data_export();
    }
    
    debug_stream_active = false;
    
    // Restart advertising for next connection
    delay(500);
    start_advertising();
}

void BluetoothManager::onWrite(BLECharacteristic* characteristic) {
    if (characteristic == ota_control_characteristic) {
        handle_ota_control_command(characteristic);
    } else if (characteristic == ota_data_characteristic) {
        handle_ota_data_chunk(characteristic);
    } else if (characteristic == debug_rx_characteristic) {
        handle_debug_command(characteristic);
    } else if (characteristic == data_control_characteristic) {
        handle_data_control_command(characteristic);
    }
}

String BluetoothManager::check_ota_failure_after_boot() {
    return ota_handler.check_ota_failure_after_boot();
}

void BluetoothManager::refresh_system_info() {
    if (!ble_enabled || !device_connected) return;
    
    update_system_info();
    update_performance_info();
    update_hardware_info();
    update_sessions_info();
}

void BluetoothManager::update_system_info() {
    if (!sysinfo_system_characteristic) return;
    
    // Create JSON-like structure for system info
    char buffer[BLE_SYSINFO_MAX_PAYLOAD_BYTES];
    unsigned long uptime_ms = millis();
    unsigned long uptime_seconds = uptime_ms / 1000;
    unsigned long uptime_minutes = uptime_seconds / 60;
    unsigned long uptime_hours = uptime_minutes / 60;
    
    // Get ESP32 system information
    size_t heap_free = ESP.getFreeHeap();
    size_t heap_total = ESP.getHeapSize();
    size_t heap_used = heap_total - heap_free;
    uint32_t flash_size = ESP.getFlashChipSize();
    float heap_usage_percent = (float(heap_used) / float(heap_total)) * 100.0f;
    
    snprintf(buffer, sizeof(buffer),
        "{"
        "\"version\":\"%s\","
        "\"build\":%d,"
        "\"uptime_h\":%lu,"
        "\"uptime_m\":%lu,"
        "\"uptime_s\":%lu,"
        "\"heap_free\":%u,"
        "\"heap_total\":%u,"
        "\"heap_used_pct\":%.1f,"
        "\"flash_size\":%u,"
        "\"cpu_freq\":%u"
        "}",
        BUILD_FIRMWARE_VERSION,
        BUILD_NUMBER,
        uptime_hours,
        uptime_minutes % 60,
        uptime_seconds % 60,
        (unsigned int)heap_free,
        (unsigned int)heap_total,
        heap_usage_percent,
        (unsigned int)flash_size,
        (unsigned int)ESP.getCpuFreqMHz()
    );
    
    sysinfo_system_characteristic->setValue(buffer);
    sysinfo_system_characteristic->notify();
}

void BluetoothManager::update_performance_info() {
    if (!sysinfo_performance_characteristic) return;
    
    // Get performance metrics from the performance monitor
    char buffer[BLE_SYSINFO_MAX_PAYLOAD_BYTES];
    
    // For now, create a simple performance summary
    // In a full implementation, we'd extract actual performance data
    snprintf(buffer, sizeof(buffer),
        "{"
        "\"tasks_registered\":6,"
        "\"system_healthy\":true,"
        "\"load_cell_freq_hz\":50,"
        "\"grind_control_freq_hz\":50,"
        "\"ui_freq_hz\":10,"
        "\"bluetooth_freq_hz\":20,"
        "\"debug_freq_hz\":1"
        "}"
    );
    
    sysinfo_performance_characteristic->setValue(buffer);
    sysinfo_performance_characteristic->notify();
}

void BluetoothManager::update_hardware_info() {
    if (!sysinfo_hardware_characteristic) return;
    
    char buffer[BLE_SYSINFO_MAX_PAYLOAD_BYTES];
    
    snprintf(buffer, sizeof(buffer),
        "{"
        "\"load_cell_active\":true,"
        "\"motor_available\":true,"
        "\"display_active\":true,"
        "\"touch_active\":true,"
        "\"ble_enabled\":true,"
        "\"wifi_available\":false,"
        "\"flash_available\":true"
        "}"
    );
    
    sysinfo_hardware_characteristic->setValue(buffer);
    sysinfo_hardware_characteristic->notify();
}

void BluetoothManager::update_sessions_info() {
    if (!sysinfo_sessions_characteristic) return;
    
    char buffer[BLE_SYSINFO_MAX_PAYLOAD_BYTES];
    uint16_t session_count = data_stream.get_total_sessions();
    
    snprintf(buffer, sizeof(buffer),
        "{"
        "\"total_sessions\":%u,"
        "\"data_available\":%s,"
        "\"export_active\":%s,"
        "\"last_export_time\":0"
        "}",
        session_count,
        session_count > 0 ? "true" : "false",
        data_export_in_progress ? "true" : "false"
    );
    
    sysinfo_sessions_characteristic->setValue(buffer);
    sysinfo_sessions_characteristic->notify();
}
