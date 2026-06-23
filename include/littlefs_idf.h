#pragma once
// Drop-in replacement for Arduino's LittleFS.h using the ESP-IDF esp_vfs_littlefs driver.
//
// The filesystem is mounted at BASE_PATH ("/fs").  All user-visible paths such as
// "/sessions/session_1.bin" are transparently prefixed so they become
// "/fs/sessions/session_1.bin" in the VFS.
//
// This header also pulls in arduino_compat.h so that callers can use the Arduino
// String type when working with File::name() return values — matching the existing
// call-sites in grind_logging.cpp and data_stream.cpp.

#include "arduino_compat.h"

#include <esp_vfs.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>

static const char* LFS_TAG = "LittleFS";

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

/**
 * Wraps a FILE* (regular file) or a DIR* (directory) providing the same
 * interface as the Arduino LittleFS File object.
 */
class File {
    FILE*          file_handle;
    DIR*           dir_handle;
    struct dirent* current_dirent;
    char           file_path[256];   // Full VFS path  (e.g. "/fs/sessions/s1.bin")
    bool           is_dir;
    bool           valid;

    // Helpers ----------------------------------------------------------------
    static const char* basename_of(const char* path) {
        const char* slash = strrchr(path, '/');
        return slash ? slash + 1 : path;
    }

public:
    // Default — represents an invalid / closed file.
    File()
        : file_handle(nullptr)
        , dir_handle(nullptr)
        , current_dirent(nullptr)
        , is_dir(false)
        , valid(false)
    {
        file_path[0] = '\0';
    }

    // Open a regular file.
    File(const char* path, const char* mode)
        : file_handle(nullptr)
        , dir_handle(nullptr)
        , current_dirent(nullptr)
        , is_dir(false)
        , valid(false)
    {
        file_path[0] = '\0';
        if (!path || !mode) return;
        strncpy(file_path, path, sizeof(file_path) - 1);
        file_handle = fopen(path, mode);
        valid = (file_handle != nullptr);
        if (!valid) {
            ESP_LOGD(LFS_TAG, "fopen('%s', '%s') failed", path, mode);
        }
    }

    // Open a directory for iteration.
    File(const char* path, bool /*as_directory*/)
        : file_handle(nullptr)
        , dir_handle(nullptr)
        , current_dirent(nullptr)
        , is_dir(true)
        , valid(false)
    {
        file_path[0] = '\0';
        if (!path) return;
        strncpy(file_path, path, sizeof(file_path) - 1);
        dir_handle = opendir(path);
        valid = (dir_handle != nullptr);
        if (!valid) {
            ESP_LOGD(LFS_TAG, "opendir('%s') failed", path);
        }
    }

    ~File() {
        // Do NOT auto-close here — Arduino's File is value-copied freely;
        // callers are expected to call close() explicitly when done.
        // Only release resources if we still hold a valid handle to avoid
        // double-close on copy destruction.
    }

    // -----------------------------------------------------------------------
    // File I/O
    // -----------------------------------------------------------------------

    size_t read(uint8_t* buf, size_t size) {
        if (!valid || !file_handle || !buf) return 0;
        return fread(buf, 1, size, file_handle);
    }

    /** Read a single byte.  Returns -1 on EOF / error. */
    int read() {
        if (!valid || !file_handle) return -1;
        int c = fgetc(file_handle);
        return (c == EOF) ? -1 : c;
    }

    /** Arduino Print::println() — writes a line to the file with a newline. */
    size_t println(const char* s = "") {
        if (!valid || !file_handle) return 0;
        int n = fprintf(file_handle, "%s\n", s ? s : "");
        return n > 0 ? (size_t)n : 0;
    }

