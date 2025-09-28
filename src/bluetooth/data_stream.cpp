#include "data_stream.h"
#include "../logging/grind_logging.h"
#include "../config/constants.h"
#include <Arduino.h>
#include <LittleFS.h>

extern GrindLogger grind_logger;

DataStreamManager::DataStreamManager() 
    : current_session_id(0)
    , file_bytes_sent(0)
    , file_total_size(0)
    , file_stream_active(false) {
}

DataStreamManager::~DataStreamManager() {
    close_stream();
}

uint32_t DataStreamManager::get_total_sessions() const {
    return grind_logger.get_total_flash_sessions();
}

void DataStreamManager::close_stream() {
    if (file_stream_active) {
        BLE_LOG("DataStream: Closing file stream\n");
    }
    if (active_file) {
        active_file.close();
    }
    file_stream_active = false;
    current_session_id = 0;
    file_bytes_sent = 0;
    file_total_size = 0;
}

uint32_t DataStreamManager::get_session_list(uint32_t* session_ids, uint32_t max_sessions) {
    uint32_t total_sessions = grind_logger.count_sessions_in_flash();
    if (total_sessions == 0 || !session_ids) {
        return 0;
    }
    
    // Reuse the session list logic from export_sessions_binary_chunk
    uint32_t* session_list = (uint32_t*)heap_caps_malloc(total_sessions * sizeof(uint32_t), MALLOC_CAP_8BIT);
    if (!session_list) {
        BLE_LOG("ERROR: Failed to allocate session list memory\n");
        return 0;
    }
    
    uint32_t list_count = 0;
    
    // Try individual session files first (new approach)
    if (LittleFS.exists(GRIND_SESSIONS_DIR)) {
        File dir = LittleFS.open(GRIND_SESSIONS_DIR);
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            while (file && list_count < total_sessions) {
                String filename = file.name();
                bool is_session_file = (filename.startsWith("session_") || filename.indexOf("/session_") != -1)
                                       && filename.endsWith(".bin");
                if (is_session_file) {
                    // Extract session ID from filename
                    int start_pos = filename.indexOf('_') + 1;
                    int end_pos = filename.lastIndexOf('.');
                    if (start_pos > 0 && end_pos > start_pos) {
                        uint32_t session_id = filename.substring(start_pos, end_pos).toInt();
                        if (session_id > 0) {
                            session_list[list_count++] = session_id;
                        }
                    }
                }
                file = dir.openNextFile();
            }
            dir.close();
        }
    }
    
    // Sort session IDs for consistent order
    for (uint32_t i = 0; i < list_count - 1; i++) {
        for (uint32_t j = i + 1; j < list_count; j++) {
            if (session_list[i] > session_list[j]) {
                uint32_t temp = session_list[i];
                session_list[i] = session_list[j];
                session_list[j] = temp;
            }
        }
    }
    
    // Copy results to output array
    uint32_t copy_count = (list_count < max_sessions) ? list_count : max_sessions;
    for (uint32_t i = 0; i < copy_count; i++) {
        session_ids[i] = session_list[i];
    }
    
    heap_caps_free(session_list);
    BLE_LOG("DataStream: Found %lu session files\n", list_count);
    return copy_count;
}

bool DataStreamManager::initialize_file_stream(uint32_t session_id) {
    // Close any existing file stream
    close_stream();

    current_session_id = session_id;
    file_bytes_sent = 0;

    // Get file size to estimate total transfer
    char filename[64];
    snprintf(filename, sizeof(filename), SESSION_FILE_FORMAT, session_id);

    if (!LittleFS.exists(filename)) {
        BLE_LOG("ERROR: Session file %s does not exist\n", filename);
        return false;
    }

    active_file = LittleFS.open(filename, "r");
    if (!active_file) {
        BLE_LOG("ERROR: Failed to open session file %s\n", filename);
        return false;
    }

    file_total_size = active_file.size();
    BLE_LOG("DataStream: Initialized file stream for session %lu (%lu bytes)\n", session_id, file_total_size);
    file_stream_active = true;
    return true;
}

bool DataStreamManager::read_file_chunk(uint8_t* buffer, size_t buffer_size, size_t* actual_size) {
    if (!file_stream_active || !buffer || !actual_size) {
        return false;
    }

    if (!active_file) {
        BLE_LOG("ERROR: Active file handle missing for session %lu\n", current_session_id);
        file_stream_active = false;
        return false;
    }

    // Read next chunk at current file position
    size_t bytes_read = active_file.read(buffer, buffer_size);

    if (bytes_read > 0) {
        file_bytes_sent += bytes_read;
        *actual_size = bytes_read;

        // Check if file is complete
        if (file_bytes_sent >= file_total_size) {
            BLE_LOG("DataStream: Completed file stream for session %lu\n", current_session_id);
            active_file.close();
            file_stream_active = false;
        }

        return true;
    }

    // No more data
    BLE_LOG("DataStream: End of file stream for session %lu\n", current_session_id);
    active_file.close();
    file_stream_active = false;
    return false;
}

uint8_t DataStreamManager::get_progress_percent() const {
    if (!file_stream_active || file_total_size == 0) {
        return 0;
    }
    
    // Calculate progress percentage (0-100)
    uint32_t progress = (file_bytes_sent * 100) / file_total_size;
    
    // Ensure we don't exceed 100%
    return (progress > 100) ? 100 : static_cast<uint8_t>(progress);
}
