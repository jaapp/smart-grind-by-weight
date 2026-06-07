#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) {
        namespace_ = ns ? ns : "";
        read_only_ = readOnly;
        return true;
    }

    void end() {}

    size_t getBytesLength(const char* key) const {
        const auto it = storage().find(full_key(key));
        return it == storage().end() ? 0 : it->second.size();
    }

    size_t getBytes(const char* key, void* buf, size_t maxLen) const {
        const auto it = storage().find(full_key(key));
        if (it == storage().end() || !buf) {
            return 0;
        }
        const size_t n = it->second.size() < maxLen ? it->second.size() : maxLen;
        std::memcpy(buf, it->second.data(), n);
        return n;
    }

    size_t putBytes(const char* key, const void* value, size_t len) {
        if (read_only_ || !value) {
            return 0;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(value);
        storage()[full_key(key)] = std::vector<uint8_t>(bytes, bytes + len);
        return len;
    }

    bool remove(const char* key) {
        if (read_only_) {
            return false;
        }
        return storage().erase(full_key(key)) > 0;
    }

private:
    std::string namespace_;
    bool read_only_ = false;

    static std::unordered_map<std::string, std::vector<uint8_t>>& storage() {
        static std::unordered_map<std::string, std::vector<uint8_t>> data;
        return data;
    }

    std::string full_key(const char* key) const {
        return namespace_ + ":" + (key ? key : "");
    }
};
