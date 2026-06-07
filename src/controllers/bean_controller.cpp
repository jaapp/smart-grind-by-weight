#include "bean_controller.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

constexpr const char* BeanController::kNamespace;
constexpr const char* BeanController::kMetaKey;
constexpr const char* BeanController::kSnapshotKey;

void BeanController::init() {
    load();
}

void BeanController::clear() {
    std::memset(beans_, 0, sizeof(beans_));
    bean_count_ = 0;
    active_bean_id_ = 0;
    next_id_ = 1;
}

void BeanController::load() {
    clear();

    if (load_current_store()) {
        return;
    }

    if (load_legacy_snapshot() && save()) {
        Preferences prefs;
        if (prefs.begin(kNamespace, false)) {
            prefs.remove(kSnapshotKey);
            prefs.end();
        }
    }
}

bool BeanController::load_current_store() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return true;
    }

    const size_t length = prefs.getBytesLength(kMetaKey);
    if (length == 0) {
        prefs.end();
        return false;
    }

    bool needs_repair = false;
    if (length == sizeof(BeanStoreMeta)) {
        BeanStoreMeta meta = {};
        if (prefs.getBytes(kMetaKey, &meta, sizeof(meta)) == sizeof(meta) &&
            meta.magic == kStoreMagic &&
            meta.version == kStoreVersion &&
            meta.record_size == sizeof(BeanRecord)) {
            active_bean_id_ = meta.active_bean_id;
            next_id_ = meta.next_id == 0 ? 1 : meta.next_id;

            const uint8_t stored_count = std::min<uint8_t>(meta.bean_count, kMaxBeans);
            for (uint8_t i = 0; i < stored_count && bean_count_ < kMaxBeans; ++i) {
                const uint8_t id = meta.ids[i];
                if (id == 0 || find_index(id) >= 0) {
                    needs_repair = true;
                    continue;
                }

                char key[8];
                bean_key(id, key, sizeof(key));
                if (prefs.getBytesLength(key) != sizeof(BeanRecord)) {
                    needs_repair = true;
                    continue;
                }

                BeanRecord bean = {};
                if (prefs.getBytes(key, &bean, sizeof(bean)) != sizeof(bean) ||
                    !bean.valid || bean.id != id) {
                    needs_repair = true;
                    continue;
                }

                beans_[bean_count_++] = bean;
            }
        } else {
            LOG_BLE("Beans: stored metadata rejected (magic/version/size mismatch) - starting empty\n");
            clear();
            needs_repair = true;
        }
    } else if (length > 0) {
        LOG_BLE("Beans: stored metadata size %u != expected %u - starting empty\n",
                static_cast<unsigned>(length), static_cast<unsigned>(sizeof(BeanStoreMeta)));
        clear();
        needs_repair = true;
    }

    const uint8_t before_count = bean_count_;
    const uint8_t before_active = active_bean_id_;
    const uint8_t before_next = next_id_;
    normalize_after_load();
    needs_repair = needs_repair ||
                   before_count != bean_count_ ||
                   before_active != active_bean_id_ ||
                   before_next != next_id_;
    prefs.end();
    if (needs_repair) {
        persist_meta();
    }
    return true;
}

bool BeanController::load_legacy_snapshot() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return false;
    }

    const size_t length = prefs.getBytesLength(kSnapshotKey);
    bool loaded = false;
    bool migrated_from_v1 = false;
    if (length == sizeof(LegacyBeanStoreSnapshot)) {
        LegacyBeanStoreSnapshot snapshot = {};
        if (prefs.getBytes(kSnapshotKey, &snapshot, sizeof(snapshot)) == sizeof(snapshot) &&
            snapshot.magic == kStoreMagic &&
            (snapshot.version == 2 || snapshot.version == 1)) {
            migrated_from_v1 = snapshot.version == 1;
            active_bean_id_ = snapshot.active_bean_id;
            next_id_ = snapshot.next_id == 0 ? 1 : snapshot.next_id;
            bean_count_ = std::min<uint8_t>(snapshot.bean_count, kLegacyMaxBeans);
            std::memcpy(beans_, snapshot.beans, sizeof(snapshot.beans));
            if (migrated_from_v1) {
                for (uint8_t i = 0; i < bean_count_; ++i) {
                    beans_[i].mahlgrad_x2[kSingleProfileIndex] =
                        beans_[i].mahlgrad_x2[kDoubleProfileIndex];
                }
            }
            normalize_after_load();
            loaded = true;
        } else {
            LOG_BLE("Beans: legacy snapshot rejected (magic/version mismatch) - starting empty\n");
        }
    } else if (length > 0) {
        LOG_BLE("Beans: legacy data size %u != expected %u - starting empty\n",
                static_cast<unsigned>(length), static_cast<unsigned>(sizeof(LegacyBeanStoreSnapshot)));
    }

    prefs.end();
    if (loaded) {
        LOG_BLE("Beans: migrated %u beans from legacy snapshot%s\n",
                static_cast<unsigned>(bean_count_),
                migrated_from_v1 ? " (v1 grind-size copy)" : "");
    }
    return loaded;
}

