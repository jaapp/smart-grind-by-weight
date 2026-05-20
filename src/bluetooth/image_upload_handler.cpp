#include "image_upload_handler.h"
#include "ota_handler.h"
#include "../config/constants.h"
#include "../config/logging.h"

ImageUploadHandler::ImageUploadHandler()
    : ota_handler_(nullptr)
    , expected_size_(0)
    , received_size_(0)
    , upload_in_progress_(false)
    , current_status_(BLE_IMG_STATUS_IDLE) {
}

void ImageUploadHandler::init(OTAHandler* ota) {
    ota_handler_ = ota;
    LOG_BLE("Image Upload: Handler initialized\n");
}

bool ImageUploadHandler::start_upload(uint32_t file_size) {
    if (upload_in_progress_) {
        LOG_BLE("Image Upload: Upload already in progress\n");
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    // Reject if OTA is active
    if (ota_handler_ && ota_handler_->is_ota_active()) {
        LOG_BLE("Image Upload: Rejected - OTA update in progress\n");
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    // Validate expected file size
    if (file_size != BLE_IMAGE_EXPECTED_SIZE) {
        LOG_BLE("Image Upload: Invalid size %lu (expected %d)\n",
                (unsigned long)file_size, BLE_IMAGE_EXPECTED_SIZE);
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    // Clean up any leftover temp file
    cleanup_temp_file();

    // Open temp file for writing
    temp_file_ = LittleFS.open(BLE_IMAGE_TEMP_FILENAME, "w");
    if (!temp_file_) {
        LOG_BLE("Image Upload: Failed to open temp file for writing\n");
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    expected_size_ = file_size;
    received_size_ = 0;
    upload_in_progress_ = true;
    current_status_ = BLE_IMG_STATUS_RECEIVING;

    LOG_BLE("Image Upload: Started (%lu bytes expected)\n", (unsigned long)expected_size_);
    return true;
}

bool ImageUploadHandler::process_chunk(const uint8_t* data, size_t size) {
    if (!upload_in_progress_ || !temp_file_) {
        return false;
    }

    // Check for overflow
    if (received_size_ + size > expected_size_) {
        LOG_BLE("Image Upload: Data overflow (%lu + %u > %lu)\n",
                (unsigned long)received_size_, (unsigned)size, (unsigned long)expected_size_);
        current_status_ = BLE_IMG_STATUS_ERROR;
        abort_upload();
        return false;
    }

    size_t written = temp_file_.write(data, size);
    if (written != size) {
        LOG_BLE("Image Upload: Write failed (wrote %u of %u bytes)\n",
                (unsigned)written, (unsigned)size);
        current_status_ = BLE_IMG_STATUS_ERROR;
        abort_upload();
        return false;
    }

    received_size_ += size;

    // Progress logging every 32KB
    if (received_size_ % 32768 == 0 || received_size_ == expected_size_) {
        LOG_BLE("Image Upload: %lu / %lu bytes (%.0f%%)\n",
                (unsigned long)received_size_, (unsigned long)expected_size_, get_progress());
    }

    return true;
}

bool ImageUploadHandler::complete_upload() {
    if (!upload_in_progress_) {
        LOG_BLE("Image Upload: No upload in progress\n");
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    // Close the temp file
    temp_file_.close();

    // Verify received size
    if (received_size_ != expected_size_) {
        LOG_BLE("Image Upload: Size mismatch - expected %lu, got %lu\n",
                (unsigned long)expected_size_, (unsigned long)received_size_);
        cleanup_temp_file();
        upload_in_progress_ = false;
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    // Remove existing image if present
    if (LittleFS.exists(BLE_IMAGE_FILENAME)) {
        LittleFS.remove(BLE_IMAGE_FILENAME);
    }

    // Rename temp file to final filename
    if (!LittleFS.rename(BLE_IMAGE_TEMP_FILENAME, BLE_IMAGE_FILENAME)) {
        LOG_BLE("Image Upload: Failed to rename temp file\n");
        cleanup_temp_file();
        upload_in_progress_ = false;
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    upload_in_progress_ = false;

    // Verify file exists after rename
    if (!LittleFS.exists(BLE_IMAGE_FILENAME)) {
        LOG_BLE("Image Upload: File missing after rename\n");
        current_status_ = BLE_IMG_STATUS_ERROR;
        return false;
    }

    current_status_ = BLE_IMG_STATUS_SUCCESS;
    LOG_BLE("Image Upload: Complete (%lu bytes)\n", (unsigned long)received_size_);
    return true;
}

void ImageUploadHandler::abort_upload() {
    if (upload_in_progress_) {
        LOG_BLE("Image Upload: Aborting\n");
        if (temp_file_) {
            temp_file_.close();
        }
        cleanup_temp_file();
        upload_in_progress_ = false;
        received_size_ = 0;
        expected_size_ = 0;
        current_status_ = BLE_IMG_STATUS_IDLE;
    }
}

bool ImageUploadHandler::delete_image() {
    if (upload_in_progress_) {
        abort_upload();
    }

    if (LittleFS.exists(BLE_IMAGE_FILENAME)) {
        if (LittleFS.remove(BLE_IMAGE_FILENAME)) {
            LOG_BLE("Image Upload: Screensaver image deleted\n");
            current_status_ = BLE_IMG_STATUS_IDLE;
            return true;
        } else {
            LOG_BLE("Image Upload: Failed to delete image\n");
            current_status_ = BLE_IMG_STATUS_ERROR;
            return false;
        }
    }

    LOG_BLE("Image Upload: No image to delete\n");
    current_status_ = BLE_IMG_STATUS_IDLE;
    return true;
}

bool ImageUploadHandler::has_image() const {
    return LittleFS.exists(BLE_IMAGE_FILENAME);
}

float ImageUploadHandler::get_progress() const {
    if (expected_size_ == 0) return 0.0f;
    return 100.0f * received_size_ / expected_size_;
}

void ImageUploadHandler::cleanup_temp_file() {
    if (LittleFS.exists(BLE_IMAGE_TEMP_FILENAME)) {
        LittleFS.remove(BLE_IMAGE_TEMP_FILENAME);
    }
}
