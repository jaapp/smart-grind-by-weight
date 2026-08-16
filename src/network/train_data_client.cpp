#include "train_data_client.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <cJSON.h>
#include <esp_heap_caps.h>
#include "../config/constants.h"
#include "../config/logging.h"

TrainDataClient train_data_client;

namespace {

constexpr uint32_t kTaskTickMs = 250;

void copy_string(char* dst, const char* src, size_t dst_len) {
    strlcpy(dst, src ? src : "", dst_len);
}

} // namespace

void TrainDataClient::init() {
    mutex_ = xSemaphoreCreateMutex();
    wifi_control_mutex_ = xSemaphoreCreateMutex();
    load_config();

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                last_disconnect_reason_ = info.wifi_sta_disconnected.reason;
                LOG_BLE("TrainDataClient: WiFi disconnected, reason=%d\n",
                        static_cast<int>(info.wifi_sta_disconnected.reason));
                break;
            case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                LOG_BLE("TrainDataClient: WiFi associated\n");
                break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                LOG_BLE("TrainDataClient: WiFi got IP %s\n", WiFi.localIP().toString().c_str());
                break;
            default:
                break;
        }
    });
    LOG_BLE("TrainDataClient: initialized (%s)\n",
            has_config_ ? "WiFi configured" : "WiFi not configured");
}

void TrainDataClient::load_config() {
    Preferences prefs;
    prefs.begin(NET_PREFS_NAMESPACE, true);
    String ssid = prefs.getString(NET_PREFS_KEY_SSID, "");
    String password = prefs.getString(NET_PREFS_KEY_PASSWORD, "");
    String url = prefs.getString(NET_PREFS_KEY_GATEWAY_URL, "");
    prefs.end();

    copy_string(ssid_, ssid.c_str(), sizeof(ssid_));
    copy_string(password_, password.c_str(), sizeof(password_));
    copy_string(gateway_url_, url.c_str(), sizeof(gateway_url_));

    has_config_ = ssid_[0] != '\0' && gateway_url_[0] != '\0';
    state_ = has_config_ ? NetworkState::CONNECTING : NetworkState::UNCONFIGURED;
}

void TrainDataClient::set_config(const char* ssid, const char* password, const char* gateway_url) {
    Preferences prefs;
    prefs.begin(NET_PREFS_NAMESPACE, false);
    prefs.putString(NET_PREFS_KEY_SSID, ssid ? ssid : "");
    prefs.putString(NET_PREFS_KEY_PASSWORD, password ? password : "");
    prefs.putString(NET_PREFS_KEY_GATEWAY_URL, gateway_url ? gateway_url : "");
    prefs.end();

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    load_config();
    config_changed_ = true;
    if (mutex_) xSemaphoreGive(mutex_);

    LOG_BLE("TrainDataClient: config updated (ssid='%s', url='%s')\n", ssid_, gateway_url_);
}

void TrainDataClient::set_polling_active(bool active) {
    polling_active_ = active;
    if (active) {
        // Force a fetch on the next task tick instead of waiting out the interval
        last_fetch_ms_ = 0;
    }
}

void TrainDataClient::pause_wifi() {
    if (wifi_control_mutex_) xSemaphoreTake(wifi_control_mutex_, portMAX_DELAY);
    wifi_paused_ = true;
    if (wifi_started_) {
        WiFi.disconnect(true);
        LOG_BLE("TrainDataClient: WiFi paused\n");
    }
    if (wifi_control_mutex_) xSemaphoreGive(wifi_control_mutex_);
}

void TrainDataClient::resume_wifi() {
    if (wifi_control_mutex_) xSemaphoreTake(wifi_control_mutex_, portMAX_DELAY);
    if (wifi_paused_) {
        wifi_paused_ = false;
        wifi_started_ = false;
        if (has_config_) state_ = NetworkState::CONNECTING;
        LOG_BLE("TrainDataClient: WiFi resumed\n");
    }
    if (wifi_control_mutex_) xSemaphoreGive(wifi_control_mutex_);
}

bool TrainDataClient::get_arrivals(TrainArrivals& out) {
    if (!mutex_) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    out = arrivals_;
    xSemaphoreGive(mutex_);
    return out.fetched_at_ms != 0;
}