bool BeanController::save() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }

    bool ok = true;
    for (uint8_t i = 0; i < bean_count_; ++i) {
        char key[8];
        bean_key(beans_[i].id, key, sizeof(key));
        ok = prefs.putBytes(key, &beans_[i], sizeof(beans_[i])) == sizeof(beans_[i]) && ok;
    }

    BeanStoreMeta meta = {};
    fill_meta(meta);
    ok = prefs.putBytes(kMetaKey, &meta, sizeof(meta)) == sizeof(meta) && ok;
    prefs.end();
    return ok;
}

void BeanController::fill_meta(BeanStoreMeta& meta) const {
    meta.magic = kStoreMagic;
    meta.version = kStoreVersion;
    meta.active_bean_id = active_bean_id_;
    meta.next_id = next_id_ == 0 ? 1 : next_id_;
    meta.bean_count = bean_count_;
    meta.record_size = sizeof(BeanRecord);
    meta.capacity = kMaxBeans;
    for (uint8_t i = 0; i < bean_count_ && i < kMaxBeans; ++i) {
        meta.ids[i] = beans_[i].id;
    }
}

bool BeanController::persist_meta() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    BeanStoreMeta meta = {};
    fill_meta(meta);
    const size_t written = prefs.putBytes(kMetaKey, &meta, sizeof(meta));
    prefs.end();
    return written == sizeof(meta);
}

bool BeanController::persist_bean(const BeanRecord& bean) const {
    if (!bean.valid || bean.id == 0) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    char key[8];
    bean_key(bean.id, key, sizeof(key));
    const size_t written = prefs.putBytes(key, &bean, sizeof(bean));
    prefs.end();
    return written == sizeof(bean);
}

bool BeanController::remove_bean_record(uint8_t id) const {
    if (id == 0) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    char key[8];
    bean_key(id, key, sizeof(key));
    const bool removed = prefs.remove(key);
    prefs.end();
    return removed;
}

void BeanController::normalize_after_load() {
    uint8_t compact_count = 0;
    BeanRecord compact[kMaxBeans] = {};
    uint8_t highest_id = 0;

    for (uint8_t i = 0; i < kMaxBeans; ++i) {
        BeanRecord bean = beans_[i];
        if (!bean.valid || bean.id == 0) {
            continue;
        }
        bean.name[sizeof(bean.name) - 1] = '\0';
        bean.roaster[sizeof(bean.roaster) - 1] = '\0';
        for (uint8_t p = 0; p < USER_PROFILE_COUNT; ++p) {
            bean.mahlgrad_x2[p] = std::clamp<uint16_t>(bean.mahlgrad_x2[p], kMinMahlgradX2, kMaxMahlgradX2);
        }
        if (bean.name[0] == '\0') {
            copy_text(bean.name, sizeof(bean.name), "Unnamed bean");
        }
        compact[compact_count++] = bean;
        highest_id = std::max(highest_id, bean.id);
    }

    std::memset(beans_, 0, sizeof(beans_));
    std::memcpy(beans_, compact, sizeof(compact));
    bean_count_ = compact_count;

    if (!find_bean(active_bean_id_)) {
        active_bean_id_ = bean_count_ > 0 ? beans_[0].id : 0;
    }

    if (next_id_ == 0 || find_bean(next_id_)) {
        next_id_ = highest_id == 255 ? 1 : static_cast<uint8_t>(highest_id + 1);
    }
}

const BeanRecord* BeanController::get_bean_at(uint8_t index) const {
    if (index >= bean_count_) {
        return nullptr;
    }
    return &beans_[index];
}

