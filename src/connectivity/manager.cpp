#include "manager.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "../config/build_info.h"
#include "../logging/grind_logging.h"
#include "../tasks/task_manager.h"

ConnectivityManager::ConnectivityManager()
    : preferences_(nullptr)
    , server_(WIFI_HTTP_PORT)
    , ui_status_queue_(nullptr)
    , state_(ConnectivityState::WIFI_DISABLED)
    , enabled_(false)
    , server_started_(false)
    , routes_registered_(false)
    , setup_ap_active_(false)
    , ota_in_progress_(false)
    , ota_error_(false)
    , restart_pending_(false)
    , ota_partition_(nullptr)
    , ota_handle_(0)
    , ota_received_bytes_(0)
    , ota_total_bytes_(0)
    , last_reconnect_attempt_ms_(0)
    , restart_at_ms_(0) {
}

ConnectivityManager::~ConnectivityManager() {
    disable();
}

void ConnectivityManager::init(Preferences* prefs) {
    preferences_ = prefs;
    if (!ui_status_queue_) {
        ui_status_queue_ = xQueueCreate(8, sizeof(ConnectivityUIStatusMessage));
    }
    LOG_BLE("Connectivity: WiFi manager initialized\n");
}

void ConnectivityManager::set_ui_status_callback(UIStatusCallback callback) {
    ui_status_callback_ = callback;
}

void ConnectivityManager::enqueue_ui_status(const char* status) {
    if (!ui_status_queue_ || !status) {
        return;
    }

    ConnectivityUIStatusMessage msg;
    strncpy(msg.text, status, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    xQueueSend(ui_status_queue_, &msg, 0);
}

bool ConnectivityManager::dequeue_ui_status(char* out, size_t out_len) {
    if (!ui_status_queue_ || !out || out_len == 0) {
        return false;
    }

    ConnectivityUIStatusMessage msg;
    if (xQueueReceive(ui_status_queue_, &msg, 0) == pdPASS) {
        strncpy(out, msg.text, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

void ConnectivityManager::set_state(ConnectivityState state, const char* message) {
    state_ = state;
    if (message) {
        status_message_ = message;
        enqueue_ui_status(message);
    }
}

bool ConnectivityManager::load_credentials(String& ssid, String& password) const {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREF_NAMESPACE, true)) {
        return false;
    }

    ssid = prefs.getString(WIFI_PREF_KEY_SSID, "");
    password = prefs.getString(WIFI_PREF_KEY_PASSWORD, "");
    prefs.end();
    return ssid.length() > 0;
}

void ConnectivityManager::save_credentials(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
        LOG_BLE("WiFi: Failed to open preferences for credentials\n");
        return;
    }

    prefs.putString(WIFI_PREF_KEY_SSID, ssid);
    prefs.putString(WIFI_PREF_KEY_PASSWORD, password);
    prefs.end();
}

void ConnectivityManager::clear_credentials() {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
        return;
    }

    prefs.remove(WIFI_PREF_KEY_SSID);
    prefs.remove(WIFI_PREF_KEY_PASSWORD);
    prefs.end();
}

void ConnectivityManager::enable(unsigned long timeout_ms) {
    (void)timeout_ms;

    if (enabled_) {
        return;
    }

    enabled_ = true;
    setup_ap_active_ = false;
    restart_pending_ = false;
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setHostname(WIFI_HOSTNAME);

    if (!connect_station()) {
        start_setup_ap();
    }
}

void ConnectivityManager::enable_during_bootup() {
    Preferences prefs;
    bool startup_enabled = true;
    if (prefs.begin(WIFI_PREF_NAMESPACE, true)) {
        startup_enabled = prefs.getBool(WIFI_PREF_KEY_STARTUP, true);
        prefs.end();
    }

    if (startup_enabled) {
        enable();
    }
}

void ConnectivityManager::disable() {
    if (!enabled_) {
        return;
    }

    if (ota_in_progress_) {
        abort_ota();
    }

    server_.stop();
    server_started_ = false;
    MDNS.end();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    enabled_ = false;
    setup_ap_active_ = false;
    set_state(ConnectivityState::WIFI_DISABLED, "WiFi disabled");
}

