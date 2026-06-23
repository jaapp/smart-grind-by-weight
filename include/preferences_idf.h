#pragma once
// Drop-in replacement for Arduino's Preferences.h using the ESP-IDF NVS API directly.
// Provides the same interface as the Arduino wrapper so existing callers require no changes.

#include "nvs_flash.h"
#include "nvs.h"
#include <string>
#include <cstring>
#include <esp_log.h>

static const char* PREFS_TAG = "Preferences";

class Preferences {
    nvs_handle_t handle;
    bool read_only;
    bool opened;

    // Initialise the NVS flash partition exactly once across all Preferences instances.
    static bool nvs_flash_initialised() {
        static bool initialised = false;
        if (!initialised) {
            esp_err_t ret = nvs_flash_init();
            if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                // NVS partition was truncated or schema changed — erase and reinitialise.
                ESP_LOGW(PREFS_TAG, "NVS needs erasing (err=%d), erasing now", ret);
                nvs_flash_erase();
                ret = nvs_flash_init();
            }
            if (ret != ESP_OK) {
                ESP_LOGE(PREFS_TAG, "nvs_flash_init failed: %d", ret);
                return false;
            }
            initialised = true;
        }
        return true;
    }

public:
    Preferences() : handle(0), read_only(false), opened(false) {}

    ~Preferences() {
        if (opened) {
            end();
        }
    }

    /**
     * Open an NVS namespace.
     * @param name            Namespace name (max 15 chars).
     * @param readOnly        When true opens in NVS_READONLY mode.
     * @param partition_label Custom partition label, or nullptr for the default "nvs" partition.
     * @return true on success.
     */
    bool begin(const char* name, bool readOnly = false, const char* partition_label = nullptr) {
        if (!nvs_flash_initialised()) {
            return false;
        }

        this->read_only = readOnly;
        nvs_open_mode_t mode = readOnly ? NVS_READONLY : NVS_READWRITE;

        esp_err_t err;
        if (partition_label != nullptr) {
            // Initialise the named partition first (safe to call multiple times).
            nvs_flash_init_partition(partition_label);
            err = nvs_open_from_partition(partition_label, name, mode, &handle);
        } else {
            err = nvs_open(name, mode, &handle);
        }

        if (err != ESP_OK) {
            // NVS_READONLY open on a namespace that doesn't exist yet returns NOT_FOUND.
            // That is not an error for read-only usage — callers will just get defaults.
            if (!(readOnly && err == ESP_ERR_NVS_NOT_FOUND)) {
                ESP_LOGE(PREFS_TAG, "nvs_open('%s') failed: %d", name, err);
            }
            opened = false;
            return false;
        }

        opened = true;
        return true;
    }

    /** Commit pending writes and close the NVS handle. */
    void end() {
        if (!opened) return;
        if (!read_only) {
            esp_err_t err = nvs_commit(handle);
            if (err != ESP_OK) {
                ESP_LOGW(PREFS_TAG, "nvs_commit failed: %d", err);
            }
        }
        nvs_close(handle);
        opened = false;
    }

    // -------------------------------------------------------------------------
    // bool
    // -------------------------------------------------------------------------
    bool putBool(const char* key, bool value) {
        if (!opened || read_only) return false;
        return nvs_set_u8(handle, key, value ? 1u : 0u) == ESP_OK;
    }

    bool getBool(const char* key, bool defaultValue = false) {
        if (!opened) return defaultValue;
        uint8_t val = 0;
        esp_err_t err = nvs_get_u8(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getBool('%s') err: %d", key, err);
            return defaultValue;
        }
        return val != 0;
    }

    // -------------------------------------------------------------------------
    // int32_t
    // -------------------------------------------------------------------------
    bool putInt(const char* key, int32_t value) {
        if (!opened || read_only) return false;
        return nvs_set_i32(handle, key, value) == ESP_OK;
    }

    int32_t getInt(const char* key, int32_t defaultValue = 0) {
        if (!opened) return defaultValue;
        int32_t val = 0;
        esp_err_t err = nvs_get_i32(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getInt('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // uint32_t
    // -------------------------------------------------------------------------
    bool putUInt(const char* key, uint32_t value) {
        if (!opened || read_only) return false;
        return nvs_set_u32(handle, key, value) == ESP_OK;
    }

    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
        if (!opened) return defaultValue;
        uint32_t val = 0;
        esp_err_t err = nvs_get_u32(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getUInt('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // uint8_t / int8_t (Arduino getUChar / getChar)
    // -------------------------------------------------------------------------
    bool putUChar(const char* key, uint8_t value) {
        if (!opened || read_only) return false;
        return nvs_set_u8(handle, key, value) == ESP_OK;
    }

    uint8_t getUChar(const char* key, uint8_t defaultValue = 0) {
        if (!opened) return defaultValue;
        uint8_t val = 0;
        esp_err_t err = nvs_get_u8(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getUChar('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    bool putChar(const char* key, int8_t value) {
        if (!opened || read_only) return false;
        return nvs_set_i8(handle, key, value) == ESP_OK;
    }

    int8_t getChar(const char* key, int8_t defaultValue = 0) {
        if (!opened) return defaultValue;
        int8_t val = 0;
        esp_err_t err = nvs_get_i8(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getChar('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // uint16_t / int16_t (Arduino getUShort / getShort)
    // -------------------------------------------------------------------------
    bool putUShort(const char* key, uint16_t value) {
        if (!opened || read_only) return false;
        return nvs_set_u16(handle, key, value) == ESP_OK;
    }

    uint16_t getUShort(const char* key, uint16_t defaultValue = 0) {
        if (!opened) return defaultValue;
        uint16_t val = 0;
        esp_err_t err = nvs_get_u16(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getUShort('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    bool putShort(const char* key, int16_t value) {
        if (!opened || read_only) return false;
        return nvs_set_i16(handle, key, value) == ESP_OK;
    }

    int16_t getShort(const char* key, int16_t defaultValue = 0) {
        if (!opened) return defaultValue;
        int16_t val = 0;
        esp_err_t err = nvs_get_i16(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getShort('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // int64_t / uint64_t (Arduino getLong64 / getULong64)
    // -------------------------------------------------------------------------
    bool putLong64(const char* key, int64_t value) {
        if (!opened || read_only) return false;
        return nvs_set_i64(handle, key, value) == ESP_OK;
    }

    int64_t getLong64(const char* key, int64_t defaultValue = 0) {
        if (!opened) return defaultValue;
        int64_t val = 0;
        esp_err_t err = nvs_get_i64(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getLong64('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    bool putULong64(const char* key, uint64_t value) {
        if (!opened || read_only) return false;
        return nvs_set_u64(handle, key, value) == ESP_OK;
    }

    uint64_t getULong64(const char* key, uint64_t defaultValue = 0) {
        if (!opened) return defaultValue;
        uint64_t val = 0;
        esp_err_t err = nvs_get_u64(handle, key, &val);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getULong64('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // Blob — byte-array read/write and size query
    // -------------------------------------------------------------------------
    bool putBytes(const char* key, const void* value, size_t len) {
        if (!opened || read_only) return false;
        return nvs_set_blob(handle, key, value, len) == ESP_OK;
    }

    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        if (!opened) return 0;
        size_t len = maxLen;
        esp_err_t err = nvs_get_blob(handle, key, buf, &len);
        if (err != ESP_OK) return 0;
        return len;
    }

    /** Return the stored byte count for a blob key (0 if not found or wrong type). */
    size_t getBytesLength(const char* key) {
        if (!opened) return 0;
        size_t required_size = 0;
        esp_err_t err = nvs_get_blob(handle, key, nullptr, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) return 0;
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getBytesLength('%s') err: %d", key, err);
            return 0;
        }
        return required_size;
    }

    // -------------------------------------------------------------------------
    // long / ulong aliases (Arduino compat — same width as int/uint on ESP32)
    // -------------------------------------------------------------------------
    bool putLong(const char* key, int32_t value)        { return putInt(key, value); }
    int32_t getLong(const char* key, int32_t defaultValue = 0) { return getInt(key, defaultValue); }

    bool putULong(const char* key, uint32_t value)       { return putUInt(key, value); }
    uint32_t getULong(const char* key, uint32_t defaultValue = 0) { return getUInt(key, defaultValue); }

    // -------------------------------------------------------------------------
    // float  (stored as 4-byte blob — NVS has no native float type)
    // -------------------------------------------------------------------------
    bool putFloat(const char* key, float value) {
        if (!opened || read_only) return false;
        return nvs_set_blob(handle, key, &value, sizeof(value)) == ESP_OK;
    }

    float getFloat(const char* key, float defaultValue = 0.0f) {
        if (!opened) return defaultValue;
        float val = defaultValue;
        size_t len = sizeof(val);
        esp_err_t err = nvs_get_blob(handle, key, &val, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK || len != sizeof(val)) {
            ESP_LOGW(PREFS_TAG, "getFloat('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // double  (stored as 8-byte blob)
    // -------------------------------------------------------------------------
    bool putDouble(const char* key, double value) {
        if (!opened || read_only) return false;
        return nvs_set_blob(handle, key, &value, sizeof(value)) == ESP_OK;
    }

    double getDouble(const char* key, double defaultValue = 0.0) {
        if (!opened) return defaultValue;
        double val = defaultValue;
        size_t len = sizeof(val);
        esp_err_t err = nvs_get_blob(handle, key, &val, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) return defaultValue;
        if (err != ESP_OK || len != sizeof(val)) {
            ESP_LOGW(PREFS_TAG, "getDouble('%s') err: %d", key, err);
            return defaultValue;
        }
        return val;
    }

    // -------------------------------------------------------------------------
    // String  (null-terminated, stored as NVS string)
    // -------------------------------------------------------------------------
    bool putString(const char* key, const char* value) {
        if (!opened || read_only) return false;
        return nvs_set_str(handle, key, value) == ESP_OK;
    }

    bool putString(const char* key, const std::string& value) {
        return putString(key, value.c_str());
    }

    std::string getString(const char* key, const char* defaultValue = "") {
        if (!opened) return std::string(defaultValue);
        // First call: query the required buffer size.
        size_t required_size = 0;
        esp_err_t err = nvs_get_str(handle, key, nullptr, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) return std::string(defaultValue);
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getString('%s') size query err: %d", key, err);
            return std::string(defaultValue);
        }
        std::string result(required_size, '\0');
        err = nvs_get_str(handle, key, &result[0], &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(PREFS_TAG, "getString('%s') read err: %d", key, err);
            return std::string(defaultValue);
        }
        // nvs_get_str includes the null terminator in required_size; trim it.
        if (!result.empty() && result.back() == '\0') {
            result.pop_back();
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Key management
    // -------------------------------------------------------------------------
    bool remove(const char* key) {
        if (!opened || read_only) return false;
        esp_err_t err = nvs_erase_key(handle, key);
        return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
    }

    /** Erase every key in the current namespace. */
    void clear() {
        if (!opened || read_only) return;
        nvs_erase_all(handle);
        nvs_commit(handle);
    }

    /** Return true if the key exists in the current namespace. */
    bool isKey(const char* key) {
        if (!opened) return false;
        // Attempt a type-agnostic size query via blob; any non-NOT_FOUND result means the key exists.
        size_t dummy_size = 0;
        esp_err_t err = nvs_get_blob(handle, key, nullptr, &dummy_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) return false;
        // Key might be a non-blob type — if NVS reports a type error the key still exists.
        return true;
    }
};