int BeanController::find_index(uint8_t id) const {
    if (id == 0) {
        return -1;
    }
    for (uint8_t i = 0; i < bean_count_; ++i) {
        if (beans_[i].valid && beans_[i].id == id) {
            return i;
        }
    }
    return -1;
}

const BeanRecord* BeanController::find_bean(uint8_t id) const {
    const int index = find_index(id);
    return index >= 0 ? &beans_[index] : nullptr;
}

BeanRecord* BeanController::find_bean(uint8_t id) {
    const int index = find_index(id);
    return index >= 0 ? &beans_[index] : nullptr;
}

uint8_t BeanController::allocate_id() {
    for (uint16_t attempt = 0; attempt < 255; ++attempt) {
        uint8_t candidate = next_id_ == 0 ? 1 : next_id_;
        next_id_ = candidate == 255 ? 1 : static_cast<uint8_t>(candidate + 1);
        if (!find_bean(candidate)) {
            return candidate;
        }
    }
    return 0;
}

bool BeanController::create_bean(const char* name, const char* roaster, uint16_t bag_size_g,
                                 float single_mahlgrad, float double_mahlgrad, uint8_t* created_id) {
    if (bean_count_ >= kMaxBeans) {
        return false;
    }

    const uint8_t old_count = bean_count_;
    const uint8_t old_active = active_bean_id_;
    const uint8_t old_next = next_id_;
    const uint8_t id = allocate_id();
    if (id == 0) {
        return false;
    }

    BeanRecord& bean = beans_[bean_count_];
    std::memset(&bean, 0, sizeof(bean));
    bean.id = id;
    bean.valid = true;
    copy_text(bean.name, sizeof(bean.name), name && name[0] ? name : "Unnamed bean");
    copy_text(bean.roaster, sizeof(bean.roaster), roaster ? roaster : "");
    bean.bag_size_g = bag_size_g;
    for (uint8_t p = 0; p < USER_PROFILE_COUNT; ++p) {
        bean.mahlgrad_x2[p] = kDefaultMahlgradX2;
    }
    bean.mahlgrad_x2[kSingleProfileIndex] = mahlgrad_to_x2(single_mahlgrad);
    bean.mahlgrad_x2[kDoubleProfileIndex] = mahlgrad_to_x2(double_mahlgrad);
    bean_count_++;

    if (active_bean_id_ == 0) {
        active_bean_id_ = id;
    }
    if (!persist_bean(bean) || !persist_meta()) {
        std::memset(&bean, 0, sizeof(bean));
        bean_count_ = old_count;
        active_bean_id_ = old_active;
        next_id_ = old_next;
        return false;
    }
    if (created_id) {
        *created_id = id;
    }
    return true;
}

bool BeanController::update_bean(uint8_t id, const char* name, const char* roaster,
                                 uint16_t bag_size_g, float single_mahlgrad, float double_mahlgrad) {
    BeanRecord* bean = find_bean(id);
    if (!bean) {
        return false;
    }

    const BeanRecord old = *bean;
    copy_text(bean->name, sizeof(bean->name), name && name[0] ? name : "Unnamed bean");
    copy_text(bean->roaster, sizeof(bean->roaster), roaster ? roaster : "");
    bean->bag_size_g = bag_size_g;
    bean->mahlgrad_x2[kSingleProfileIndex] = mahlgrad_to_x2(single_mahlgrad);
    bean->mahlgrad_x2[kDoubleProfileIndex] = mahlgrad_to_x2(double_mahlgrad);
    if (!persist_bean(*bean)) {
        *bean = old;
        return false;
    }
    return true;
}

bool BeanController::delete_bean(uint8_t id) {
    const int index = find_index(id);
    if (index < 0) {
        return false;
    }

    const uint8_t old_count = bean_count_;
    const uint8_t old_active = active_bean_id_;
    BeanRecord old_beans[kMaxBeans] = {};
    std::memcpy(old_beans, beans_, sizeof(old_beans));

    for (uint8_t i = static_cast<uint8_t>(index); i + 1 < bean_count_; ++i) {
        beans_[i] = beans_[i + 1];
    }
    std::memset(&beans_[bean_count_ - 1], 0, sizeof(beans_[bean_count_ - 1]));
    bean_count_--;

    if (active_bean_id_ == id) {
        active_bean_id_ = bean_count_ > 0 ? beans_[0].id : 0;
    }
    if (!persist_meta()) {
        std::memcpy(beans_, old_beans, sizeof(beans_));
        bean_count_ = old_count;
        active_bean_id_ = old_active;
        return false;
    }
    remove_bean_record(id);
    return true;
}