    /** Arduino Print::printf() — formatted write to the file. */
    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (!valid || !file_handle) return 0;
        va_list args;
        va_start(args, fmt);
        int n = vfprintf(file_handle, fmt, args);
        va_end(args);
        return n > 0 ? (size_t)n : 0;
    }

    /** Arduino Stream::readBytes() — reads up to `len` bytes into `buf`. */
    size_t readBytes(char* buf, size_t len) {
        if (!valid || !file_handle || !buf) return 0;
        return fread(buf, 1, len, file_handle);
    }
    size_t readBytes(uint8_t* buf, size_t len) {
        return readBytes(reinterpret_cast<char*>(buf), len);
    }

    size_t write(const uint8_t* buf, size_t size) {
        if (!valid || !file_handle || !buf) return 0;
        return fwrite(buf, 1, size, file_handle);
    }

    void flush() {
        if (valid && file_handle) {
            fflush(file_handle);
        }
    }

    bool seek(uint32_t pos) {
        if (!valid || !file_handle) return false;
        return fseek(file_handle, static_cast<long>(pos), SEEK_SET) == 0;
    }

    uint32_t position() const {
        if (!valid || !file_handle) return 0;
        long pos = ftell(file_handle);
        return (pos < 0) ? 0u : static_cast<uint32_t>(pos);
    }

    size_t size() const {
        if (!valid || !file_handle) return 0;
        long cur = ftell(file_handle);
        if (cur < 0) return 0;
        if (fseek(file_handle, 0, SEEK_END) != 0) return 0;
        long end = ftell(file_handle);
        fseek(file_handle, cur, SEEK_SET);   // Restore position
        return (end < 0) ? 0u : static_cast<size_t>(end);
    }

    /**
     * Returns the number of bytes available to read from the current position.
     * Matches Arduino's Stream::available() semantics used in grind_logging.cpp.
     */
    size_t available() const {
        if (!valid || !file_handle) return 0;
        long cur = ftell(file_handle);
        if (cur < 0) return 0;
        if (fseek(file_handle, 0, SEEK_END) != 0) return 0;
        long end = ftell(file_handle);
        fseek(file_handle, cur, SEEK_SET);   // Restore position
        if (end < 0 || end < cur) return 0;
        return static_cast<size_t>(end - cur);
    }

    void close() {
        if (file_handle) {
            fclose(file_handle);
            file_handle = nullptr;
        }
        if (dir_handle) {
            closedir(dir_handle);
            dir_handle = nullptr;
            current_dirent = nullptr;
        }
        valid = false;
    }

    explicit operator bool() const { return valid; }

    // -----------------------------------------------------------------------
    // File metadata
    // -----------------------------------------------------------------------

    /**
     * Returns just the filename component (no directory path).
     * The return type is String (from arduino_compat.h) so that call-sites such
     * as:   String filename = file.name();  compile without changes.
     */
    String name() const {
        return String(basename_of(file_path));
    }

    /**
     * Returns the full VFS path.
     */
    const char* path() const { return file_path; }

    bool isDirectory() const { return is_dir; }

    // -----------------------------------------------------------------------
    // Directory iteration
    // -----------------------------------------------------------------------

    /**
     * Advance to the next entry in the directory, skipping "." and "..".
     * Returns an invalid File when the directory is exhausted.
     */
    File openNextFile() {
        if (!valid || !dir_handle) return File();

        struct dirent* entry = nullptr;
        do {
            entry = readdir(dir_handle);
        } while (entry && (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0));

        if (!entry) return File();

        // Build child path: file_path + "/" + entry->d_name
        char child_path[256];
        size_t dir_len = strlen(file_path);
        // Remove trailing slash from directory path if present.
        bool has_slash = (dir_len > 0 && file_path[dir_len - 1] == '/');
        snprintf(child_path, sizeof(child_path), "%s%s%s",
                 file_path,
                 has_slash ? "" : "/",
                 entry->d_name);

        // Determine whether the entry is itself a directory.
        bool child_is_dir = (entry->d_type == DT_DIR);
        if (entry->d_type == DT_UNKNOWN) {
            // Some VFS drivers don't set d_type — fall back to stat.
            struct stat st;
            if (stat(child_path, &st) == 0) {
                child_is_dir = S_ISDIR(st.st_mode);
            }
        }

        if (child_is_dir) {
            return File(child_path, true);
        } else {
            return File(child_path, "r");
        }
    }
};


// ---------------------------------------------------------------------------
// LittleFSClass
// ---------------------------------------------------------------------------

class LittleFSClass {
    bool mounted;

public:
    static constexpr const char* BASE_PATH = "/fs";

    LittleFSClass() : mounted(false) {}

