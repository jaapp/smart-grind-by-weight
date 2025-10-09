#include "statistics_manager.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

StatisticsManager statistics_manager;

namespace {
StaticSemaphore_t g_stats_mutex_buffer;
SemaphoreHandle_t g_stats_mutex = nullptr;

struct StatisticsSnapshotV1 {
    uint32_t version;
    uint32_t total_grinds;
    uint32_t single_shots;
    uint32_t double_shots;
    uint32_t custom_shots;
    uint32_t motor_runtime_sec;
    uint32_t motor_runtime_ms_remainder;
    uint32_t device_uptime_hrs;
    float total_weight_kg;
    uint32_t weight_mode_grinds;
    uint32_t time_mode_grinds;
    uint32_t time_pulses;
    uint32_t total_pulses;
    uint32_t accuracy_sample_count;
    float accuracy_sum;
    uint32_t pulse_sample_count;
    float pulse_sum;
    uint32_t reserved0;
    uint32_t reserved1;
};

class StatsLockGuard {
public:
    explicit StatsLockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }

    ~StatsLockGuard() {
        if (mutex_) {
            xSemaphoreGive(mutex_);
        }
    }

private:
    SemaphoreHandle_t mutex_;
};

constexpr uint32_t kPulseFlushThreshold = 50;        // Flush every ~5 seconds of pulses
constexpr uint32_t kPulseDurationMs = 100;           // Each time-mode pulse is 100ms
constexpr uint32_t kMaxFlushIntervalMs = 10000;      // Failsafe flush window
constexpr uint32_t kMinFlushIntervalMs = 1000;       // Minimum spacing between auto-flush commits
} // namespace

void StatisticsManager::init(Preferences* prefs) {
    (void)prefs;

    if (!g_stats_mutex) {
        g_stats_mutex = xSemaphoreCreateMutexStatic(&g_stats_mutex_buffer);
    }

    {
        StatsLockGuard lock(g_stats_mutex);
        load_from_storage();
        last_flush_ms_ = millis();
    }

    initialized_ = true;
}

void StatisticsManager::update_grind_session(float final_weight, float error_grams, uint8_t pulse_count,
                                             bool is_weight_mode, uint32_t motor_time_ms) {
    if (!initialized_) return;

    StatsLockGuard lock(g_stats_mutex);

    snapshot_.total_grinds++;

    if (final_weight <= 10.0f) {
        snapshot_.single_shots++;
    } else if (final_weight <= 22.0f) {
        snapshot_.double_shots++;
    } else {
        snapshot_.custom_shots++;
    }

    if (is_weight_mode) {
        snapshot_.weight_mode_grinds++;
        float abs_error = std::fabs(error_grams);
        snapshot_.accuracy_sample_count++;
        snapshot_.accuracy_sum += abs_error;
    } else {
        snapshot_.time_mode_grinds++;
    }

    add_motor_runtime_ms_locked(motor_time_ms);

    snapshot_.total_weight_kg += (final_weight / 1000.0f);
    snapshot_.total_pulses += pulse_count;
    snapshot_.pulse_sample_count++;
    snapshot_.pulse_sum += pulse_count;

    mark_dirty_locked();
    persist_locked(true); // Session-level updates should persist immediately
}

void StatisticsManager::update_motor_test(uint32_t duration_ms) {
    if (!initialized_) return;

    StatsLockGuard lock(g_stats_mutex);
    add_motor_runtime_ms_locked(duration_ms);
    mark_dirty_locked();
    persist_locked(true);
}

void StatisticsManager::update_time_pulse() {
    if (!initialized_) return;

    StatsLockGuard lock(g_stats_mutex);

    snapshot_.time_pulses++;
    add_motor_runtime_ms_locked(kPulseDurationMs);
    mark_dirty_locked();
    pending_pulse_flush_counter_++;

    uint32_t now = millis();
    bool flush_due = pending_pulse_flush_counter_ >= kPulseFlushThreshold;
    if (!flush_due && dirty_) {
        uint32_t elapsed = now - last_flush_ms_;
        flush_due = elapsed >= kMaxFlushIntervalMs;
    }

    bool flushed = persist_locked(flush_due);
    if (flushed || (dirty_ && (now - last_flush_ms_) >= kMinFlushIntervalMs)) {
        // Avoid holding onto dirty data for too long even if we skipped flushing
        if (!flushed) {
            persist_locked(true);
            flushed = true;
        }
    }

    if (flushed) {
        pending_pulse_flush_counter_ = 0;
    }
}

void StatisticsManager::update_uptime(uint32_t minutes_to_add) {
    if (!initialized_ || minutes_to_add == 0) return;

    StatsLockGuard lock(g_stats_mutex);
    add_uptime_minutes_locked(minutes_to_add);
    mark_dirty_locked();
    persist_locked(true);
}

