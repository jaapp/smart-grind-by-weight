#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "../config/constants.h"

using UIStatusCallback = std::function<void(const char*)>;

struct ConnectivityUIStatusMessage {
    char text[64];
};

enum class ConnectivityState : uint8_t {
    WIFI_DISABLED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_SETUP_AP,
    WIFI_OTA_RECEIVING,
    WIFI_OTA_APPLYING,
    WIFI_ERROR
};

class ConnectivityManager {
private:
    Preferences* preferences_;
    WebServer server_;
    QueueHandle_t ui_status_queue_;
    UIStatusCallback ui_status_callback_;

    ConnectivityState state_;
    bool enabled_;
    bool server_started_;
    bool routes_registered_;
    bool setup_ap_active_;
    bool ota_in_progress_;
    bool ota_error_;
    bool screensaver_upload_error_;
    bool restart_pending_;
    volatile bool settings_changed_;

    String sta_ssid_;
    String status_message_;
    String ota_error_message_;
    String screensaver_error_message_;
    String expected_firmware_version_;

    File screensaver_upload_file_;
    const esp_partition_t* ota_partition_;
    esp_ota_handle_t ota_handle_;
    size_t ota_received_bytes_;
    size_t ota_total_bytes_;
    size_t screensaver_received_bytes_;
    unsigned long last_reconnect_attempt_ms_;
    unsigned long restart_at_ms_;

    void enqueue_ui_status(const char* status);
    void set_state(ConnectivityState state, const char* message = nullptr);
    bool load_credentials(String& ssid, String& password) const;
    void save_credentials(const String& ssid, const String& password);
    void clear_credentials();
    bool connect_station();
    bool start_setup_ap();
    bool start_mdns();
    void register_routes();
    void start_http_server();
    void send_cors_headers();
    void send_plain_response(int code, const char* body);
    void send_json_response(int code, const String& body);
    void handle_root();
    void handle_status();
    void handle_settings_get();
    void handle_settings_post();
    void handle_wifi_save();
    void handle_wifi_clear();
    void handle_screensaver_status();
    void handle_screensaver_upload();
    void handle_screensaver_complete();
    void handle_screensaver_clear();
    void handle_ota_upload();
    void handle_ota_complete();
    void handle_options();
    void handle_not_found();
    bool begin_ota(size_t expected_size);
    bool write_ota_chunk(const uint8_t* data, size_t size);
    bool finish_ota();
    void abort_ota();
    void schedule_restart();
    void store_expected_firmware_version(const String& version);
    String html_escape(const String& value) const;
    String json_escape(const String& value) const;
    String build_status_json() const;
    String build_settings_json() const;
    String build_screensaver_json() const;
    bool get_request_bool(const char* key, bool current_value, bool& found);
    float get_request_float(const char* key, float current_value, float min_value, float max_value, bool& found);
    int get_request_int(const char* key, int current_value, int min_value, int max_value, bool& found);
    String get_request_string(const char* key, bool& found);
    void mark_settings_changed();

public:
    ConnectivityManager();
    ~ConnectivityManager();

    void init(Preferences* prefs);
    void enable(unsigned long timeout_ms = 0);
    void enable_during_bootup();
    void disable();
    void handle();

    bool is_enabled() const { return enabled_; }
    bool is_connected() const;
    bool is_setup_mode() const { return setup_ap_active_; }
    bool is_updating() const { return ota_in_progress_; }
    bool is_debug_stream_active() const { return false; }

    float get_ota_progress() const;
    unsigned long get_remaining_time_ms() const { return 0; }
    unsigned long get_connectivity_timeout_remaining_ms() const { return 0; }

    bool is_data_export_active() const { return false; }
    float get_data_export_progress() const { return 0.0f; }
    uint32_t get_data_export_session_count() const { return 0; }
    void start_data_export() {}
    void stop_data_export() {}
    void update_data_export() {}

    const char* get_transport_label() const { return "WiFi"; }
    const char* get_status_label() const;
    String get_ip_address() const;
    String get_setup_address() const;
    String get_hostname() const { return WIFI_HOSTNAME; }
    String get_active_ssid() const;
    String get_mac_address() const;
    int32_t get_rssi_dbm() const;
    const char* get_mode_label() const;
    String get_ota_url() const;
    bool has_screensaver_image() const;
    size_t get_screensaver_image_size() const;
    bool consume_settings_changed();

    void set_ui_status_callback(UIStatusCallback callback);
    bool dequeue_ui_status(char* out, size_t out_len);
    String check_ota_failure_after_boot();
};

extern ConnectivityManager g_connectivity_manager;
extern ConnectivityManager& connectivity_manager;
