#pragma once

#include <Preferences.h>
#include <stddef.h>
#include <stdint.h>

#include "../config/constants.h"

struct BeanRecord {
    uint8_t id;
    bool valid;
    char name[33];
    char roaster[25];
    uint16_t bag_size_g;
    uint16_t mahlgrad_x2[USER_PROFILE_COUNT];
    uint32_t dose_used_x10;
    uint32_t purge_used_x10;
};

class BeanController {
public:
    static constexpr uint8_t kMaxBeans = 8;
    static constexpr uint8_t kDoubleProfileIndex = 1;
    static constexpr uint16_t kMinMahlgradX2 = 2;
    static constexpr uint16_t kMaxMahlgradX2 = 100;
    static constexpr uint16_t kDefaultMahlgradX2 = 50;
    static constexpr uint32_t kMaxUsageX10 = 999999;

    enum class Feedback {
        FINER = -1,
        OK = 0,
        COARSER = 1
    };

    void init();
    void load();
    bool save() const;

    uint8_t count() const { return bean_count_; }
    uint8_t capacity() const { return kMaxBeans; }
    uint8_t get_active_id() const { return active_bean_id_; }
    bool has_active_bean() const { return find_bean(active_bean_id_) != nullptr; }

    const BeanRecord* get_bean_at(uint8_t index) const;
    const BeanRecord* find_bean(uint8_t id) const;
    BeanRecord* find_bean(uint8_t id);
    const BeanRecord* get_active_bean() const { return find_bean(active_bean_id_); }

    bool create_bean(const char* name, const char* roaster, uint16_t bag_size_g,
                     float double_mahlgrad, uint8_t* created_id = nullptr);
    bool update_bean(uint8_t id, const char* name, const char* roaster,
                     uint16_t bag_size_g, float double_mahlgrad);
    bool delete_bean(uint8_t id);
    bool set_active_bean(uint8_t id);

    bool apply_feedback(uint8_t id, Feedback feedback);
    bool apply_feedback_to_active(Feedback feedback);
    bool add_dose_used_g(float grams);
    bool add_purge_used_g(float grams);

    static uint16_t mahlgrad_to_x2(float value);
    static float x2_to_mahlgrad(uint16_t value_x2);
    static void format_mahlgrad(char* out, size_t out_len, uint16_t value_x2);

private:
    struct BeanStoreSnapshot {
        uint32_t magic;
        uint8_t version;
        uint8_t active_bean_id;
        uint8_t next_id;
        uint8_t bean_count;
        BeanRecord beans[kMaxBeans];
    };

    static constexpr uint32_t kStoreMagic = 0x4245414Eu; // "BEAN"
    static constexpr uint8_t kStoreVersion = 1;
    static constexpr const char* kNamespace = "beans";
    static constexpr const char* kSnapshotKey = "snapshot";

    BeanRecord beans_[kMaxBeans] = {};
    uint8_t bean_count_ = 0;
    uint8_t active_bean_id_ = 0;
    uint8_t next_id_ = 1;

    int find_index(uint8_t id) const;
    uint8_t allocate_id();
    void clear();
    void normalize_after_load();
    bool persist_snapshot(const BeanStoreSnapshot& snapshot) const;
    void fill_snapshot(BeanStoreSnapshot& snapshot) const;
    static void copy_text(char* dest, size_t dest_len, const char* src);
    static uint32_t grams_to_x10(float grams);
};