bool BeanController::set_active_bean(uint8_t id) {
    if (!find_bean(id)) {
        return false;
    }
    const uint8_t old_active = active_bean_id_;
    active_bean_id_ = id;
    if (!persist_meta()) {
        active_bean_id_ = old_active;
        return false;
    }
    return true;
}

bool BeanController::apply_feedback(uint8_t id, uint8_t profile_index, Feedback feedback) {
    BeanRecord* bean = find_bean(id);
    if (!bean || !stores_mahlgrad_for_profile(profile_index)) {
        return false;
    }

    const BeanRecord old = *bean;
    int next = static_cast<int>(bean->mahlgrad_x2[profile_index]);
    if (feedback == Feedback::FINER) {
        next -= 1;
    } else if (feedback == Feedback::COARSER) {
        next += 1;
    }
    bean->mahlgrad_x2[profile_index] =
        std::clamp<uint16_t>(static_cast<uint16_t>(next), kMinMahlgradX2, kMaxMahlgradX2);
    if (!persist_bean(*bean)) {
        *bean = old;
        return false;
    }
    return true;
}

bool BeanController::apply_feedback_to_active(uint8_t profile_index, Feedback feedback) {
    return apply_feedback(active_bean_id_, profile_index, feedback);
}

bool BeanController::add_dose_used_g(float grams) {
    BeanRecord* bean = find_bean(active_bean_id_);
    if (!bean || !std::isfinite(grams) || grams <= 0.0f) {
        return false;
    }
    const BeanRecord old = *bean;
    bean->dose_used_x10 = std::min<uint32_t>(kMaxUsageX10, bean->dose_used_x10 + grams_to_x10(grams));
    if (!persist_bean(*bean)) {
        *bean = old;
        return false;
    }
    return true;
}

bool BeanController::add_purge_used_g(float grams) {
    BeanRecord* bean = find_bean(active_bean_id_);
    if (!bean || !std::isfinite(grams) || grams <= 0.0f) {
        return false;
    }
    const BeanRecord old = *bean;
    bean->purge_used_x10 = std::min<uint32_t>(kMaxUsageX10, bean->purge_used_x10 + grams_to_x10(grams));
    if (!persist_bean(*bean)) {
        *bean = old;
        return false;
    }
    return true;
}

uint16_t BeanController::mahlgrad_to_x2(float value) {
    if (!std::isfinite(value)) {
        return kDefaultMahlgradX2;
    }
    int rounded = static_cast<int>(std::lround(value * 2.0f));
    rounded = std::clamp<int>(rounded, kMinMahlgradX2, kMaxMahlgradX2);
    return static_cast<uint16_t>(rounded);
}

float BeanController::x2_to_mahlgrad(uint16_t value_x2) {
    value_x2 = std::clamp<uint16_t>(value_x2, kMinMahlgradX2, kMaxMahlgradX2);
    return static_cast<float>(value_x2) / 2.0f;
}

void BeanController::format_mahlgrad(char* out, size_t out_len, uint16_t value_x2) {
    if (!out || out_len == 0) {
        return;
    }
    value_x2 = std::clamp<uint16_t>(value_x2, kMinMahlgradX2, kMaxMahlgradX2);
    if ((value_x2 % 2) == 0) {
        std::snprintf(out, out_len, "%u", static_cast<unsigned>(value_x2 / 2));
    } else {
        std::snprintf(out, out_len, "%u.5", static_cast<unsigned>(value_x2 / 2));
    }
}

bool BeanController::stores_mahlgrad_for_profile(uint8_t profile_index) {
    return profile_index == kSingleProfileIndex || profile_index == kDoubleProfileIndex;
}

void BeanController::copy_text(char* dest, size_t dest_len, const char* src) {
    if (!dest || dest_len == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, dest_len - 1);
    dest[dest_len - 1] = '\0';
}

void BeanController::bean_key(uint8_t id, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    std::snprintf(out, out_len, "b%03u", static_cast<unsigned>(id));
}

uint32_t BeanController::grams_to_x10(float grams) {
    if (!std::isfinite(grams) || grams <= 0.0f) {
        return 0;
    }
    const float clamped = std::min<float>(grams, static_cast<float>(kMaxUsageX10) / 10.0f);
    return static_cast<uint32_t>(std::lround(clamped * 10.0f));
}
