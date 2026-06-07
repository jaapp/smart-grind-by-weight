#include "bean_controller.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

constexpr const char* BeanController::kNamespace;
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

    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return;
    }

    const size_t length = prefs.getBytesLength(kSnapshotKey);
    if (length == sizeof(BeanStoreSnapshot)) {
        BeanStoreSnapshot snapshot = {};
        if (prefs.getBytes(kSnapshotKey, &snapshot, sizeof(snapshot)) == sizeof(snapshot) &&
            snapshot.magic == kStoreMagic &&
            snapshot.version == kStoreVersion) {
            active_bean_id_ = snapshot.active_bean_id;
            next_id_ = snapshot.next_id == 0 ? 1 : snapshot.next_id;
            bean_count_ = std::min<uint8_t>(snapshot.bean_count, kMaxBeans);
            std::memcpy(beans_, snapshot.beans, sizeof(beans_));
            normalize_after_load();
        } else {
            // Surface the discard instead of silently starting empty so a
            // vanished bean list is diagnosable.
            LOG_BLE("Beans: stored snapshot rejected (magic/version mismatch) - starting empty\n");
        }
    } else if (length > 0) {
        LOG_BLE("Beans: stored data size %u != expected %u - starting empty\n",
                static_cast<unsigned>(length), static_cast<unsigned>(sizeof(BeanStoreSnapshot)));
    }

    prefs.end();
}

bool BeanController::save() const {
    BeanStoreSnapshot snapshot = {};
    fill_snapshot(snapshot);
    return persist_snapshot(snapshot);
}

void BeanController::fill_snapshot(BeanStoreSnapshot& snapshot) const {
    snapshot.magic = kStoreMagic;
    snapshot.version = kStoreVersion;
    snapshot.active_bean_id = active_bean_id_;
    snapshot.next_id = next_id_ == 0 ? 1 : next_id_;
    snapshot.bean_count = bean_count_;
    std::memcpy(snapshot.beans, beans_, sizeof(beans_));
}

bool BeanController::persist_snapshot(const BeanStoreSnapshot& snapshot) const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    const size_t written = prefs.putBytes(kSnapshotKey, &snapshot, sizeof(snapshot));
    prefs.end();
    return written == sizeof(snapshot);
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
                                 float double_mahlgrad, uint8_t* created_id) {
    if (bean_count_ >= kMaxBeans) {
        return false;
    }

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
    bean.mahlgrad_x2[kDoubleProfileIndex] = mahlgrad_to_x2(double_mahlgrad);
    bean_count_++;

    if (active_bean_id_ == 0) {
        active_bean_id_ = id;
    }
    if (created_id) {
        *created_id = id;
    }
    return save();
}

bool BeanController::update_bean(uint8_t id, const char* name, const char* roaster,
                                 uint16_t bag_size_g, float double_mahlgrad) {
    BeanRecord* bean = find_bean(id);
    if (!bean) {
        return false;
    }

    copy_text(bean->name, sizeof(bean->name), name && name[0] ? name : "Unnamed bean");
    copy_text(bean->roaster, sizeof(bean->roaster), roaster ? roaster : "");
    bean->bag_size_g = bag_size_g;
    bean->mahlgrad_x2[kDoubleProfileIndex] = mahlgrad_to_x2(double_mahlgrad);
    return save();
}

bool BeanController::delete_bean(uint8_t id) {
    const int index = find_index(id);
    if (index < 0) {
        return false;
    }

    for (uint8_t i = static_cast<uint8_t>(index); i + 1 < bean_count_; ++i) {
        beans_[i] = beans_[i + 1];
    }
    std::memset(&beans_[bean_count_ - 1], 0, sizeof(beans_[bean_count_ - 1]));
    bean_count_--;

    if (active_bean_id_ == id) {
        active_bean_id_ = bean_count_ > 0 ? beans_[0].id : 0;
    }
    return save();
}

bool BeanController::set_active_bean(uint8_t id) {
    if (!find_bean(id)) {
        return false;
    }
    active_bean_id_ = id;
    return save();
}

bool BeanController::apply_feedback(uint8_t id, Feedback feedback) {
    BeanRecord* bean = find_bean(id);
    if (!bean) {
        return false;
    }

    int next = static_cast<int>(bean->mahlgrad_x2[kDoubleProfileIndex]);
    if (feedback == Feedback::FINER) {
        next -= 1;
    } else if (feedback == Feedback::COARSER) {
        next += 1;
    }
    bean->mahlgrad_x2[kDoubleProfileIndex] =
        std::clamp<uint16_t>(static_cast<uint16_t>(next), kMinMahlgradX2, kMaxMahlgradX2);
    return save();
}

bool BeanController::apply_feedback_to_active(Feedback feedback) {
    return apply_feedback(active_bean_id_, feedback);
}

bool BeanController::add_dose_used_g(float grams) {
    BeanRecord* bean = find_bean(active_bean_id_);
    if (!bean || !std::isfinite(grams) || grams <= 0.0f) {
        return false;
    }
    bean->dose_used_x10 = std::min<uint32_t>(kMaxUsageX10, bean->dose_used_x10 + grams_to_x10(grams));
    return save();
}

bool BeanController::add_purge_used_g(float grams) {
    BeanRecord* bean = find_bean(active_bean_id_);
    if (!bean || !std::isfinite(grams) || grams <= 0.0f) {
        return false;
    }
    bean->purge_used_x10 = std::min<uint32_t>(kMaxUsageX10, bean->purge_used_x10 + grams_to_x10(grams));
    return save();
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

uint32_t BeanController::grams_to_x10(float grams) {
    if (!std::isfinite(grams) || grams <= 0.0f) {
        return 0;
    }
    const float clamped = std::min<float>(grams, static_cast<float>(kMaxUsageX10) / 10.0f);
    return static_cast<uint32_t>(std::lround(clamped * 10.0f));
}