uint32_t StatisticsManager::get_total_grinds() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.total_grinds;
}

uint32_t StatisticsManager::get_single_shots() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.single_shots;
}

uint32_t StatisticsManager::get_double_shots() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.double_shots;
}

uint32_t StatisticsManager::get_custom_shots() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.custom_shots;
}

uint32_t StatisticsManager::get_motor_runtime_sec() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.motor_runtime_sec;
}

uint64_t StatisticsManager::get_motor_runtime_ms() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return (static_cast<uint64_t>(snapshot_.motor_runtime_sec) * 1000ULL) +
           static_cast<uint64_t>(snapshot_.motor_runtime_ms_remainder);
}

uint32_t StatisticsManager::get_device_uptime_hrs() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.device_uptime_hrs;
}

uint32_t StatisticsManager::get_device_uptime_min_remainder() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.device_uptime_min_remainder;
}

float StatisticsManager::get_total_weight_kg() const {
    if (!initialized_) return 0.0f;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.total_weight_kg;
}

uint32_t StatisticsManager::get_weight_mode_grinds() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.weight_mode_grinds;
}

uint32_t StatisticsManager::get_time_mode_grinds() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.time_mode_grinds;
}

uint32_t StatisticsManager::get_time_pulses() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.time_pulses;
}

float StatisticsManager::get_avg_accuracy_g() const {
    if (!initialized_) return 0.0f;
    StatsLockGuard lock(g_stats_mutex);
    if (snapshot_.accuracy_sample_count == 0) return 0.0f;
    return snapshot_.accuracy_sum / static_cast<float>(snapshot_.accuracy_sample_count);
}

uint32_t StatisticsManager::get_total_pulses() const {
    if (!initialized_) return 0;
    StatsLockGuard lock(g_stats_mutex);
    return snapshot_.total_pulses;
}

float StatisticsManager::get_avg_pulses() const {
    if (!initialized_) return 0.0f;
    StatsLockGuard lock(g_stats_mutex);
    if (snapshot_.pulse_sample_count == 0) return 0.0f;
    return snapshot_.pulse_sum / static_cast<float>(snapshot_.pulse_sample_count);
}

void StatisticsManager::reset_all() {
    if (!initialized_) return;

    StatsLockGuard lock(g_stats_mutex);
    std::memset(&snapshot_, 0, sizeof(snapshot_));
    snapshot_.version = StatisticsSnapshot::kVersion;
    dirty_ = true;
    pending_pulse_flush_counter_ = 0;
    persist_locked(true);
    last_flush_ms_ = millis();
}

void StatisticsManager::reset_statistics_only() {
    reset_all();
}

void StatisticsManager::load_from_storage() {
    std::memset(&snapshot_, 0, sizeof(snapshot_));
    snapshot_.version = StatisticsSnapshot::kVersion;

    Preferences stats_prefs;
    stats_prefs.begin("stats", false);

    size_t stored_size = stats_prefs.getBytesLength("snapshot");
    if (stored_size == sizeof(StatisticsSnapshot)) {
        stats_prefs.getBytes("snapshot", &snapshot_, sizeof(StatisticsSnapshot));
    } else if (stored_size == sizeof(StatisticsSnapshotV1)) {
        StatisticsSnapshotV1 legacy_snapshot = {};
        stats_prefs.getBytes("snapshot", &legacy_snapshot, sizeof(StatisticsSnapshotV1));

        snapshot_.version = StatisticsSnapshot::kVersion;
        snapshot_.total_grinds = legacy_snapshot.total_grinds;
        snapshot_.single_shots = legacy_snapshot.single_shots;
        snapshot_.double_shots = legacy_snapshot.double_shots;
        snapshot_.custom_shots = legacy_snapshot.custom_shots;
        snapshot_.motor_runtime_sec = legacy_snapshot.motor_runtime_sec;
        snapshot_.motor_runtime_ms_remainder = legacy_snapshot.motor_runtime_ms_remainder;
        snapshot_.device_uptime_hrs = legacy_snapshot.device_uptime_hrs;
        snapshot_.device_uptime_min_remainder = 0;
        snapshot_.total_weight_kg = legacy_snapshot.total_weight_kg;
        snapshot_.weight_mode_grinds = legacy_snapshot.weight_mode_grinds;
        snapshot_.time_mode_grinds = legacy_snapshot.time_mode_grinds;
        snapshot_.time_pulses = legacy_snapshot.time_pulses;
        snapshot_.total_pulses = legacy_snapshot.total_pulses;
        snapshot_.accuracy_sample_count = legacy_snapshot.accuracy_sample_count;
        snapshot_.accuracy_sum = legacy_snapshot.accuracy_sum;
        snapshot_.pulse_sample_count = legacy_snapshot.pulse_sample_count;
        snapshot_.pulse_sum = legacy_snapshot.pulse_sum;

        dirty_ = true;
        stats_prefs.putBytes("snapshot", &snapshot_, sizeof(StatisticsSnapshot));
    } else {
        migrate_from_legacy(stats_prefs);
        stats_prefs.putBytes("snapshot", &snapshot_, sizeof(StatisticsSnapshot));
    }

    stats_prefs.end();
    dirty_ = false;
    pending_pulse_flush_counter_ = 0;
}

