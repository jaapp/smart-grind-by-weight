#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../config/constants.h"

// Latest arrivals snapshot fetched from the gateway. Copied out under a mutex
// so the UI task can render it without touching the network task's state.
struct TrainArrivalItem {
    char route[8];                      // Line name, e.g. "N"
    char direction[24];                 // Friendly direction label, e.g. "Manhattan"
    uint32_t color;                     // Badge color, 0xRRGGBB
    uint32_t text_color;                // Badge glyph color, 0xRRGGBB
    uint8_t mins_count;
    uint8_t mins[NET_MAX_ARRIVAL_MINS]; // Minutes until upcoming arrivals, ascending
};

struct TrainArrivals {
    uint32_t fetched_at_ms;             // millis() of the successful fetch (0 = never)
    bool gateway_stale;                 // Gateway reported its own MTA data as stale
    uint8_t item_count;
    TrainArrivalItem items[NET_MAX_ARRIVAL_ITEMS];
};

enum class NetworkState {
    UNCONFIGURED,   // No WiFi credentials in NVS
    CONNECTING,
    CONNECTED,
    ERROR,          // Last fetch or connection attempt failed
};

/**
 * TrainDataClient - WiFi lifecycle and gateway polling
 *
 * Runs from the dedicated network FreeRTOS task (core 1, low priority).
 * Keeps WiFi connected whenever credentials exist, and polls the gateway's
 * arrivals endpoint while the trains screensaver is visible. All public
 * methods are safe to call from other tasks.
 */
class TrainDataClient {
public:
    void init();

    // Persist new credentials/URL and trigger a reconnect. Called from the BLE task.
    void set_config(const char* ssid, const char* password, const char* gateway_url);

    // Called by the screensaver on show/hide to start/stop polling
    void set_polling_active(bool active);

    bool has_config() const { return has_config_; }
    NetworkState get_state() const { return state_; }

    // Copies the latest snapshot; returns false if nothing was fetched yet
    bool get_arrivals(TrainArrivals& out);

    // Fills a short human-readable status line (for BLE debug logging)
    void get_status_string(char* out, size_t out_len);

    // Network task body, called by TaskManager
    void task_impl();

private:
    void load_config();
    void update_wifi();
    void fetch_arrivals();
    bool parse_arrivals(const char* json, TrainArrivals& out);

    SemaphoreHandle_t mutex_ = nullptr;

    char ssid_[NET_MAX_CONFIG_STRING_LEN] = {0};
    char password_[NET_MAX_CONFIG_STRING_LEN] = {0};
    char gateway_url_[NET_MAX_CONFIG_STRING_LEN] = {0};

    volatile bool has_config_ = false;
    volatile bool config_changed_ = false;
    volatile bool polling_active_ = false;
    volatile NetworkState state_ = NetworkState::UNCONFIGURED;

    TrainArrivals arrivals_ = {};
    char last_error_[64] = {0};
    uint32_t last_connect_attempt_ms_ = 0;
    uint32_t last_fetch_ms_ = 0;
    bool wifi_started_ = false;
};

extern TrainDataClient train_data_client;
