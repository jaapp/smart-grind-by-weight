#pragma once

#include <cstdint>
#include <cstddef>
#include "../../include/littlefs_idf.h"

/**
 * DataStreamManager - Handles streaming data from the grind logger
 * 
 * This class isolates file I/O and progress tracking from BLE communication,
 * providing a clean interface for reading data in chunks.
 */
class DataStreamManager {
private:
    // Per-file streaming state
    uint32_t current_session_id;
    uint32_t file_bytes_sent;
    uint32_t file_total_size;
    bool file_stream_active;
    File active_file;                      // Persistent handle for efficient reads
    
public:
    DataStreamManager();
    ~DataStreamManager();
    
    /**
     * Get total sessions available for export
     */
    uint32_t get_total_sessions() const;
    
    /**
     * Close and cleanup stream
     */
    void close_stream();
    
    /**
     * Get list of available session files
     * @param session_ids Output array to store session IDs
     * @param max_sessions Maximum number of sessions to return
     * @return Number of sessions found
     */
    uint32_t get_session_list(uint32_t* session_ids, uint32_t max_sessions);
    
    /**
     * Initialize stream for a specific session file
     * @param session_id The session ID to stream
     * @return true if file exists and stream is ready, false otherwise
     */
    bool initialize_file_stream(uint32_t session_id);
    
    /**
     * Read chunk of data from current file stream
     * @param buffer Output buffer for data
     * @param buffer_size Size of output buffer  
     * @param actual_size Actual bytes read (output parameter)
     * @return true if data was read, false if file complete or error
     */
    bool read_file_chunk(uint8_t* buffer, size_t buffer_size, size_t* actual_size);
    
    /**
     * Get current file transfer progress as percentage (0-100)
     * @return Progress percentage, or 0 if no stream is active
     */
    uint8_t get_progress_percent() const;
};