bool ConnectivityManager::connect_station() {
    String ssid;
    String password;
    if (!load_credentials(ssid, password)) {
        LOG_BLE("WiFi: No stored credentials, starting setup AP\n");
        return false;
    }

    sta_ssid_ = ssid;
    setup_ap_active_ = false;
    set_state(ConnectivityState::WIFI_CONNECTING, "Connecting to WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start_ms < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG_BLE("WiFi: Failed to connect to %s\n", ssid.c_str());
        WiFi.disconnect(false);
        return false;
    }

    LOG_BLE("WiFi: Connected to %s, IP %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    start_mdns();
    start_http_server();
    set_state(ConnectivityState::WIFI_CONNECTED, "WiFi connected");
    return true;
}

bool ConnectivityManager::start_setup_ap() {
    setup_ap_active_ = true;
    sta_ssid_ = "";

    WiFi.mode(WIFI_AP);
    WiFi.setHostname(WIFI_HOSTNAME);

    bool ok = false;
    if (strlen(WIFI_SETUP_AP_PASSWORD) >= 8) {
        ok = WiFi.softAP(WIFI_SETUP_AP_SSID, WIFI_SETUP_AP_PASSWORD);
    } else {
        ok = WiFi.softAP(WIFI_SETUP_AP_SSID);
    }

    if (!ok) {
        set_state(ConnectivityState::WIFI_ERROR, "WiFi setup AP failed");
        return false;
    }

    start_http_server();
    LOG_BLE("WiFi: Setup AP ready: %s at %s\n",
            WIFI_SETUP_AP_SSID,
            WiFi.softAPIP().toString().c_str());
    set_state(ConnectivityState::WIFI_SETUP_AP, "WiFi setup AP active");
    return true;
}

bool ConnectivityManager::start_mdns() {
    if (MDNS.begin(WIFI_HOSTNAME)) {
        MDNS.addService("http", "tcp", WIFI_HTTP_PORT);
        LOG_BLE("WiFi: mDNS ready at http://%s.local\n", WIFI_HOSTNAME);
        return true;
    }

    LOG_BLE("WiFi: mDNS start failed, use IP address instead\n");
    return false;
}

void ConnectivityManager::register_routes() {
    if (routes_registered_) {
        return;
    }

    server_.on(WIFI_SETUP_PATH, HTTP_GET, [this]() { handle_root(); });
    server_.on(WIFI_STATUS_PATH, HTTP_GET, [this]() { handle_status(); });
    server_.on("/wifi", HTTP_POST, [this]() { handle_wifi_save(); });
    server_.on("/wifi/clear", HTTP_POST, [this]() { handle_wifi_clear(); });
    server_.on(WIFI_OTA_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_STATUS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on("/wifi", HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on("/wifi/clear", HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_OTA_PATH, HTTP_POST,
               [this]() { handle_ota_complete(); },
               [this]() { handle_ota_upload(); });
    server_.onNotFound([this]() { handle_not_found(); });

    routes_registered_ = true;
}

void ConnectivityManager::start_http_server() {
    register_routes();
    if (!server_started_) {
        server_.begin(WIFI_HTTP_PORT);
        server_started_ = true;
        LOG_BLE("WiFi: HTTP server listening on port %d\n", WIFI_HTTP_PORT);
    }
}

void ConnectivityManager::handle() {
    if (!enabled_) {
        return;
    }

    if (server_started_) {
        server_.handleClient();
    }

    if (restart_pending_ && millis() >= restart_at_ms_) {
        Serial.flush();
        esp_restart();
    }

    if (!setup_ap_active_ && !ota_in_progress_ && WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - last_reconnect_attempt_ms_ >= WIFI_RECONNECT_INTERVAL_MS) {
            last_reconnect_attempt_ms_ = now;
            LOG_BLE("WiFi: Connection lost, retrying station connection\n");
            if (!connect_station()) {
                start_setup_ap();
            }
        }
    }
}

void ConnectivityManager::send_cors_headers() {
    server_.sendHeader("Access-Control-Allow-Origin", "*");
    server_.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server_.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server_.sendHeader("Access-Control-Max-Age", "600");
}

void ConnectivityManager::send_plain_response(int code, const char* body) {
    send_cors_headers();
    server_.send(code, "text/plain", body ? body : "");
}

void ConnectivityManager::send_json_response(int code, const String& body) {
    send_cors_headers();
    server_.send(code, "application/json", body);
}

String ConnectivityManager::html_escape(const String& value) const {
    String escaped;
    escaped.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

void ConnectivityManager::handle_root() {
    send_cors_headers();

    String ssid;
    String password;
    load_credentials(ssid, password);

    String body;
    body.reserve(2800);
    body += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
    body += F("<title>GrindByWeight WiFi</title><style>");
    body += F("body{font-family:Arial,sans-serif;margin:24px;background:#111;color:#f4f4f4}");
    body += F("main{max-width:520px;margin:auto}label,input,button{display:block;width:100%;box-sizing:border-box}");
    body += F("input,button{font-size:16px;padding:12px;margin:8px 0 18px;border-radius:6px;border:0}");
    body += F("button{background:#d71920;color:#fff;font-weight:700}.secondary{background:#333}");
    body += F(".card{background:#1d1d1d;padding:18px;border-radius:8px;margin:16px 0}.muted{color:#aaa}");
    body += F("</style></head><body><main><h1>GrindByWeight</h1>");
    body += F("<div class='card'><h2>WiFi Status</h2><p>");
    body += html_escape(get_status_label());
    body += F("</p><p class='muted'>IP: ");
    body += html_escape(get_ip_address());
    body += F("</p><p class='muted'>Host: http://");
    body += WIFI_HOSTNAME;
    body += F(".local</p></div>");
    body += F("<div class='card'><h2>Network</h2><form method='post' action='/wifi'>");
    body += F("<label>SSID</label><input name='ssid' value='");
    body += html_escape(ssid);
    body += F("' required><label>Password</label><input name='password' type='password'>");
    body += F("<button type='submit'>Save WiFi</button></form>");
    body += F("<form method='post' action='/wifi/clear'><button class='secondary' type='submit'>Clear WiFi</button></form></div>");
    body += F("<div class='card'><h2>OTA Update</h2><form method='post' action='/ota' enctype='multipart/form-data'>");
    body += F("<input type='file' name='firmware' accept='.bin' required><button type='submit'>Upload Firmware</button></form></div>");
    body += F("</main></body></html>");

    server_.send(200, "text/html", body);
}

void ConnectivityManager::handle_status() {
    String ip = get_ip_address();
    String json;
    json.reserve(512);
    json += "{";
    json += "\"device\":\"" CONNECTIVITY_DEVICE_NAME "\",";
    json += "\"transport\":\"wifi\",";
    json += "\"hostname\":\"" WIFI_HOSTNAME "\",";
    json += "\"version\":\"" BUILD_FIRMWARE_VERSION "\",";
    json += "\"build\":";
    json += BUILD_NUMBER;
    json += ",";
    json += "\"enabled\":";
    json += enabled_ ? "true" : "false";
    json += ",";
    json += "\"connected\":";
    json += is_connected() ? "true" : "false";
    json += ",";
    json += "\"setup_ap\":";
    json += setup_ap_active_ ? "true" : "false";
    json += ",";
    json += "\"ssid\":\"";
    json += html_escape(get_active_ssid());
    json += "\",";
    json += "\"ip\":\"";
    json += ip;
    json += "\",";
    json += "\"status\":\"";
    json += html_escape(get_status_label());
    json += "\",";
    json += "\"ota_active\":";
    json += ota_in_progress_ ? "true" : "false";
    json += ",";
    json += "\"ota_progress\":";
    json += String(get_ota_progress(), 1);
    json += "}";

    send_json_response(200, json);
}

void ConnectivityManager::handle_wifi_save() {
    String ssid = server_.arg("ssid");
    String password = server_.arg("password");
    ssid.trim();

    if (ssid.isEmpty()) {
        send_plain_response(400, "SSID is required");
        return;
    }

    save_credentials(ssid, password);
    send_plain_response(200, "WiFi saved. Restarting...");
    schedule_restart();
}

void ConnectivityManager::handle_wifi_clear() {
    clear_credentials();
    send_plain_response(200, "WiFi credentials cleared. Restarting into setup mode...");
    schedule_restart();
}

bool ConnectivityManager::begin_ota(size_t expected_size) {
    if (ota_in_progress_) {
        ota_error_ = true;
        ota_error_message_ = "OTA already in progress";
        return false;
    }

    ota_partition_ = esp_ota_get_next_update_partition(nullptr);
    if (!ota_partition_) {
        ota_error_ = true;
        ota_error_message_ = "No OTA partition available";
        return false;
    }

    LOG_BLE("WiFi OTA: Starting upload to partition %s\n", ota_partition_->label);

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 1800000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);

    task_manager.suspend_hardware_tasks();

    esp_err_t err = esp_ota_begin(ota_partition_, OTA_SIZE_UNKNOWN, &ota_handle_);
    if (err != ESP_OK) {
        ota_error_ = true;
        ota_error_message_ = "esp_ota_begin failed";
        task_manager.resume_hardware_tasks();
        return false;
    }

    ota_received_bytes_ = 0;
    ota_total_bytes_ = expected_size;
    ota_error_ = false;
    ota_error_message_ = "";
    ota_in_progress_ = true;
    set_state(ConnectivityState::WIFI_OTA_RECEIVING, "Receiving update...");
    return true;
}

bool ConnectivityManager::write_ota_chunk(const uint8_t* data, size_t size) {
    if (!ota_in_progress_ || !data || size == 0) {
        return false;
    }

    esp_err_t err = esp_ota_write(ota_handle_, data, size);
    if (err != ESP_OK) {
        ota_error_ = true;
        ota_error_message_ = "esp_ota_write failed";
        return false;
    }

    ota_received_bytes_ += size;
    if (ota_received_bytes_ == size || ota_received_bytes_ % 16384 == 0) {
        LOG_BLE("WiFi OTA: Received %u KB\n", static_cast<unsigned int>(ota_received_bytes_ / 1024));
    }
    return true;
}

bool ConnectivityManager::finish_ota() {
    if (!ota_in_progress_ || ota_error_) {
        abort_ota();
        return false;
    }

    set_state(ConnectivityState::WIFI_OTA_APPLYING, "Applying update...");

    esp_err_t err = esp_ota_end(ota_handle_);
    if (err != ESP_OK) {
        ota_error_ = true;
        ota_error_message_ = "esp_ota_end failed";
        abort_ota();
        return false;
    }

    err = esp_ota_set_boot_partition(ota_partition_);
    if (err != ESP_OK) {
        ota_error_ = true;
        ota_error_message_ = "esp_ota_set_boot_partition failed";
        abort_ota();
        return false;
    }

    LOG_BLE("WiFi OTA: Update complete (%u KB), reboot scheduled\n",
            static_cast<unsigned int>(ota_received_bytes_ / 1024));
    set_state(ConnectivityState::WIFI_OTA_APPLYING, "Restarting...");
    schedule_restart();
    return true;
}

void ConnectivityManager::abort_ota() {
    if (ota_in_progress_) {
        esp_ota_abort(ota_handle_);
    }

    ota_in_progress_ = false;
    ota_handle_ = 0;
    ota_partition_ = nullptr;
    ota_received_bytes_ = 0;
    ota_total_bytes_ = 0;
    task_manager.resume_hardware_tasks();

    if (enabled_) {
        set_state(ConnectivityState::WIFI_ERROR, "OTA failed");
    }
}

void ConnectivityManager::handle_ota_upload() {
    HTTPUpload& upload = server_.upload();

    switch (upload.status) {
        case UPLOAD_FILE_START:
            begin_ota(upload.totalSize);
            break;

        case UPLOAD_FILE_WRITE:
            if (!write_ota_chunk(upload.buf, upload.currentSize)) {
                ota_error_ = true;
            }
            break;

        case UPLOAD_FILE_END:
            if (ota_total_bytes_ == 0) {
                ota_total_bytes_ = upload.totalSize;
            }
            LOG_BLE("WiFi OTA: Upload finished, %u bytes\n",
                    static_cast<unsigned int>(upload.totalSize));
            break;

        case UPLOAD_FILE_ABORTED:
            ota_error_ = true;
            ota_error_message_ = "Upload aborted";
            abort_ota();
            break;
    }
}

void ConnectivityManager::handle_ota_complete() {
    String version = server_.arg("version");
    if (version.isEmpty()) {
        version = server_.arg("fw_version");
    }

    if (!version.isEmpty()) {
        store_expected_firmware_version(version);
    }

    if (ota_error_ || !finish_ota()) {
        String error = ota_error_message_.isEmpty() ? "OTA failed" : ota_error_message_;
        send_json_response(500, "{\"ok\":false,\"error\":\"" + html_escape(error) + "\"}");
        return;
    }

    send_json_response(200, "{\"ok\":true,\"message\":\"Update complete. Device restarting.\"}");
}

void ConnectivityManager::handle_options() {
    send_cors_headers();
    server_.send(204, "text/plain", "");
}

void ConnectivityManager::handle_not_found() {
    if (server_.method() == HTTP_OPTIONS) {
        handle_options();
        return;
    }

    send_plain_response(404, "Not found");
}

void ConnectivityManager::schedule_restart() {
    restart_pending_ = true;
    restart_at_ms_ = millis() + 1200;
}

void ConnectivityManager::store_expected_firmware_version(const String& version) {
    if (!preferences_ || version.isEmpty()) {
        return;
    }

    preferences_->putString("new_fw_ver", version);
    preferences_->remove("new_build_nr");
    expected_firmware_version_ = version;
}

String ConnectivityManager::check_ota_failure_after_boot() {
    if (!preferences_) {
        return "";
    }

    String expected_build = preferences_->getString("new_build_nr", "");
    String expected_version = preferences_->getString("new_fw_ver", "");

    if (expected_build.isEmpty() && expected_version.isEmpty()) {
        return "";
    }

    if (!expected_version.isEmpty()) {
        String current_version = BUILD_FIRMWARE_VERSION;
        preferences_->remove("new_build_nr");
        preferences_->remove("new_fw_ver");
        if (expected_version != current_version) {
            LOG_BLE("OTA: Version check failed - expected v%s, got v%s\n",
                    expected_version.c_str(), current_version.c_str());
            return expected_version;
        }
        LOG_BLE("OTA: Version check passed - expected v%s, got v%s\n",
                expected_version.c_str(), current_version.c_str());
        return "";
    }

    int expected_build_num = expected_build.toInt();
    preferences_->remove("new_build_nr");
    preferences_->remove("new_fw_ver");
    if (BUILD_NUMBER != expected_build_num) {
        LOG_BLE("OTA: Build number check failed - expected #%d, got #%d\n",
                expected_build_num, BUILD_NUMBER);
        return expected_build;
    }

    LOG_BLE("OTA: Build number check passed - expected #%d, got #%d\n",
            expected_build_num, BUILD_NUMBER);
    return "";
}

bool ConnectivityManager::is_connected() const {
    return WiFi.status() == WL_CONNECTED;
}

float ConnectivityManager::get_ota_progress() const {
    if (!ota_in_progress_ || ota_total_bytes_ == 0) {
        return ota_in_progress_ ? 1.0f : 0.0f;
    }
    float progress = (100.0f * static_cast<float>(ota_received_bytes_)) / static_cast<float>(ota_total_bytes_);
    return progress > 100.0f ? 100.0f : progress;
}

const char* ConnectivityManager::get_status_label() const {
    switch (state_) {
        case ConnectivityState::WIFI_CONNECTING:
            return "Connecting";
        case ConnectivityState::WIFI_CONNECTED:
            return "Connected";
        case ConnectivityState::WIFI_SETUP_AP:
            return "Setup AP";
        case ConnectivityState::WIFI_OTA_RECEIVING:
            return "Updating";
        case ConnectivityState::WIFI_OTA_APPLYING:
            return "Applying";
        case ConnectivityState::WIFI_ERROR:
            return "Error";
        case ConnectivityState::WIFI_DISABLED:
        default:
            return "Disabled";
    }
}

String ConnectivityManager::get_ip_address() const {
    if (setup_ap_active_) {
        return WiFi.softAPIP().toString();
    }
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "";
}

String ConnectivityManager::get_setup_address() const {
    return WiFi.softAPIP().toString();
}

String ConnectivityManager::get_active_ssid() const {
    if (setup_ap_active_) {
        return WIFI_SETUP_AP_SSID;
    }
    return sta_ssid_;
}
