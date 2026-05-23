#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <cstdint>

// Image upload command bytes (sent via data control characteristic)
// Uses 0x30+ range to avoid conflicts with data export commands (0x11-0x15)
enum BLEImageCommand {
    BLE_IMG_CMD_START  = 0x30,  // [0x30][file_size:4 LE] - begin upload
    BLE_IMG_CMD_DATA   = 0x31,  // Implicit - raw data sent to data transfer char
    BLE_IMG_CMD_END    = 0x32,  // [0x32] - finalize upload
    BLE_IMG_CMD_ABORT  = 0x33,  // [0x33] - cancel upload
    BLE_IMG_CMD_DELETE = 0x34   // [0x34] - delete existing image
};

// Image upload status bytes (sent via data status characteristic notifications)
// Uses 0x30+ range to avoid conflicts with data export status (0x20-0x23)
enum BLEImageStatus {
    BLE_IMG_STATUS_IDLE      = 0x30,
    BLE_IMG_STATUS_READY     = 0x31,
    BLE_IMG_STATUS_RECEIVING = 0x32,
    BLE_IMG_STATUS_SUCCESS   = 0x33,
    BLE_IMG_STATUS_ERROR     = 0x34,
    BLE_IMG_STATUS_HAS_IMAGE = 0x35  // Sent on connect if image exists
};

class OTAHandler;  // Forward declaration for OTA active check

/**
 * ImageUploadHandler - Receives screensaver image data over BLE
 * and writes it to LittleFS as a raw RGB565 file.
 *
 * Protocol mirrors the OTA pattern: START -> DATA chunks -> END
 * Image is written to a temp file first, then renamed on success.
 */
class ImageUploadHandler {
public:
    ImageUploadHandler();

    void init(OTAHandler* ota);

    /**
     * Start a new image upload.
     * @param file_size Expected size in bytes (must be BLE_IMAGE_EXPECTED_SIZE)
     * @return true if upload started successfully
     */
    bool start_upload(uint32_t file_size);

    /**
     * Process a chunk of image data.
     * @param data Pointer to chunk data
     * @param size Size of chunk in bytes
     * @return true if chunk written successfully
     */
    bool process_chunk(const uint8_t* data, size_t size);

    /**
     * Finalize the upload: verify size and rename temp file.
     * @return true if finalization successful
     */
    bool complete_upload();

    /**
     * Abort an in-progress upload and clean up temp file.
     */
    void abort_upload();

    /**
     * Delete the screensaver image file.
     * @return true if file deleted or didn't exist
     */
    bool delete_image();

    /**
     * Check if a screensaver image exists on LittleFS.
     */
    bool has_image() const;

    uint8_t get_status() const { return current_status_; }
    float get_progress() const;
    bool is_upload_active() const { return upload_in_progress_; }

private:
    OTAHandler* ota_handler_;
    File temp_file_;
    uint32_t expected_size_;
    uint32_t received_size_;
    bool upload_in_progress_;
    uint8_t current_status_;

    void cleanup_temp_file();
};