void TrainDataClient::get_status_string(char* out, size_t out_len) {
    const char* state_name = "UNCONFIGURED";
    switch (state_) {
        case NetworkState::CONNECTING: state_name = "CONNECTING"; break;
        case NetworkState::CONNECTED: state_name = "CONNECTED"; break;
        case NetworkState::ERROR: state_name = "ERROR"; break;
        default: break;
    }
    snprintf(out, out_len,
             "state=%s ssid='%s' url='%s' ip=%s items=%u err='%s' wl=%d reason=%d "
             "mode_ok=%d begin=%d ticks=%lu heap=%u/%u",
             state_name, ssid_, gateway_url_,
             WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "-",
             arrivals_.item_count, last_error_,
             static_cast<int>(WiFi.status()), static_cast<int>(last_disconnect_reason_),
             last_mode_ok_ ? 1 : 0, static_cast<int>(last_begin_status_),
             static_cast<unsigned long>(loop_ticks_),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void TrainDataClient::task_impl() {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(kTaskTickMs);

    LOG_BLE("Network Task started on Core %d\n", xPortGetCoreID());

    while (true) {
        loop_ticks_++;
        if (wifi_paused_) {
            vTaskDelayUntil(&last_wake_time, frequency);
            continue;
        }

        if (config_changed_) {
            config_changed_ = false;
            wifi_started_ = false;
            WiFi.disconnect(true);
        }

        update_wifi();

        if (state_ == NetworkState::CONNECTED && polling_active_) {
            uint32_t now = millis();
            if (last_fetch_ms_ == 0 || now - last_fetch_ms_ >= NET_POLL_INTERVAL_MS) {
                last_fetch_ms_ = now;
                fetch_arrivals();
            }
        }

        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

void TrainDataClient::update_wifi() {
    if (!has_config_) {
        return;
    }

    if (WiFi.isConnected()) {
        if (state_ != NetworkState::CONNECTED) {
            state_ = NetworkState::CONNECTED;
            LOG_BLE("TrainDataClient: WiFi connected, ip=%s\n", WiFi.localIP().toString().c_str());
        }
        return;
    }

    if (state_ == NetworkState::CONNECTED) {
        LOG_BLE("TrainDataClient: WiFi connection lost\n");
        state_ = NetworkState::CONNECTING;
    }

    uint32_t now = millis();
    if (!wifi_started_ || now - last_connect_attempt_ms_ >= NET_WIFI_RETRY_INTERVAL_MS) {
        if (wifi_control_mutex_) xSemaphoreTake(wifi_control_mutex_, portMAX_DELAY);
        if (!wifi_paused_) {
            last_connect_attempt_ms_ = now;
            wifi_started_ = true;
            state_ = NetworkState::CONNECTING;
            last_mode_ok_ = WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
            last_begin_status_ = static_cast<int>(WiFi.begin(ssid_, password_));
            WiFi.setTxPower(NET_WIFI_TX_POWER);
            LOG_BLE("TrainDataClient: connecting to '%s' (mode_ok=%d begin=%d)\n",
                    ssid_, last_mode_ok_ ? 1 : 0, last_begin_status_);
        }
        if (wifi_control_mutex_) xSemaphoreGive(wifi_control_mutex_);
    }
}

void TrainDataClient::fetch_arrivals() {
    char url[NET_MAX_CONFIG_STRING_LEN + sizeof(NET_ARRIVALS_PATH)];
    snprintf(url, sizeof(url), "%s%s", gateway_url_, NET_ARRIVALS_PATH);

    HTTPClient http;
    http.setConnectTimeout(NET_HTTP_TIMEOUT_MS);
    http.setTimeout(NET_HTTP_TIMEOUT_MS);
    if (!http.begin(url)) {
        copy_string(last_error_, "bad url", sizeof(last_error_));
        state_ = NetworkState::ERROR;
        return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        snprintf(last_error_, sizeof(last_error_), "http %d", code);
        http.end();
        state_ = NetworkState::ERROR;
        LOG_BLE("TrainDataClient: arrivals fetch failed (%s)\n", last_error_);
        return;
    }

    String payload = http.getString();
    http.end();

    TrainArrivals parsed = {};
    if (!parse_arrivals(payload.c_str(), parsed)) {
        copy_string(last_error_, "bad json", sizeof(last_error_));
        state_ = NetworkState::ERROR;
        LOG_BLE("TrainDataClient: arrivals parse failed\n");
        return;
    }

    parsed.fetched_at_ms = millis();
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    arrivals_ = parsed;
    if (mutex_) xSemaphoreGive(mutex_);

    last_error_[0] = '\0';
    state_ = NetworkState::CONNECTED;
}

bool TrainDataClient::parse_arrivals(const char* json, TrainArrivals& out) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return false;
    }

    cJSON* stale = cJSON_GetObjectItem(root, "stale");
    out.gateway_stale = cJSON_IsTrue(stale);

    cJSON* items = cJSON_GetObjectItem(root, "items");
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }

    out.item_count = 0;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, items) {
        if (out.item_count >= NET_MAX_ARRIVAL_ITEMS) break;

        cJSON* route = cJSON_GetObjectItem(item, "route");
        cJSON* direction = cJSON_GetObjectItem(item, "direction");
        cJSON* station = cJSON_GetObjectItem(item, "station");
        cJSON* color = cJSON_GetObjectItem(item, "color");
        cJSON* text_color = cJSON_GetObjectItem(item, "text_color");
        cJSON* mins = cJSON_GetObjectItem(item, "mins");
        if (!cJSON_IsString(route) || !cJSON_IsString(direction) || !cJSON_IsArray(mins)) {
            continue;
        }

        TrainArrivalItem& dst = out.items[out.item_count];
        copy_string(dst.route, route->valuestring, sizeof(dst.route));
        copy_string(dst.direction, direction->valuestring, sizeof(dst.direction));
        copy_string(dst.station, cJSON_IsString(station) ? station->valuestring : "", sizeof(dst.station));
        dst.color = cJSON_IsString(color) ? strtoul(color->valuestring, nullptr, 16) : 0x808183;
        dst.text_color = cJSON_IsString(text_color) ? strtoul(text_color->valuestring, nullptr, 16) : 0xFFFFFF;

        // Optional walk-to-platform estimate; the gateway sends null when unset
        cJSON* walk_min = cJSON_GetObjectItem(item, "walk_min");
        int walk_value = cJSON_IsNumber(walk_min) ? walk_min->valueint : 0;
        dst.walk_min = static_cast<uint8_t>(walk_value < 0 ? 0 : (walk_value > 255 ? 255 : walk_value));

        dst.mins_count = 0;
        cJSON* min_entry = nullptr;
        cJSON_ArrayForEach(min_entry, mins) {
            if (dst.mins_count >= NET_MAX_ARRIVAL_MINS) break;
            if (!cJSON_IsNumber(min_entry)) continue;
            int value = min_entry->valueint;
            dst.mins[dst.mins_count++] = static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
        }

        out.item_count++;
    }

    cJSON_Delete(root);
    return true;
}