void StatisticsManager::migrate_from_legacy(Preferences& stats_prefs) {
    // Preserve existing values if upgrading from key-per-stat version.
    snapshot_.total_grinds = stats_prefs.getUInt("total_grinds", 0);
    snapshot_.single_shots = stats_prefs.getUInt("single_shots", 0);
    snapshot_.double_shots = stats_prefs.getUInt("double_shots", 0);
    snapshot_.custom_shots = stats_prefs.getUInt("custom_shots", 0);
    snapshot_.motor_runtime_sec = stats_prefs.getUInt("motor_runtime", 0);
    snapshot_.motor_runtime_ms_remainder = stats_prefs.getUInt("pulse_millis", 0);
    snapshot_.device_uptime_hrs = stats_prefs.getUInt("uptime_hrs", 0);
    snapshot_.device_uptime_min_remainder = 0;
    snapshot_.total_weight_kg = stats_prefs.getFloat("total_weight_kg", 0.0f);
    snapshot_.weight_mode_grinds = stats_prefs.getUInt("weight_grinds", 0);
    snapshot_.time_mode_grinds = stats_prefs.getUInt("time_grinds", 0);
    snapshot_.time_pulses = stats_prefs.getUInt("time_pulses", 0);
    snapshot_.total_pulses = stats_prefs.getUInt("total_pulses", 0);
    snapshot_.accuracy_sample_count = stats_prefs.getUInt("acc_count", 0);
    snapshot_.accuracy_sum = stats_prefs.getFloat("acc_sum", 0.0f);
    snapshot_.pulse_sample_count = stats_prefs.getUInt("pulse_count", 0);
    snapshot_.pulse_sum = stats_prefs.getFloat("pulse_sum", 0.0f);

    // Old averages may still exist; keep them consistent if sums are missing.
    if (snapshot_.accuracy_sample_count == 0) {
        snapshot_.accuracy_sum = 0.0f;
    }
    if (snapshot_.pulse_sample_count == 0) {
        snapshot_.pulse_sum = 0.0f;
    }

    // Clear legacy keys to shrink namespace before writing the consolidated snapshot.
    stats_prefs.clear();

    dirty_ = true;
}

void StatisticsManager::add_motor_runtime_ms_locked(uint32_t additional_ms) {
    uint64_t total_ms = static_cast<uint64_t>(snapshot_.motor_runtime_ms_remainder) + additional_ms;
    uint32_t add_seconds = static_cast<uint32_t>(total_ms / 1000ULL);
    snapshot_.motor_runtime_ms_remainder = static_cast<uint32_t>(total_ms % 1000ULL);
    if (add_seconds > 0) {
        snapshot_.motor_runtime_sec += add_seconds;
    }
}

void StatisticsManager::add_uptime_minutes_locked(uint32_t minutes_to_add) {
    if (minutes_to_add == 0) {
        return;
    }

    uint64_t total_minutes = static_cast<uint64_t>(snapshot_.device_uptime_min_remainder) + minutes_to_add;
    uint32_t add_hours = static_cast<uint32_t>(total_minutes / 60ULL);
    snapshot_.device_uptime_min_remainder = static_cast<uint32_t>(total_minutes % 60ULL);
    if (add_hours > 0) {
        snapshot_.device_uptime_hrs += add_hours;
    }
}

bool StatisticsManager::persist_locked(bool force) {
    if (!dirty_) {
        return false;
    }

    uint32_t now = millis();
    if (!force) {
        uint32_t elapsed = now - last_flush_ms_;
        if (elapsed < kMinFlushIntervalMs) {
            return false;
        }
    }

    Preferences stats_prefs;
    stats_prefs.begin("stats", false);
    stats_prefs.putBytes("snapshot", &snapshot_, sizeof(StatisticsSnapshot));
    stats_prefs.end();

    dirty_ = false;
    last_flush_ms_ = now;
    return true;
}

void StatisticsManager::mark_dirty_locked() {
    dirty_ = true;
}