    /**
     * Mount the LittleFS partition.
     *
     * @param formatOnFail   Automatically format the partition if it cannot be mounted.
     * @param basePath       VFS mount point (default: "/fs").
     * @param maxOpenFiles   Maximum simultaneous open files.
     * @param partitionLabel Partition label in partitions.csv (default: "spiffs" — the
     *                       physical partition is labelled "spiffs" even though it uses
     *                       the LittleFS format).
     */
    bool begin(bool formatOnFail = false,
               const char* basePath = "/fs",
               uint8_t maxOpenFiles = 10,
               const char* partitionLabel = "spiffs")
    {
        if (mounted) return true;

        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path              = basePath;
        conf.partition_label        = partitionLabel;
        conf.format_if_mount_failed = formatOnFail;
        (void)maxOpenFiles; // max_files was removed in IDF 5.x

        esp_err_t ret = esp_vfs_littlefs_register(&conf);

        if (ret == ESP_OK) {
            mounted = true;
            size_t total = 0, used = 0;
            esp_littlefs_info(partitionLabel, &total, &used);
            ESP_LOGI(LFS_TAG, "Mounted '%s' at '%s': %u KB total, %u KB used",
                     partitionLabel, basePath,
                     (unsigned)(total / 1024), (unsigned)(used / 1024));
            return true;
        }

        if (ret == ESP_ERR_INVALID_STATE) {
            // Already registered by another component — treat as success.
            mounted = true;
            return true;
        }

        ESP_LOGE(LFS_TAG, "esp_vfs_littlefs_register failed: %d", ret);
        return false;
    }

    void end() {
        if (!mounted) return;
        esp_vfs_littlefs_unregister("spiffs");
        mounted = false;
    }

    // -----------------------------------------------------------------------
    // Path helpers
    // -----------------------------------------------------------------------

    /**
     * Prepend BASE_PATH to a user path.
     * "/sessions/foo.bin"  → "/fs/sessions/foo.bin"
     * If path already starts with BASE_PATH it is returned unchanged (idempotent).
     */
    static std::string full_path(const char* path) {
        if (!path) return std::string(BASE_PATH);
        // Avoid double-prepending.
        if (strncmp(path, BASE_PATH, strlen(BASE_PATH)) == 0) {
            return std::string(path);
        }
        std::string result(BASE_PATH);
        if (path[0] != '/') result += '/';
        result += path;
        return result;
    }

    // -----------------------------------------------------------------------
    // Filesystem operations
    // -----------------------------------------------------------------------

    /**
     * Open a file or directory.
     * Paths are transparently prefixed with BASE_PATH.
     *
     * @param path  User path, e.g. "/sessions/session_1.bin".
     * @param mode  fopen mode string ("r", "w", "a", etc.).  Pass nullptr or
     *              omit to auto-detect: if the path resolves to a directory the
     *              handle is opened for iteration; otherwise defaults to "r".
     */
    File open(const char* path, const char* mode = nullptr) {
        std::string vfs_path = full_path(path);

        // Determine whether the path is a directory.
        struct stat st;
        bool path_is_dir = false;
        if (stat(vfs_path.c_str(), &st) == 0) {
            path_is_dir = S_ISDIR(st.st_mode);
        }

        if (path_is_dir) {
            return File(vfs_path.c_str(), true);
        }

        const char* open_mode = (mode != nullptr) ? mode : "r";
        return File(vfs_path.c_str(), open_mode);
    }

    bool exists(const char* path) {
        std::string vfs_path = full_path(path);
        struct stat st;
        return stat(vfs_path.c_str(), &st) == 0;
    }

    bool remove(const char* path) {
        std::string vfs_path = full_path(path);
        int ret = ::remove(vfs_path.c_str());
        if (ret != 0) {
            ESP_LOGW(LFS_TAG, "remove('%s') failed: %d", vfs_path.c_str(), errno);
        }
        return ret == 0;
    }

    /** Overload accepting Arduino String for convenience (used in grind_logging.cpp). */
    bool remove(const String& path) {
        return remove(path.c_str());
    }

    bool rename(const char* pathFrom, const char* pathTo) {
        std::string vfs_from = full_path(pathFrom);
        std::string vfs_to   = full_path(pathTo);
        return ::rename(vfs_from.c_str(), vfs_to.c_str()) == 0;
    }

    bool mkdir(const char* path) {
        std::string vfs_path = full_path(path);
        // Create all intermediate components.
        int ret = ::mkdir(vfs_path.c_str(), 0755);
        if (ret != 0 && errno == EEXIST) return true;   // Already exists — OK.
        if (ret != 0) {
            ESP_LOGW(LFS_TAG, "mkdir('%s') failed: %d (errno %d)", vfs_path.c_str(), ret, errno);
        }
        return ret == 0;
    }

    bool rmdir(const char* path) {
        std::string vfs_path = full_path(path);
        return ::rmdir(vfs_path.c_str()) == 0;
    }
};

// Global singleton instance — matches Arduino's `LittleFS` symbol.
extern LittleFSClass LittleFS;
