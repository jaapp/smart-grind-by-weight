#include "manager.h"

#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <ctype.h>
#include <cmath>
#include <cstring>

#include "../config/build_info.h"
#include "../controllers/basket_detector.h"
#include "../controllers/bean_controller.h"
#include "../controllers/grind_controller.h"
#include "../controllers/grind_mode.h"
#include "../hardware/hardware_manager.h"
#include "../logging/grind_logging.h"
#include "../tasks/task_manager.h"

namespace {

float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

void append_write_error(String& errors, const char* setting_name) {
    if (!errors.isEmpty()) {
        errors += ", ";
    }
    errors += setting_name;
}

bool record_preference_write(size_t written, const char* setting_name, String& errors) {
    if (written > 0) {
        return true;
    }
    append_write_error(errors, setting_name);
    return false;
}

float sanitize_basket_weight(float weight_g) {
    if (!std::isfinite(weight_g)) {
        return 0.0f;
    }
    const float absolute_weight_g = std::fabs(weight_g);
    if (absolute_weight_g <= 0.0f) {
        return 0.0f;
    }
    return absolute_weight_g;
}

String extract_json_value(const String& body, const char* key) {
    if (body.isEmpty() || !key) {
        return "";
    }

    String needle = "\"";
    needle += key;
    needle += "\"";
    int key_pos = body.indexOf(needle);
    if (key_pos < 0) {
        return "";
    }

    int colon_pos = body.indexOf(':', key_pos + needle.length());
    if (colon_pos < 0) {
        return "";
    }

    int value_start = colon_pos + 1;
    while (value_start < static_cast<int>(body.length()) &&
           isspace(static_cast<unsigned char>(body[value_start]))) {
        value_start++;
    }

    if (value_start >= static_cast<int>(body.length())) {
        return "";
    }

    if (body[value_start] == '"') {
        String value;
        for (int i = value_start + 1; i < static_cast<int>(body.length()); ++i) {
            char c = body[i];
            if (c == '\\' && i + 1 < static_cast<int>(body.length())) {
                value += body[++i];
                continue;
            }
            if (c == '"') {
                return value;
            }
            value += c;
        }
        return value;
    }

    int value_end = value_start;
    while (value_end < static_cast<int>(body.length()) &&
           body[value_end] != ',' && body[value_end] != '}') {
        value_end++;
    }

    String value = body.substring(value_start, value_end);
    value.trim();
    return value;
}

bool parse_bool_value(String value, bool fallback) {
    value.trim();
    value.toLowerCase();
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    return fallback;
}

const char* reset_reason_label(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_UNKNOWN:
        default: return "UNKNOWN";
    }
}

} // namespace

ConnectivityManager::ConnectivityManager()
    : preferences_(nullptr)
    , hardware_manager_(nullptr)
    , bean_controller_(nullptr)
    , server_(WIFI_HTTP_PORT)
    , ui_status_queue_(nullptr)
    , state_(ConnectivityState::WIFI_DISABLED)
    , enabled_(false)
    , server_started_(false)
    , routes_registered_(false)
    , setup_ap_active_(false)
    , sta_connecting_(false)
    , ota_in_progress_(false)
    , ota_error_(false)
    , screensaver_upload_error_(false)
    , restart_pending_(false)
    , settings_changed_(false)
    , ota_received_bytes_(0)
    , ota_total_bytes_(0)
    , screensaver_received_bytes_(0)
    , last_reconnect_attempt_ms_(0)
    , sta_connect_started_ms_(0)
    , restart_at_ms_(0) {
}

ConnectivityManager::~ConnectivityManager() {
    if (screensaver_upload_file_) {
        screensaver_upload_file_.close();
    }
    disable();
}

void ConnectivityManager::init(Preferences* prefs, HardwareManager* hardware, BeanController* beans) {
    preferences_ = prefs;
    hardware_manager_ = hardware;
    bean_controller_ = beans;
    if (!ui_status_queue_) {
        ui_status_queue_ = xQueueCreate(8, sizeof(ConnectivityUIStatusMessage));
    }
    LOG_BLE("Connectivity: WiFi manager initialized\n");
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

    // Kick off a non-blocking station connect. handle() polls the result and
    // falls back to the setup AP if the connection times out. If there are no
    // stored credentials we go straight to the setup AP.
    if (!begin_station_connect()) {
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
    sta_connecting_ = false;
    set_state(ConnectivityState::WIFI_DISABLED, "WiFi disabled");
}

// Start a station connection without blocking. The actual association happens
// on the Wi-Fi stack's own task; poll_station_connect() (called from handle())
// finalizes the connection or falls back to the setup AP on timeout. This keeps
// the single-threaded HTTP server responsive instead of freezing it for up to
// WIFI_CONNECT_TIMEOUT_MS while WiFi associates/reconnects.
bool ConnectivityManager::begin_station_connect() {
    String ssid;
    String password;
    if (!load_credentials(ssid, password)) {
        LOG_BLE("WiFi: No stored credentials, starting setup AP\n");
        return false;
    }

    sta_ssid_ = ssid;
    setup_ap_active_ = false;
    set_state(ConnectivityState::WIFI_CONNECTING, "Connecting to WiFi...");

    MDNS.end();
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(ssid.c_str(), password.c_str());

    sta_connecting_ = true;
    sta_connect_started_ms_ = millis();
    last_reconnect_attempt_ms_ = sta_connect_started_ms_;
    return true;
}

void ConnectivityManager::poll_station_connect() {
    if (!sta_connecting_) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        sta_connecting_ = false;
        LOG_BLE("WiFi: Connected to %s, IP %s\n",
                sta_ssid_.c_str(), WiFi.localIP().toString().c_str());
        start_mdns();
        start_http_server(true);
        set_state(ConnectivityState::WIFI_CONNECTED, "WiFi connected");
        return;
    }

    if (millis() - sta_connect_started_ms_ >= WIFI_CONNECT_TIMEOUT_MS) {
        sta_connecting_ = false;
        LOG_BLE("WiFi: Failed to connect to %s, starting setup AP\n", sta_ssid_.c_str());
        WiFi.disconnect(false);
        start_setup_ap();
    }
}

bool ConnectivityManager::start_setup_ap() {
    setup_ap_active_ = true;
    sta_ssid_ = "";

    MDNS.end();
    WiFi.disconnect(true, true);
    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                      IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));

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

    start_http_server(true);
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

    static const char* header_keys[] = {
        "Content-Type",
        "X-Firmware-Version",
    };
    server_.collectHeaders(header_keys, sizeof(header_keys) / sizeof(header_keys[0]));

    server_.on(WIFI_SETUP_PATH, HTTP_GET, [this]() { handle_root(); });
    server_.on("/generate_204", HTTP_GET, [this]() { handle_root(); });
    server_.on("/gen_204", HTTP_GET, [this]() { handle_root(); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this]() { handle_root(); });
    server_.on("/library/test/success.html", HTTP_GET, [this]() { handle_root(); });
    server_.on("/connecttest.txt", HTTP_GET, [this]() { handle_root(); });
    server_.on("/ncsi.txt", HTTP_GET, [this]() { handle_root(); });
    server_.on("/ping", HTTP_GET, [this]() { send_plain_response(200, "ok"); });
    server_.on(WIFI_STATUS_PATH, HTTP_GET, [this]() { handle_status(); });
    server_.on(WIFI_API_STATUS_PATH, HTTP_GET, [this]() { handle_status(); });
    server_.on(WIFI_API_SETTINGS_PATH, HTTP_GET, [this]() { handle_settings_get(); });
    server_.on(WIFI_API_SETTINGS_PATH, HTTP_POST, [this]() { handle_settings_post(); });
    server_.on(WIFI_API_BEANS_PATH, HTTP_GET, [this]() { handle_beans_get(); });
    server_.on(WIFI_API_BEANS_PATH, HTTP_POST, [this]() { handle_beans_post(); });
    server_.on(WIFI_API_SCREENSAVER_PATH, HTTP_GET, [this]() { handle_screensaver_status(); });
    server_.on(WIFI_API_SCREENSAVER_PATH, HTTP_POST,
               [this]() { handle_screensaver_complete(); },
               [this]() { handle_screensaver_upload(); });
    server_.on(WIFI_API_SCREENSAVER_CLEAR_PATH, HTTP_POST, [this]() { handle_screensaver_clear(); });
    server_.on(WIFI_API_BASKET_CAPTURE_SINGLE_PATH, HTTP_POST, [this]() { handle_basket_capture(true); });
    server_.on(WIFI_API_BASKET_CAPTURE_DOUBLE_PATH, HTTP_POST, [this]() { handle_basket_capture(false); });
    server_.on("/wifi", HTTP_POST, [this]() { handle_wifi_save(); });
    server_.on("/wifi/clear", HTTP_POST, [this]() { handle_wifi_clear(); });
    server_.on(WIFI_OTA_PATH, HTTP_GET, [this]() { handle_ota_page(); });
    server_.on(WIFI_OTA_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on("/ping", HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_STATUS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_STATUS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_SETTINGS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_BEANS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_SCREENSAVER_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_SCREENSAVER_CLEAR_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_BASKET_CAPTURE_SINGLE_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_BASKET_CAPTURE_DOUBLE_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on("/wifi", HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on("/wifi/clear", HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_OTA_PATH, HTTP_POST,
               [this]() { handle_ota_complete(); },
               [this]() { handle_ota_upload(); });
    server_.onNotFound([this]() { handle_not_found(); });

    routes_registered_ = true;
}

void ConnectivityManager::start_http_server(bool restart) {
    register_routes();
    if (restart && server_started_) {
        server_.stop();
        server_started_ = false;
        delay(20);
    }
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

    // Drive an in-flight station connect to completion without blocking.
    if (sta_connecting_) {
        poll_station_connect();
        return;
    }

    if (!setup_ap_active_ && !ota_in_progress_ && WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - last_reconnect_attempt_ms_ >= WIFI_RECONNECT_INTERVAL_MS) {
            LOG_BLE("WiFi: Connection lost, retrying station connection\n");
            if (!begin_station_connect()) {
                start_setup_ap();
            }
        }
    }
}

void ConnectivityManager::send_cors_headers() {
    server_.sendHeader("Access-Control-Allow-Origin", "*");
    server_.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server_.sendHeader("Access-Control-Allow-Headers", "Content-Type,Accept,X-Firmware-Version");
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

void ConnectivityManager::send_html_response(const char* body) {
    if (!body) {
        send_plain_response(500, "Missing page");
        return;
    }

    constexpr size_t kChunkSize = 1024;
    const size_t length = strlen(body);
    send_cors_headers();
    server_.setContentLength(length);
    server_.send(200, "text/html; charset=utf-8", "");

    for (size_t offset = 0; offset < length; offset += kChunkSize) {
        const size_t remaining = length - offset;
        const size_t chunk = remaining > kChunkSize ? kChunkSize : remaining;
        server_.sendContent(body + offset, chunk);
        delay(0);
    }
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

String ConnectivityManager::json_escape(const String& value) const {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<uint8_t>(c) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    escaped += buf;
                } else {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

void ConnectivityManager::handle_root() {
    if (setup_ap_active_) {
        handle_setup_recovery_root();
        return;
    }

    static const char body[] = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GrindByWeight WiFi</title>
<style>
:root{color-scheme:dark;--bg:#101010;--panel:#1b1b1b;--muted:#a8a8a8;--line:#323232;--red:#d71920;--blue:#00aaff;--green:#2aa84a;--text:#f5f5f5;--soft:#242424}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Arial,sans-serif}main{max-width:760px;margin:0 auto;padding:22px}h1{margin:0 0 18px;font-size:30px}h2{margin:0 0 6px;font-size:21px}section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:16px;margin:14px 0}form{margin:0}.section-note,.hint{color:var(--muted);font-size:14px;line-height:1.4}.section-note{margin:0 0 12px}.hint{margin:4px 0 10px}.field{padding:12px 0;border-top:1px solid var(--line)}.field:first-child{border-top:0}.field label,.field-title{display:flex;justify-content:space-between;gap:12px;margin:0;color:var(--text);font-weight:700}.settings-grid{display:grid;grid-template-columns:1fr 1fr;gap:0 18px}.wide{grid-column:1/-1}.toggle-list{display:grid;grid-template-columns:1fr 1fr;gap:4px 18px}.check{display:flex;align-items:flex-start;gap:10px;color:var(--text);margin:0;padding:10px 0}.check input{width:auto;margin:3px 0 0;flex:0 0 auto}.check strong{display:block}.check small{display:block;color:var(--muted);font-size:13px;line-height:1.35;margin-top:3px}input,select,button{width:100%;font:inherit;border:0;border-radius:6px;padding:11px;margin:0 0 10px}input,select{background:#0b0b0b;color:var(--text);border:1px solid var(--line)}input[type=range]{height:36px;padding:0;accent-color:var(--blue)}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.actions{display:flex;gap:10px;flex-wrap:wrap}.actions button{flex:1 1 160px}button{background:var(--red);color:#fff;font-weight:700;cursor:pointer}.secondary{background:#333}.ok{color:var(--green)}.warn{color:#f2a12b}.muted{color:var(--muted)}dl{display:grid;grid-template-columns:135px 1fr;gap:8px;margin:0}dt{color:var(--muted)}dd{margin:0;word-break:break-word}.value{white-space:nowrap;color:var(--muted);font-weight:400}.bean-list{display:grid;gap:10px;margin:12px 0}.bean{background:var(--soft);border:1px solid var(--line);border-radius:8px;padding:12px}.bean.active{border-color:var(--blue);box-shadow:0 0 0 1px var(--blue)}.bean-head{display:flex;justify-content:space-between;gap:12px;font-weight:700}.bean-sub{color:var(--muted);font-size:13px;margin:5px 0 10px}.bean-stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;font-size:13px;color:var(--muted)}.bean-stats strong{display:block;color:var(--text);font-size:16px}.bean-actions{display:flex;gap:8px;margin-top:12px}.bean-actions button{padding:9px;margin:0}@media(max-width:620px){main{padding:14px}.row,.settings-grid,.toggle-list{grid-template-columns:1fr}.bean-stats{grid-template-columns:1fr}dl{grid-template-columns:1fr}dt{margin-top:8px}}
</style>
</head>
<body>
<main>
<h1>GrindByWeight</h1>

<section>
<h2>WiFi Status</h2>
<dl id="statusList"><dt>Status</dt><dd>Loading...</dd></dl>
<p class="muted" id="screensaverState"></p>
</section>

<section>
<h2>Coffee Beans</h2>
<p class="section-note">Manage beans stored on the grinder. Single and Double can use different grind sizes.</p>
<div id="beanList" class="bean-list"></div>
<form id="beanForm">
<input id="bean_id" type="hidden">
<div class="row">
<div><label>Name</label><input id="bean_name" maxlength="32" placeholder="Bean name"></div>
<div><label>Roaster</label><input id="bean_roaster" maxlength="24" placeholder="Roaster"></div>
</div>
<div class="row">
<div><label>Bag Size (g)</label><input id="bean_bag_size_g" type="number" min="0" max="5000" step="1" value="250"></div>
<div><label>Single Grind Size</label><input id="bean_mahlgrad_single" type="number" min="1" max="50" step="0.5" value="25"></div>
</div>
<div class="row">
<div><label>Double Grind Size</label><input id="bean_mahlgrad" type="number" min="1" max="50" step="0.5" value="25"></div>
<div></div>
</div>
<div class="actions"><button type="submit">Save Bean</button><button id="newBean" class="secondary" type="button">New Bean</button></div>
<p id="beanMessage" class="muted"></p>
</form>
</section>

<section>
<h2>Network</h2>
<p class="section-note">Save the WiFi network the grinder should join. Clearing credentials reboots into setup access-point mode.</p>
<form method="post" action="/wifi">
<label>SSID</label><input name="ssid" id="ssid" required>
<label>Password</label><input name="password" type="password" autocomplete="current-password">
<button type="submit">Save WiFi</button>
</form>
<form method="post" action="/wifi/clear"><button class="secondary" type="submit">Clear WiFi</button></form>
</section>

<section>
<h2>Device Settings</h2>
<p class="section-note">These settings are saved on the grinder and apply to both the touchscreen and WiFi controls.</p>
<form id="settingsForm">
<div class="settings-grid">
<div class="field"><label>Grind Mode</label><p class="hint">Weight mode stops at the saved gram target. Time mode runs each profile for its saved seconds.</p><select id="grind_mode" name="grind_mode"><option value="0">Weight</option><option value="1">Time</option></select></div>
<div class="field"><label>Purge Mode</label><p class="hint">Keep continues after priming. Remove pauses after priming so stale grinds can be discarded before the dose.</p><select id="purge_mode" name="purge_mode"><option value="0">Keep purge grinds</option><option value="1">Remove purge grinds</option></select></div>
<div class="field"><label>Purge Amount <span class="value" id="purge_amount_g_value"></span></label><p class="hint">Minimum coffee used to fill the chute before a weight-mode dose starts.</p><input id="purge_amount_g" name="purge_amount_g" type="range" min="0.1" max="2.5" step="0.1"></div>
<div class="field"><label>Freshness <span class="value" id="freshness_hours_value"></span></label><p class="hint">How long previous grinds are treated as fresh before the purge confirmation appears.</p><input id="freshness_hours" name="freshness_hours" type="range" min="0.5" max="48" step="0.5"></div>
<div class="field wide"><label>Grind Coast <span class="value" id="coast_ratio_value"></span></label><p class="hint">Prediction for coffee that keeps falling after the motor stops. Higher stops earlier to reduce overshoot; lower stops later if shots tend to undershoot.</p><input id="coast_ratio" name="coast_ratio" type="range" min="0.7" max="1.5" step="0.05"></div>
<div class="field"><label>Brightness <span class="value" id="brightness_normal_value"></span></label><p class="hint">Normal touchscreen brightness while you are using the grinder.</p><input id="brightness_normal" name="brightness_normal" type="range" min="0.15" max="1" step="0.01"></div>
<div class="field"><label>Screensaver Brightness <span class="value" id="brightness_screensaver_value"></span></label><p class="hint">Dimmed brightness used after inactivity and while the screensaver image is shown.</p><input id="brightness_screensaver" name="brightness_screensaver" type="range" min="0.15" max="1" step="0.01"></div>
<div class="field"><label>Screensaver After <span class="value" id="screensaver_sleep_delay_min_value"></span></label><p class="hint">Inactivity time before the screen dims and the sleep screensaver can appear.</p><input id="screensaver_sleep_delay_min" name="screensaver_sleep_delay_min" type="range" min="1" max="60" step="1"></div>
<div class="field"><label>Startup Duration <span class="value" id="screensaver_startup_duration_s_value"></span></label><p class="hint">How long the uploaded screensaver image stays visible during boot.</p><input id="screensaver_startup_duration_s" name="screensaver_startup_duration_s" type="range" min="1" max="30" step="1"></div>
</div>
<div class="field wide">
<div class="field-title">Toggles</div>
<p class="hint">Quick behavior switches. Auto Start can optionally be paired with basket detection from the touchscreen menu.</p>
<div class="toggle-list">
<label class="check"><input id="wifi_startup" name="wifi_startup" type="checkbox"><span><strong>Start WiFi on boot</strong><small>Connect automatically after startup.</small></span></label>
<label class="check"><input id="advanced_ui" name="advanced_ui" type="checkbox"><span><strong>Advanced touchscreen UI</strong><small>Use the bean tracking ready screen; off restores the classic profile pages.</small></span></label>
<label class="check"><input id="swipe_enabled" name="swipe_enabled" type="checkbox"><span><strong>Swipe mode switching</strong><small>Allow vertical swipes between Weight and Time modes.</small></span></label>
<label class="check"><input id="auto_start" name="auto_start" type="checkbox"><span><strong>Start on cup</strong><small>Start the active profile when a cup or portafilter lands on the scale.</small></span></label>
<label class="check"><input id="auto_return" name="auto_return" type="checkbox"><span><strong>Return on cup removal</strong><small>Leave the completion screen when the finished cup is removed.</small></span></label>
<label class="check"><input id="logging_enabled" name="logging_enabled" type="checkbox"><span><strong>Grind logging</strong><small>Save grind data for later diagnostics and analysis.</small></span></label>
<label class="check"><input id="screensaver_startup" name="screensaver_startup" type="checkbox"><span><strong>Show screensaver on startup</strong><small>Display the uploaded image while booting.</small></span></label>
<label class="check"><input id="screensaver_sleep" name="screensaver_sleep" type="checkbox"><span><strong>Show screensaver when dimmed</strong><small>Use the uploaded image after inactivity.</small></span></label>
</div>
</div>
<div class="field wide">
<div class="field-title">Basket Detection</div>
<p class="hint">Optional Auto Start helper. Store the empty Single and Double basket weights; a matched basket selects that profile before grinding. Ambiguous or unmatched weights will not start.</p>
<label class="check"><input id="basket_detect" name="basket_detect" type="checkbox"><span><strong>Detect Basket</strong><small>Choose Single or Double by the settled basket weight.</small></span></label>
<div class="row">
<div><label>Single Basket</label><p class="hint">Saved absolute scale reading for the single basket or portafilter.</p><input id="basket_single_g" name="basket_single_g" type="number" min="0" max="1000" step="0.1"></div>
<div><label>Double Basket</label><p class="hint">Saved absolute scale reading for the double basket or portafilter.</p><input id="basket_double_g" name="basket_double_g" type="number" min="0" max="1000" step="0.1"></div>
</div>
<div class="actions"><button id="captureSingleBasket" type="button" class="secondary">Capture Single</button><button id="captureDoubleBasket" type="button" class="secondary">Capture Double</button></div>
<label>Basket Tolerance <span class="value" id="basket_tolerance_g_value"></span></label><p class="hint">Allowed weight difference around each stored basket. Keep it narrow enough that Single and Double do not overlap.</p><input id="basket_tolerance_g" name="basket_tolerance_g" type="range" min="1" max="30" step="1">
<p id="basketMessage" class="muted"></p>
</div>
<button type="submit">Save Settings</button>
<p id="settingsMessage" class="muted"></p>
</form>
</section>

<section>
<h2>Screensaver Image</h2>
<p class="section-note">Upload one image for startup and dimmed-screen display. Timing and brightness are controlled in Device Settings.</p>
<input id="screensaverFile" type="file" accept="image/*">
<div class="actions"><button id="uploadScreensaver" type="button">Upload Image</button><button id="clearScreensaver" class="secondary" type="button">Clear Image</button></div>
<p id="imageMessage" class="muted">Images are resized to the device display before upload.</p>
</section>

<section>
<h2>OTA Update</h2>
<p class="section-note">Upload a firmware .bin directly to this grinder. The device restarts when the update is applied.</p>
<form method="post" action="/ota" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<button type="submit">Upload Firmware</button>
</form>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
const W=280,H=456;
function pct(v){return Math.round(Number(v)*100)+"%"}
function setMsg(id,text,cls="muted"){const el=$(id);el.textContent=text;el.className=cls}
async function json(path,opts){const r=await fetch(path,opts);const t=await r.text();let data={};try{data=t?JSON.parse(t):{}}catch(e){throw new Error(t||r.statusText)}if(!r.ok)throw new Error(data.error||t||r.statusText);return data}
function esc(v){return String(v??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]))}
function renderStatus(s,prefillWifi=false){
  const rows=[["State",s.status],["Mode",s.mode],["SSID",s.ssid||"--"],["IP",s.ip||"--"],["Host",s.host_url||"--"],["MAC",s.mac||"--"],["RSSI",s.connected?(s.rssi_dbm+" dBm"):"--"],["OTA",s.ota_active?(s.ota_progress+"%"):(s.ota_url||"--")],["Build","#"+s.build],["Version",s.version]];
  $("statusList").innerHTML=rows.map(r=>"<dt>"+esc(r[0])+"</dt><dd>"+esc(r[1])+"</dd>").join("");
  if(prefillWifi)$("ssid").value=s.setup_ap?"":(s.ssid||"");
}
function bindRange(id,fmt){const el=$(id),out=$(id+"_value");const upd=()=>out.textContent=fmt(el.value);el.addEventListener("input",upd);upd()}
function renderSettings(x){
  const s=x.settings||x;
  $("wifi_startup").checked=!!s.wifi_startup;$("advanced_ui").checked=!!s.advanced_ui;$("logging_enabled").checked=!!s.logging_enabled;$("swipe_enabled").checked=!!s.swipe_enabled;$("auto_start").checked=!!s.auto_start;$("auto_return").checked=!!s.auto_return;
  $("basket_detect").checked=!!s.basket_detect;$("basket_single_g").value=Number(s.basket_single_g||0).toFixed(1);$("basket_double_g").value=Number(s.basket_double_g||0).toFixed(1);$("basket_tolerance_g").value=s.basket_tolerance_g;
  $("screensaver_startup").checked=!!s.screensaver_startup;$("screensaver_sleep").checked=!!s.screensaver_sleep;
  $("grind_mode").value=String(s.grind_mode_index);$("purge_mode").value=String(s.purge_mode_index);
  $("purge_amount_g").value=s.purge_amount_g;$("freshness_hours").value=s.freshness_hours;$("coast_ratio").value=s.coast_ratio;$("brightness_normal").value=s.brightness_normal;$("brightness_screensaver").value=s.brightness_screensaver;
  $("screensaver_sleep_delay_min").value=s.screensaver_sleep_delay_min;$("screensaver_startup_duration_s").value=s.screensaver_startup_duration_s;
  ["purge_amount_g","freshness_hours","coast_ratio","brightness_normal","brightness_screensaver","screensaver_sleep_delay_min","screensaver_startup_duration_s","basket_tolerance_g"].forEach(id=>$(id).dispatchEvent(new Event("input")));
  $("basketMessage").textContent=s.basket_configured?"Basket detection configured":"Capture or enter both basket weights";
  $("screensaverState").textContent=s.screensaver_image?"Screensaver image stored: "+s.screensaver_image_bytes+" bytes":"No screensaver image stored";
}
function clearBeanForm(clearMessage=true){$("bean_id").value="";$("bean_name").value="";$("bean_roaster").value="";$("bean_bag_size_g").value=250;$("bean_mahlgrad_single").value=25;$("bean_mahlgrad").value=25;if(clearMessage)setMsg("beanMessage","")}
function editBean(b){$("bean_id").value=b.id;$("bean_name").value=b.name||"";$("bean_roaster").value=b.roaster||"";$("bean_bag_size_g").value=b.bag_size_g||0;$("bean_mahlgrad_single").value=Number(b.mahlgrad_single??b.mahlgrad??25).toFixed(1);$("bean_mahlgrad").value=Number(b.mahlgrad_double??b.mahlgrad??25).toFixed(1);setMsg("beanMessage","Editing "+(b.name||"bean"))}
function renderBeans(data){
  const beans=data.beans||[];
  $("beanList").innerHTML=beans.length?beans.map(b=>`<div class="bean ${b.active?"active":""}"><div class="bean-head"><span>${esc(b.name||"Unnamed bean")}</span><span>S ${Number(b.mahlgrad_single??b.mahlgrad??25).toFixed(1).replace(".0","")} / D ${Number(b.mahlgrad_double??b.mahlgrad??25).toFixed(1).replace(".0","")}</span></div><div class="bean-sub">${esc(b.roaster||"No roaster")}</div><div class="bean-stats"><span><strong>${Number(b.dose_used_g||0).toFixed(1)}g</strong>Dose</span><span><strong>${Number(b.purge_used_g||0).toFixed(1)}g</strong>Purge</span><span><strong>${Number(b.total_used_g||0).toFixed(1)}g</strong>Total</span></div><div class="bean-actions"><button type="button" class="secondary" data-act="edit" data-id="${b.id}">Edit</button><button type="button" class="secondary" data-act="active" data-id="${b.id}">${b.active?"Active":"Set Active"}</button><button type="button" class="secondary" data-act="delete" data-id="${b.id}">Delete</button></div></div>`).join(""):`<p class="muted">No beans stored yet. Add the first bean below.</p>`;
  $("beanList").querySelectorAll("button").forEach(btn=>btn.addEventListener("click",async()=>{
    const id=btn.dataset.id,act=btn.dataset.act,b=beans.find(x=>String(x.id)===String(id));
    if(act==="edit"){editBean(b);return}
    try{
      if(act==="active")await json("/api/beans",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({action:"set_active",id})});
      if(act==="delete")await json("/api/beans",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({action:"delete",id})});
      setMsg("beanMessage",act==="delete"?"Bean deleted":"Active bean updated","ok");await loadBeans()
    }catch(e){setMsg("beanMessage",e.message,"warn")}
  }));
}
async function loadBeans(){renderBeans(await json("/api/beans"))}
async function refreshStatus(){renderStatus(await json("/api/status"))}
async function load(){try{renderStatus(await json("/api/status"),true);renderSettings(await json("/api/settings"));await loadBeans()}catch(e){setMsg("settingsMessage",e.message,"warn")}}
function settingsPayload(){
  const p=new URLSearchParams();
  ["wifi_startup","advanced_ui","logging_enabled","swipe_enabled","auto_start","auto_return","basket_detect","screensaver_startup","screensaver_sleep"].forEach(id=>p.set(id,$(id).checked?"1":"0"));
  ["grind_mode","purge_mode","purge_amount_g","freshness_hours","coast_ratio","brightness_normal","brightness_screensaver","screensaver_sleep_delay_min","screensaver_startup_duration_s","basket_single_g","basket_double_g","basket_tolerance_g"].forEach(id=>p.set(id,$(id).value));
  return p;
}
$("settingsForm").addEventListener("submit",async e=>{e.preventDefault();setMsg("settingsMessage","Saving...");try{await json("/api/settings",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:settingsPayload()});setMsg("settingsMessage","Settings saved","ok");await load()}catch(err){setMsg("settingsMessage",err.message,"warn")}});
$("newBean").addEventListener("click",clearBeanForm);
$("beanForm").addEventListener("submit",async e=>{e.preventDefault();setMsg("beanMessage","Saving...");const id=$("bean_id").value;const body=new URLSearchParams({action:id?"update":"create",name:$("bean_name").value,roaster:$("bean_roaster").value,bag_size_g:$("bean_bag_size_g").value,mahlgrad_single:$("bean_mahlgrad_single").value,mahlgrad:$("bean_mahlgrad").value,mahlgrad_double:$("bean_mahlgrad").value});if(id)body.set("id",id);try{await json("/api/beans",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});setMsg("beanMessage","Bean saved","ok");clearBeanForm(false);await loadBeans()}catch(err){setMsg("beanMessage",err.message,"warn")}});
async function captureBasket(kind){setMsg("basketMessage","Capturing "+kind+" basket...");try{const data=await json("/api/basket/capture/"+kind,{method:"POST"});renderSettings(data);const s=data.settings||data;const key=kind==="single"?"basket_single_g":"basket_double_g";setMsg("basketMessage","Captured "+kind+": "+Number(s[key]||0).toFixed(1)+"g","ok")}catch(e){setMsg("basketMessage",e.message,"warn")}}
$("captureSingleBasket").addEventListener("click",()=>captureBasket("single"));
$("captureDoubleBasket").addEventListener("click",()=>captureBasket("double"));
async function imageToRgb565(file){
  const img=new Image();const url=URL.createObjectURL(file);
  await new Promise((res,rej)=>{img.onload=res;img.onerror=rej;img.src=url});
  const c=document.createElement("canvas");c.width=W;c.height=H;const ctx=c.getContext("2d");
  ctx.fillStyle="#000";ctx.fillRect(0,0,W,H);
  const scale=Math.max(W/img.width,H/img.height),dw=img.width*scale,dh=img.height*scale;
  ctx.drawImage(img,(W-dw)/2,(H-dh)/2,dw,dh);URL.revokeObjectURL(url);
  const src=ctx.getImageData(0,0,W,H).data,out=new Uint8Array(W*H*2);
  for(let i=0,j=0;i<src.length;i+=4,j+=2){const v=((src[i]&248)<<8)|((src[i+1]&252)<<3)|(src[i+2]>>3);out[j]=v&255;out[j+1]=v>>8}
  return out;
}
$("uploadScreensaver").addEventListener("click",async()=>{const f=$("screensaverFile").files[0];if(!f){setMsg("imageMessage","Choose an image first","warn");return}setMsg("imageMessage","Converting image...");try{const raw=await imageToRgb565(f);const form=new FormData();form.append("image",new Blob([raw],{type:"application/octet-stream"}),"screensaver.rgb565");setMsg("imageMessage","Uploading...");await json("/api/screensaver",{method:"POST",body:form});setMsg("imageMessage","Screensaver image saved","ok");load()}catch(e){setMsg("imageMessage",e.message,"warn")}});
$("clearScreensaver").addEventListener("click",async()=>{try{await json("/api/screensaver/clear",{method:"POST"});setMsg("imageMessage","Screensaver image cleared","ok");load()}catch(e){setMsg("imageMessage",e.message,"warn")}});
bindRange("purge_amount_g",v=>Number(v).toFixed(1)+"g");bindRange("freshness_hours",v=>Number(v).toFixed(1)+"h");bindRange("coast_ratio",v=>Math.round(Number(v)*100)+"%");bindRange("brightness_normal",pct);bindRange("brightness_screensaver",pct);bindRange("screensaver_sleep_delay_min",v=>Number(v).toFixed(0)+" min");bindRange("screensaver_startup_duration_s",v=>Number(v).toFixed(0)+" s");bindRange("basket_tolerance_g",v=>"±"+Number(v).toFixed(0)+"g");
load();setInterval(()=>refreshStatus().catch(()=>{}),5000);
</script>
</body>
</html>)HTML";

    send_html_response(body);
}

void ConnectivityManager::handle_setup_recovery_root() {
    send_cors_headers();

    String html;
    html.reserve(2300);
    html += F("<!doctype html><html><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>GrindByWeight Recovery</title>"
              "<style>"
              ":root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#101010;color:#f4f4f4;font-family:Arial,sans-serif}"
              "main{max-width:520px;margin:0 auto;padding:18px}h1{font-size:26px;margin:0 0 8px}h2{font-size:19px;margin:0 0 8px}"
              "section{border:1px solid #333;background:#1a1a1a;border-radius:8px;padding:14px;margin:12px 0}"
              "p,small{color:#aaa;line-height:1.4}label{display:block;font-weight:700;margin:10px 0 5px}"
              "input,button{width:100%;font:inherit;border:0;border-radius:6px;padding:12px;margin:0 0 10px}"
              "input{background:#080808;color:#fff;border:1px solid #444}button{background:#d71920;color:#fff;font-weight:700}"
              "dl{display:grid;grid-template-columns:110px 1fr;gap:7px;margin:0}dt{color:#aaa}dd{margin:0;word-break:break-word}"
              ".secondary{background:#333}.warn{color:#f6b24a}.ok{color:#24d060}"
              "</style></head><body><main><h1>GrindByWeight Recovery</h1>"
              "<p>Setup access point mode is active. Use this page to restore WiFi or flash firmware over USB-free OTA.</p>"
              "<section><h2>Status</h2><dl>");
    html += F("<dt>AP SSID</dt><dd>");
    html += html_escape(WIFI_SETUP_AP_SSID);
    html += F("</dd><dt>Address</dt><dd>http://");
    html += WiFi.softAPIP().toString();
    html += F("</dd><dt>Build</dt><dd>#");
    html += String(BUILD_NUMBER);
    html += F("</dd><dt>Firmware</dt><dd>");
    html += html_escape(BUILD_FIRMWARE_VERSION);
    html += F("</dd><dt>MAC</dt><dd>");
    html += html_escape(WiFi.softAPmacAddress());
    html += F("</dd></dl></section>"
              "<section><h2>WiFi</h2>"
              "<form method=\"post\" action=\"/wifi\">"
              "<label>Network name</label><input name=\"ssid\" required autocomplete=\"off\">"
              "<label>Password</label><input name=\"password\" type=\"password\" autocomplete=\"current-password\">"
              "<button type=\"submit\">Save WiFi and Restart</button>"
              "</form>"
              "<form method=\"post\" action=\"/wifi/clear\"><button class=\"secondary\" type=\"submit\">Clear WiFi Credentials</button></form>"
              "</section>"
              "<section><h2>Firmware OTA</h2>"
              "<form method=\"post\" action=\"/ota\" enctype=\"multipart/form-data\">"
              "<input type=\"file\" name=\"firmware\" accept=\".bin,application/octet-stream\" required>"
              "<button type=\"submit\">Upload Firmware</button>"
              "</form><small>Use the production firmware .bin, not a mock build.</small>"
              "</section>"
              "<section><h2>Diagnostics</h2><p><a class=\"ok\" href=\"/ping\">Ping</a> &nbsp; "
              "<a class=\"ok\" href=\"/api/status\">Status JSON</a></p></section>"
              "</main></body></html>");

    server_.send(200, "text/html; charset=utf-8", html);
}

String ConnectivityManager::build_status_json() const {
    String ip = get_ip_address();
    String host_url = "";
    if (!ip.isEmpty()) {
        host_url = setup_ap_active_ ? String("http://") + ip
                                    : String("http://") + WIFI_HOSTNAME + ".local";
    }
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    String json;
    json.reserve(1300);
    json += "{";
    json += "\"device\":\"" CONNECTIVITY_DEVICE_NAME "\",";
    json += "\"transport\":\"wifi\",";
    json += "\"hostname\":\"" WIFI_HOSTNAME "\",";
    json += "\"mode\":\"";
    json += get_mode_label();
    json += "\",";
    json += "\"version\":\"" BUILD_FIRMWARE_VERSION "\",";
    json += "\"build\":";
    json += BUILD_NUMBER;
    json += ",";
    json += "\"uptime_ms\":";
    json += String(millis());
    json += ",";
    json += "\"reset_reason\":\"";
    json += reset_reason_label(reset_reason);
    json += "\",";
    json += "\"reset_reason_code\":";
    json += String(static_cast<int>(reset_reason));
    json += ",";
    json += "\"free_heap_bytes\":";
    json += String(ESP.getFreeHeap());
    json += ",";
    json += "\"min_free_heap_bytes\":";
    json += String(ESP.getMinFreeHeap());
    json += ",";
    json += "\"max_alloc_heap_bytes\":";
    json += String(ESP.getMaxAllocHeap());
    json += ",";
    json += "\"free_internal_heap_bytes\":";
    json += String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    json += ",";
    json += "\"largest_internal_block_bytes\":";
    json += String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    json += ",";
    json += "\"psram_size_bytes\":";
    json += String(ESP.getPsramSize());
    json += ",";
    json += "\"free_psram_bytes\":";
    json += String(ESP.getFreePsram());
    json += ",";
    json += "\"cpu_mhz\":";
    json += String(getCpuFrequencyMhz());
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
    json += json_escape(get_active_ssid());
    json += "\",";
    json += "\"ip\":\"";
    json += json_escape(ip);
    json += "\",";
    json += "\"mac\":\"";
    json += json_escape(get_mac_address());
    json += "\",";
    json += "\"host_url\":\"";
    json += json_escape(host_url);
    json += "\",";
    json += "\"rssi_dbm\":";
    json += String(get_rssi_dbm());
    json += ",";
    json += "\"ota_url\":\"";
    json += json_escape(get_ota_url());
    json += "\",";
    json += "\"status\":\"";
    json += json_escape(get_status_label());
    json += "\",";
    json += "\"ota_active\":";
    json += ota_in_progress_ ? "true" : "false";
    json += ",";
    json += "\"ota_raw\":true,";
    json += "\"ota_progress\":";
    json += String(get_ota_progress(), 1);
    json += ",";
    json += "\"screensaver_image\":";
    json += has_screensaver_image() ? "true" : "false";
    json += ",";
    json += "\"screensaver_image_bytes\":";
    json += String(get_screensaver_image_size());
    json += "}";

    return json;
}

void ConnectivityManager::handle_status() {
    String json = build_status_json();
    send_json_response(200, json);
}

String ConnectivityManager::get_request_string(const char* key, bool& found) {
    found = false;
    if (!key) {
        return "";
    }

    if (server_.hasArg(key)) {
        found = true;
        return server_.arg(key);
    }

    if (server_.hasArg("plain")) {
        String value = extract_json_value(server_.arg("plain"), key);
        if (!value.isEmpty()) {
            found = true;
            return value;
        }
    }

    return "";
}

bool ConnectivityManager::get_request_bool(const char* key, bool current_value, bool& found) {
    String value = get_request_string(key, found);
    if (!found) {
        return current_value;
    }
    return parse_bool_value(value, current_value);
}

float ConnectivityManager::get_request_float(const char* key, float current_value, float min_value, float max_value, bool& found) {
    String value = get_request_string(key, found);
    if (!found) {
        return current_value;
    }
    return clamp_float(value.toFloat(), min_value, max_value);
}

int ConnectivityManager::get_request_int(const char* key, int current_value, int min_value, int max_value, bool& found) {
    String value = get_request_string(key, found);
    if (!found) {
        return current_value;
    }
    String lower = value;
    lower.trim();
    lower.toLowerCase();
    if (lower == "time" || lower == "purge" || lower == "remove") {
        return clamp_int(1, min_value, max_value);
    }
    if (lower == "weight" || lower == "prime" || lower == "keep") {
        return clamp_int(0, min_value, max_value);
    }
    return clamp_int(value.toInt(), min_value, max_value);
}

String ConnectivityManager::build_settings_json() const {
    bool wifi_startup = true;
    Preferences wifi_prefs;
    if (wifi_prefs.begin(WIFI_PREF_NAMESPACE, true)) {
        wifi_startup = wifi_prefs.getBool(WIFI_PREF_KEY_STARTUP, true);
        wifi_prefs.end();
    }

    float brightness_normal = USER_SCREEN_BRIGHTNESS_NORMAL;
    float brightness_screensaver = USER_SCREEN_BRIGHTNESS_DIMMED;
    Preferences brightness_prefs;
    if (brightness_prefs.begin("brightness", true)) {
        brightness_normal = brightness_prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
        brightness_screensaver = brightness_prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
        brightness_prefs.end();
    }

    bool logging_enabled = false;
    Preferences logging_prefs;
    if (logging_prefs.begin("logging", true)) {
        logging_enabled = logging_prefs.getBool("enabled", false);
        logging_prefs.end();
    }

    bool swipe_enabled = false;
    Preferences swipe_prefs;
    if (swipe_prefs.begin("swipe", true)) {
        swipe_enabled = swipe_prefs.getBool("enabled", false);
        swipe_prefs.end();
    }

    bool advanced_ui = USER_READY_UI_ADVANCED_DEFAULT;
    Preferences ready_ui_prefs;
    if (ready_ui_prefs.begin(USER_READY_UI_PREF_NAMESPACE, true)) {
        advanced_ui = ready_ui_prefs.getBool(USER_READY_UI_PREF_KEY_ADVANCED, USER_READY_UI_ADVANCED_DEFAULT);
        ready_ui_prefs.end();
    }

    bool auto_start = false;
    bool auto_return = false;
    Preferences auto_prefs;
    if (auto_prefs.begin("autogrind", true)) {
        auto_start = auto_prefs.getBool("auto_start", false);
        auto_return = auto_prefs.getBool("auto_return", false);
        auto_prefs.end();
    }

    bool screensaver_startup = false;
    bool screensaver_sleep = false;
    uint32_t screensaver_startup_duration_ms = USER_SCREENSAVER_STARTUP_DURATION_DEFAULT_MS;
    uint32_t screensaver_sleep_delay_ms = USER_SCREEN_AUTO_DIM_TIMEOUT_DEFAULT_MS;
    Preferences screensaver_prefs;
    if (screensaver_prefs.begin(USER_SCREENSAVER_PREF_NAMESPACE, true)) {
        screensaver_startup = screensaver_prefs.getBool(USER_SCREENSAVER_PREF_KEY_STARTUP, false);
        screensaver_sleep = screensaver_prefs.getBool(USER_SCREENSAVER_PREF_KEY_SLEEP, false);
        screensaver_startup_duration_ms = screensaver_prefs.getUInt(USER_SCREENSAVER_PREF_KEY_STARTUP_DURATION_MS,
                                                                    USER_SCREENSAVER_STARTUP_DURATION_DEFAULT_MS);
        screensaver_sleep_delay_ms = screensaver_prefs.getUInt(USER_SCREENSAVER_PREF_KEY_SLEEP_DELAY_MS,
                                                               USER_SCREEN_AUTO_DIM_TIMEOUT_DEFAULT_MS);
        screensaver_prefs.end();
    }

    int grind_mode_index = static_cast<int>(GrindMode::WEIGHT);
    int purge_mode_index = GRIND_PURGE_MODE_DEFAULT;
    float purge_amount_g = GRIND_PURGE_AMOUNT_DEFAULT_G;
    float freshness_hours = GRIND_FRESHNESS_DEFAULT_HOURS;
    float coast_ratio = GRIND_LATENCY_TO_COAST_RATIO_DEFAULT;
    bool basket_detect = false;
    float basket_single_g = 0.0f;
    float basket_double_g = 0.0f;
    float basket_tolerance_g = USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G;
    if (preferences_) {
        grind_mode_index = preferences_->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
        purge_mode_index = preferences_->getInt(GrindController::PREF_KEY_GRINDER_MODE, GRIND_PURGE_MODE_DEFAULT);
        purge_amount_g = preferences_->getFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, GRIND_PURGE_AMOUNT_DEFAULT_G);
        freshness_hours = preferences_->getFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, GRIND_FRESHNESS_DEFAULT_HOURS);
        coast_ratio = preferences_->getFloat(GrindController::PREF_KEY_COAST_RATIO, GRIND_LATENCY_TO_COAST_RATIO_DEFAULT);
        basket_detect = preferences_->getBool(BasketDetector::PREF_KEY_ENABLED, false);
        basket_single_g = sanitize_basket_weight(preferences_->getFloat(BasketDetector::PREF_KEY_SINGLE_WEIGHT_G, 0.0f));
        basket_double_g = sanitize_basket_weight(preferences_->getFloat(BasketDetector::PREF_KEY_DOUBLE_WEIGHT_G, 0.0f));
        basket_tolerance_g = preferences_->getFloat(BasketDetector::PREF_KEY_TOLERANCE_G, USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G);
    }

    grind_mode_index = clamp_int(grind_mode_index, 0, 1);
    purge_mode_index = clamp_int(purge_mode_index, 0, 1);
    purge_amount_g = clamp_float(purge_amount_g, GRIND_PURGE_AMOUNT_MIN_G, GRIND_PURGE_AMOUNT_MAX_G);
    freshness_hours = clamp_float(freshness_hours, 0.5f, 48.0f);
    coast_ratio = clamp_float(coast_ratio, GRIND_LATENCY_TO_COAST_RATIO_MIN, GRIND_LATENCY_TO_COAST_RATIO_MAX);
    basket_tolerance_g = clamp_float(basket_tolerance_g,
                                     USER_BASKET_DETECTION_TOLERANCE_MIN_G,
                                     USER_BASKET_DETECTION_TOLERANCE_MAX_G);
    brightness_normal = clamp_float(brightness_normal, 0.15f, 1.0f);
    brightness_screensaver = clamp_float(brightness_screensaver, 0.15f, 1.0f);
    screensaver_startup_duration_ms = clamp_int(screensaver_startup_duration_ms,
                                                USER_SCREENSAVER_STARTUP_DURATION_MIN_MS,
                                                USER_SCREENSAVER_STARTUP_DURATION_MAX_MS);
    screensaver_sleep_delay_ms = clamp_int(screensaver_sleep_delay_ms,
                                           USER_SCREEN_AUTO_DIM_TIMEOUT_MIN_MS,
                                           USER_SCREEN_AUTO_DIM_TIMEOUT_MAX_MS);

    String json;
    json.reserve(1400);
    json += "{\"ok\":true,\"settings\":{";
    json += "\"wifi_startup\":";
    json += wifi_startup ? "true" : "false";
    json += ",\"logging_enabled\":";
    json += logging_enabled ? "true" : "false";
    json += ",\"swipe_enabled\":";
    json += swipe_enabled ? "true" : "false";
    json += ",\"advanced_ui\":";
    json += advanced_ui ? "true" : "false";
    json += ",\"auto_start\":";
    json += auto_start ? "true" : "false";
    json += ",\"auto_return\":";
    json += auto_return ? "true" : "false";
    json += ",\"basket_detect\":";
    json += basket_detect ? "true" : "false";
    json += ",\"basket_configured\":";
    json += (basket_single_g > 0.0f && basket_double_g > 0.0f) ? "true" : "false";
    json += ",\"basket_single_g\":";
    json += String(basket_single_g, 1);
    json += ",\"basket_double_g\":";
    json += String(basket_double_g, 1);
    json += ",\"basket_tolerance_g\":";
    json += String(basket_tolerance_g, 0);
    json += ",\"screensaver_startup\":";
    json += screensaver_startup ? "true" : "false";
    json += ",\"screensaver_sleep\":";
    json += screensaver_sleep ? "true" : "false";
    json += ",\"grind_mode_index\":";
    json += String(grind_mode_index);
    json += ",\"grind_mode\":\"";
    json += grind_mode_index == static_cast<int>(GrindMode::TIME) ? "time" : "weight";
    json += "\",\"purge_mode_index\":";
    json += String(purge_mode_index);
    json += ",\"purge_mode\":\"";
    json += purge_mode_index == static_cast<int>(GrinderPurgeMode::PURGE) ? "purge" : "prime";
    json += "\",\"purge_amount_g\":";
    json += String(purge_amount_g, 1);
    json += ",\"freshness_hours\":";
    json += String(freshness_hours, 1);
    json += ",\"coast_ratio\":";
    json += String(coast_ratio, 2);
    json += ",\"brightness_normal\":";
    json += String(brightness_normal, 2);
    json += ",\"brightness_screensaver\":";
    json += String(brightness_screensaver, 2);
    json += ",\"screensaver_sleep_delay_min\":";
    json += String(static_cast<float>(screensaver_sleep_delay_ms) / 60000.0f, 0);
    json += ",\"screensaver_startup_duration_s\":";
    json += String(static_cast<float>(screensaver_startup_duration_ms) / 1000.0f, 0);
    json += ",\"screensaver_image\":";
    json += has_screensaver_image() ? "true" : "false";
    json += ",\"screensaver_image_bytes\":";
    json += String(get_screensaver_image_size());
    json += ",\"screensaver_format\":\"rgb565-";
    json += String(WIFI_SCREENSAVER_WIDTH_PX);
    json += "x";
    json += String(WIFI_SCREENSAVER_HEIGHT_PX);
    json += "\"}}";
    return json;
}

void ConnectivityManager::handle_settings_get() {
    send_json_response(200, build_settings_json());
}

void ConnectivityManager::handle_settings_post() {
    bool changed = false;
    bool found = false;
    String write_errors;

    Preferences wifi_prefs;
    if (wifi_prefs.begin(WIFI_PREF_NAMESPACE, false)) {
        bool current = wifi_prefs.getBool(WIFI_PREF_KEY_STARTUP, true);
        bool value = get_request_bool("wifi_startup", current, found);
        if (found) {
            changed = record_preference_write(wifi_prefs.putBool(WIFI_PREF_KEY_STARTUP, value),
                                              "WiFi startup", write_errors) || changed;
        }
        wifi_prefs.end();
    }

    Preferences logging_prefs;
    if (logging_prefs.begin("logging", false)) {
        bool current = logging_prefs.getBool("enabled", false);
        bool value = get_request_bool("logging_enabled", current, found);
        if (found) {
            changed = record_preference_write(logging_prefs.putBool("enabled", value),
                                              "grind logging", write_errors) || changed;
        }
        logging_prefs.end();
    }

    Preferences swipe_prefs;
    if (swipe_prefs.begin("swipe", false)) {
        bool current = swipe_prefs.getBool("enabled", false);
        bool value = get_request_bool("swipe_enabled", current, found);
        if (found) {
            changed = record_preference_write(swipe_prefs.putBool("enabled", value),
                                              "swipe mode switching", write_errors) || changed;
        }
        swipe_prefs.end();
    }

    Preferences ready_ui_prefs;
    if (ready_ui_prefs.begin(USER_READY_UI_PREF_NAMESPACE, false)) {
        bool current = ready_ui_prefs.getBool(USER_READY_UI_PREF_KEY_ADVANCED, USER_READY_UI_ADVANCED_DEFAULT);
        bool value = get_request_bool("advanced_ui", current, found);
        if (found) {
            changed = record_preference_write(ready_ui_prefs.putBool(USER_READY_UI_PREF_KEY_ADVANCED, value),
                                              "advanced ready UI", write_errors) || changed;
        }
        ready_ui_prefs.end();
    }

    Preferences auto_prefs;
    if (auto_prefs.begin("autogrind", false)) {
        bool current_start = auto_prefs.getBool("auto_start", false);
        bool start_value = get_request_bool("auto_start", current_start, found);
        if (found) {
            changed = record_preference_write(auto_prefs.putBool("auto_start", start_value),
                                              "start on cup", write_errors) || changed;
        }

        bool current_return = auto_prefs.getBool("auto_return", false);
        bool return_value = get_request_bool("auto_return", current_return, found);
        if (found) {
            changed = record_preference_write(auto_prefs.putBool("auto_return", return_value),
                                              "return on cup removal", write_errors) || changed;
        }
        auto_prefs.end();
    }

    Preferences screensaver_prefs;
    if (screensaver_prefs.begin(USER_SCREENSAVER_PREF_NAMESPACE, false)) {
        bool current_startup = screensaver_prefs.getBool(USER_SCREENSAVER_PREF_KEY_STARTUP, false);
        bool startup = get_request_bool("screensaver_startup", current_startup, found);
        if (found) {
            changed = record_preference_write(screensaver_prefs.putBool(USER_SCREENSAVER_PREF_KEY_STARTUP, startup),
                                              "startup screensaver", write_errors) || changed;
        }

        bool current_sleep = screensaver_prefs.getBool(USER_SCREENSAVER_PREF_KEY_SLEEP, false);
        bool sleep = get_request_bool("screensaver_sleep", current_sleep, found);
        if (found) {
            changed = record_preference_write(screensaver_prefs.putBool(USER_SCREENSAVER_PREF_KEY_SLEEP, sleep),
                                              "sleep screensaver", write_errors) || changed;
        }

        uint32_t current_startup_ms = screensaver_prefs.getUInt(USER_SCREENSAVER_PREF_KEY_STARTUP_DURATION_MS,
                                                                USER_SCREENSAVER_STARTUP_DURATION_DEFAULT_MS);
        float current_startup_s = static_cast<float>(current_startup_ms) / 1000.0f;
        float startup_s = get_request_float("screensaver_startup_duration_s", current_startup_s,
                                            static_cast<float>(USER_SCREENSAVER_STARTUP_DURATION_MIN_MS) / 1000.0f,
                                            static_cast<float>(USER_SCREENSAVER_STARTUP_DURATION_MAX_MS) / 1000.0f,
                                            found);
        if (found) {
            changed = record_preference_write(
                          screensaver_prefs.putUInt(USER_SCREENSAVER_PREF_KEY_STARTUP_DURATION_MS,
                                                    static_cast<uint32_t>(startup_s * 1000.0f + 0.5f)),
                          "startup duration",
                          write_errors) || changed;
        }

        uint32_t current_sleep_ms = screensaver_prefs.getUInt(USER_SCREENSAVER_PREF_KEY_SLEEP_DELAY_MS,
                                                              USER_SCREEN_AUTO_DIM_TIMEOUT_DEFAULT_MS);
        float current_sleep_min = static_cast<float>(current_sleep_ms) / 60000.0f;
        float sleep_min = get_request_float("screensaver_sleep_delay_min", current_sleep_min,
                                            static_cast<float>(USER_SCREEN_AUTO_DIM_TIMEOUT_MIN_MS) / 60000.0f,
                                            static_cast<float>(USER_SCREEN_AUTO_DIM_TIMEOUT_MAX_MS) / 60000.0f,
                                            found);
        if (found) {
            changed = record_preference_write(
                          screensaver_prefs.putUInt(USER_SCREENSAVER_PREF_KEY_SLEEP_DELAY_MS,
                                                    static_cast<uint32_t>(sleep_min * 60000.0f + 0.5f)),
                          "screensaver delay",
                          write_errors) || changed;
        }
        screensaver_prefs.end();
    }

    Preferences brightness_prefs;
    if (brightness_prefs.begin("brightness", false)) {
        float current_normal = brightness_prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
        float normal = get_request_float("brightness_normal", current_normal, 0.15f, 1.0f, found);
        if (found) {
            changed = record_preference_write(brightness_prefs.putFloat("normal", normal),
                                              "brightness", write_errors) || changed;
        }

        float current_screensaver = brightness_prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
        float screensaver = get_request_float("brightness_screensaver", current_screensaver, 0.15f, 1.0f, found);
        if (found) {
            changed = record_preference_write(brightness_prefs.putFloat("screensaver", screensaver),
                                              "screensaver brightness", write_errors) || changed;
        }
        brightness_prefs.end();
    }

    if (preferences_) {
        bool current_basket_detect = preferences_->getBool(BasketDetector::PREF_KEY_ENABLED, false);
        bool basket_detect = get_request_bool("basket_detect", current_basket_detect, found);
        if (found) {
            changed = record_preference_write(preferences_->putBool(BasketDetector::PREF_KEY_ENABLED, basket_detect),
                                              "basket detection", write_errors) || changed;
        }

        float current_single = sanitize_basket_weight(
            preferences_->getFloat(BasketDetector::PREF_KEY_SINGLE_WEIGHT_G, 0.0f));
        float single = get_request_float("basket_single_g", current_single, 0.0f, USER_MAX_TARGET_WEIGHT_G, found);
        if (found) {
            changed = record_preference_write(
                          preferences_->putFloat(BasketDetector::PREF_KEY_SINGLE_WEIGHT_G, sanitize_basket_weight(single)),
                          "single basket weight",
                          write_errors) || changed;
        }

        float current_double = sanitize_basket_weight(
            preferences_->getFloat(BasketDetector::PREF_KEY_DOUBLE_WEIGHT_G, 0.0f));
        float double_basket = get_request_float("basket_double_g", current_double, 0.0f, USER_MAX_TARGET_WEIGHT_G, found);
        if (found) {
            changed = record_preference_write(
                          preferences_->putFloat(BasketDetector::PREF_KEY_DOUBLE_WEIGHT_G, sanitize_basket_weight(double_basket)),
                          "double basket weight",
                          write_errors) || changed;
        }

        float current_tolerance = preferences_->getFloat(BasketDetector::PREF_KEY_TOLERANCE_G,
                                                         USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G);
        float tolerance = get_request_float("basket_tolerance_g", current_tolerance,
                                            USER_BASKET_DETECTION_TOLERANCE_MIN_G,
                                            USER_BASKET_DETECTION_TOLERANCE_MAX_G, found);
        if (found) {
            changed = record_preference_write(preferences_->putFloat(BasketDetector::PREF_KEY_TOLERANCE_G, tolerance),
                                              "basket tolerance", write_errors) || changed;
        }

        int current_grind_mode = preferences_->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
        int grind_mode = get_request_int("grind_mode", current_grind_mode, 0, 1, found);
        if (found) {
            changed = record_preference_write(preferences_->putInt("grind_mode", grind_mode),
                                              "grind mode", write_errors) || changed;
        }

        int current_purge_mode = preferences_->getInt(GrindController::PREF_KEY_GRINDER_MODE, GRIND_PURGE_MODE_DEFAULT);
        int purge_mode = get_request_int("purge_mode", current_purge_mode, 0, 1, found);
        if (found) {
            changed = record_preference_write(preferences_->putInt(GrindController::PREF_KEY_GRINDER_MODE, purge_mode),
                                              "purge mode", write_errors) || changed;
        }

        float current_purge_amount = preferences_->getFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, GRIND_PURGE_AMOUNT_DEFAULT_G);
        float purge_amount = get_request_float("purge_amount_g", current_purge_amount,
                                               GRIND_PURGE_AMOUNT_MIN_G, GRIND_PURGE_AMOUNT_MAX_G, found);
        if (found) {
            changed = record_preference_write(preferences_->putFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, purge_amount),
                                              "purge amount", write_errors) || changed;
        }

        float current_freshness = preferences_->getFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, GRIND_FRESHNESS_DEFAULT_HOURS);
        float freshness = get_request_float("freshness_hours", current_freshness, 0.5f, 48.0f, found);
        if (found) {
            changed = record_preference_write(preferences_->putFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, freshness),
                                              "freshness", write_errors) || changed;
        }

        float current_coast_ratio = preferences_->getFloat(GrindController::PREF_KEY_COAST_RATIO, GRIND_LATENCY_TO_COAST_RATIO_DEFAULT);
        float coast_ratio = get_request_float("coast_ratio", current_coast_ratio,
                                              GRIND_LATENCY_TO_COAST_RATIO_MIN,
                                              GRIND_LATENCY_TO_COAST_RATIO_MAX,
                                              found);
        if (found) {
            changed = record_preference_write(preferences_->putFloat(GrindController::PREF_KEY_COAST_RATIO, coast_ratio),
                                              "grind coast", write_errors) || changed;
        }
    }

    if (changed) {
        mark_settings_changed();
    }

    if (!write_errors.isEmpty()) {
        String json;
        json.reserve(120);
        json += "{\"ok\":false,\"error\":\"Could not persist: ";
        json += json_escape(write_errors);
        json += "\"}";
        send_json_response(500, json);
        return;
    }

    send_json_response(200, build_settings_json());
}

String ConnectivityManager::build_beans_json() const {
    String json;
    json.reserve(512 + BeanController::kMaxBeans * 260);
    json += "{\"ok\":true,\"capacity\":";
    json += String(BeanController::kMaxBeans);
    json += ",\"count\":";
    json += bean_controller_ ? String(bean_controller_->count()) : "0";
    json += ",\"active_bean_id\":";
    json += bean_controller_ ? String(bean_controller_->get_active_id()) : "0";
    json += ",\"beans\":[";

    if (bean_controller_) {
        for (uint8_t i = 0; i < bean_controller_->count(); ++i) {
            const BeanRecord* bean = bean_controller_->get_bean_at(i);
            if (!bean) {
                continue;
            }
            if (i > 0) {
                json += ",";
            }
            const uint16_t mg_single_x2 = bean->mahlgrad_x2[BeanController::kSingleProfileIndex];
            const uint16_t mg_double_x2 = bean->mahlgrad_x2[BeanController::kDoubleProfileIndex];
            const float dose_g = static_cast<float>(bean->dose_used_x10) / 10.0f;
            const float purge_g = static_cast<float>(bean->purge_used_x10) / 10.0f;
            json += "{\"id\":";
            json += String(bean->id);
            json += ",\"active\":";
            json += bean_controller_->get_active_id() == bean->id ? "true" : "false";
            json += ",\"name\":\"";
            json += json_escape(bean->name);
            json += "\",\"roaster\":\"";
            json += json_escape(bean->roaster);
            json += "\",\"bag_size_g\":";
            json += String(bean->bag_size_g);
            json += ",\"mahlgrad_x2\":";
            json += String(mg_double_x2);
            json += ",\"mahlgrad\":";
            json += String(BeanController::x2_to_mahlgrad(mg_double_x2), 1);
            json += ",\"mahlgrad_single_x2\":";
            json += String(mg_single_x2);
            json += ",\"mahlgrad_single\":";
            json += String(BeanController::x2_to_mahlgrad(mg_single_x2), 1);
            json += ",\"mahlgrad_double_x2\":";
            json += String(mg_double_x2);
            json += ",\"mahlgrad_double\":";
            json += String(BeanController::x2_to_mahlgrad(mg_double_x2), 1);
            json += ",\"dose_used_g\":";
            json += String(dose_g, 1);
            json += ",\"purge_used_g\":";
            json += String(purge_g, 1);
            json += ",\"total_used_g\":";
            json += String(dose_g + purge_g, 1);
            json += "}";
        }
    }

    json += "]}";
    return json;
}

void ConnectivityManager::handle_beans_get() {
    send_json_response(200, build_beans_json());
}

void ConnectivityManager::handle_beans_post() {
    if (!bean_controller_) {
        send_json_response(503, "{\"ok\":false,\"error\":\"Bean controller unavailable\"}");
        return;
    }

    bool found = false;
    String action = get_request_string("action", found);
    action.trim();
    action.toLowerCase();
    if (action.isEmpty()) {
        send_json_response(400, "{\"ok\":false,\"error\":\"Missing action\"}");
        return;
    }

    bool changed = false;
    if (action == "create") {
        bool name_found = false;
        String name = get_request_string("name", name_found);
        bool roaster_found = false;
        String roaster = get_request_string("roaster", roaster_found);
        (void)roaster_found;
        const int bag_size = get_request_int("bag_size_g", 0, 0, 5000, found);
        float mg_double = get_request_float("mahlgrad", 25.0f, 1.0f, 50.0f, found);
        bool mg_double_found = false;
        mg_double = get_request_float("mahlgrad_double", mg_double, 1.0f, 50.0f, mg_double_found);
        bool mg_single_found = false;
        float mg_single = get_request_float("mahlgrad_single", mg_double, 1.0f, 50.0f, mg_single_found);

        if (!name_found || name.length() == 0) {
            name = "Unnamed bean";
        }
        uint8_t id = 0;
        changed = bean_controller_->create_bean(name.c_str(), roaster.c_str(),
                                                static_cast<uint16_t>(bag_size), mg_single, mg_double, &id);
        if (!changed) {
            send_json_response(400, "{\"ok\":false,\"error\":\"Could not create bean\"}");
            return;
        }
    } else if (action == "update") {
        const int id = get_request_int("id", 0, 0, 255, found);
        const BeanRecord* current = bean_controller_->find_bean(static_cast<uint8_t>(id));
        if (!current) {
            send_json_response(404, "{\"ok\":false,\"error\":\"Bean not found\"}");
            return;
        }

        bool field_found = false;
        String name = get_request_string("name", field_found);
        if (!field_found) {
            name = current->name;
        }
        String roaster = get_request_string("roaster", field_found);
        if (!field_found) {
            roaster = current->roaster;
        }
        const int bag_size = get_request_int("bag_size_g", current->bag_size_g, 0, 5000, field_found);
        float mg_double = get_request_float("mahlgrad",
                                            BeanController::x2_to_mahlgrad(current->mahlgrad_x2[BeanController::kDoubleProfileIndex]),
                                            1.0f, 50.0f, field_found);
        bool mg_double_found = false;
        mg_double = get_request_float("mahlgrad_double", mg_double, 1.0f, 50.0f, mg_double_found);
        bool mg_single_found = false;
        float mg_single = get_request_float("mahlgrad_single",
                                            BeanController::x2_to_mahlgrad(current->mahlgrad_x2[BeanController::kSingleProfileIndex]),
                                            1.0f, 50.0f, mg_single_found);

        changed = bean_controller_->update_bean(static_cast<uint8_t>(id), name.c_str(), roaster.c_str(),
                                                static_cast<uint16_t>(bag_size), mg_single, mg_double);
        if (!changed) {
            send_json_response(500, "{\"ok\":false,\"error\":\"Could not update bean\"}");
            return;
        }
    } else if (action == "delete") {
        const int id = get_request_int("id", 0, 0, 255, found);
        changed = bean_controller_->delete_bean(static_cast<uint8_t>(id));
        if (!changed) {
            send_json_response(404, "{\"ok\":false,\"error\":\"Bean not found\"}");
            return;
        }
    } else if (action == "set_active") {
        const int id = get_request_int("id", 0, 0, 255, found);
        changed = bean_controller_->set_active_bean(static_cast<uint8_t>(id));
        if (!changed) {
            send_json_response(404, "{\"ok\":false,\"error\":\"Bean not found\"}");
            return;
        }
    } else if (action == "feedback") {
        int id = get_request_int("id", bean_controller_->get_active_id(), 0, 255, found);
        if (id == 0) {
            id = bean_controller_->get_active_id();
        }
        String feedback = get_request_string("feedback", found);
        feedback.trim();
        feedback.toLowerCase();
        BeanController::Feedback value = BeanController::Feedback::OK;
        if (feedback == "finer" || feedback == "fine" || feedback == "-1") {
            value = BeanController::Feedback::FINER;
        } else if (feedback == "coarser" || feedback == "coarse" || feedback == "1") {
            value = BeanController::Feedback::COARSER;
        }
        const int profile = get_request_int("profile", BeanController::kDoubleProfileIndex,
                                            BeanController::kSingleProfileIndex,
                                            BeanController::kDoubleProfileIndex, found);
        changed = bean_controller_->apply_feedback(static_cast<uint8_t>(id),
                                                   static_cast<uint8_t>(profile),
                                                   value);
        if (!changed) {
            send_json_response(404, "{\"ok\":false,\"error\":\"Bean not found\"}");
            return;
        }
    } else {
        send_json_response(400, "{\"ok\":false,\"error\":\"Unknown action\"}");
        return;
    }

    if (changed) {
        mark_settings_changed();
    }
    send_json_response(200, build_beans_json());
}

void ConnectivityManager::handle_basket_capture(bool capture_single) {
    if (!hardware_manager_) {
        send_json_response(503, "{\"ok\":false,\"error\":\"Hardware manager unavailable\"}");
        return;
    }

    WeightSensor* sensor = hardware_manager_->get_weight_sensor();
    if (!sensor) {
        send_json_response(503, "{\"ok\":false,\"error\":\"Weight sensor unavailable\"}");
        return;
    }

    if (sensor->is_tare_in_progress()) {
        send_json_response(409, "{\"ok\":false,\"error\":\"Scale is taring, try again shortly\"}");
        return;
    }

    float settle_time_s = 0.0f;
    const float captured_weight_g = sensor->get_precision_settled_weight(&settle_time_s);
    const float stored_weight_g = sanitize_basket_weight(captured_weight_g);
    if (stored_weight_g <= USER_BASKET_DETECTION_REMOVAL_THRESHOLD_G) {
        send_json_response(400, "{\"ok\":false,\"error\":\"Place the basket on the scale before capture\"}");
        return;
    }

    if (!preferences_) {
        send_json_response(503, "{\"ok\":false,\"error\":\"Preferences unavailable\"}");
        return;
    }

    preferences_->putFloat(capture_single ? BasketDetector::PREF_KEY_SINGLE_WEIGHT_G
                                          : BasketDetector::PREF_KEY_DOUBLE_WEIGHT_G,
                           stored_weight_g);
    mark_settings_changed();

    LOG_BLE("WiFi: Captured %s basket weight %.2fg after %.2fs settling\n",
            capture_single ? "single" : "double",
            static_cast<double>(stored_weight_g),
            static_cast<double>(settle_time_s));

    send_json_response(200, build_settings_json());
}

String ConnectivityManager::build_screensaver_json() const {
    String json;
    json.reserve(220);
    json += "{\"ok\":true,\"exists\":";
    json += has_screensaver_image() ? "true" : "false";
    json += ",\"bytes\":";
    json += String(get_screensaver_image_size());
    json += ",\"max_bytes\":";
    json += String(WIFI_SCREENSAVER_BYTES);
    json += ",\"width\":";
    json += String(WIFI_SCREENSAVER_WIDTH_PX);
    json += ",\"height\":";
    json += String(WIFI_SCREENSAVER_HEIGHT_PX);
    json += ",\"format\":\"rgb565\"}";
    return json;
}

void ConnectivityManager::handle_screensaver_status() {
    send_json_response(200, build_screensaver_json());
}

void ConnectivityManager::handle_screensaver_upload() {
    HTTPUpload& upload = server_.upload();

    switch (upload.status) {
        case UPLOAD_FILE_START:
            screensaver_upload_error_ = false;
            screensaver_error_message_ = "";
            screensaver_received_bytes_ = 0;
            if (screensaver_upload_file_) {
                screensaver_upload_file_.close();
            }
            LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
            screensaver_upload_file_ = LittleFS.open(WIFI_SCREENSAVER_TEMP_PATH, "w");
            if (!screensaver_upload_file_) {
                screensaver_upload_error_ = true;
                screensaver_error_message_ = "Could not open screensaver file";
            }
            break;

        case UPLOAD_FILE_WRITE:
            if (screensaver_upload_error_) {
                break;
            }
            if (screensaver_received_bytes_ + upload.currentSize > WIFI_SCREENSAVER_BYTES) {
                screensaver_upload_error_ = true;
                screensaver_error_message_ = "Screensaver image is too large";
                if (screensaver_upload_file_) {
                    screensaver_upload_file_.close();
                }
                LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
                break;
            }
            if (!screensaver_upload_file_ ||
                screensaver_upload_file_.write(upload.buf, upload.currentSize) != upload.currentSize) {
                screensaver_upload_error_ = true;
                screensaver_error_message_ = "Failed to write screensaver image";
                if (screensaver_upload_file_) {
                    screensaver_upload_file_.close();
                }
                LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
                break;
            }
            screensaver_received_bytes_ += upload.currentSize;
            break;

        case UPLOAD_FILE_END:
            if (screensaver_upload_file_) {
                screensaver_upload_file_.close();
            }
            if (screensaver_upload_error_) {
                break;
            }
            if (screensaver_received_bytes_ != WIFI_SCREENSAVER_BYTES) {
                screensaver_upload_error_ = true;
                screensaver_error_message_ = "Screensaver image has the wrong size";
                LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
                break;
            }
            LittleFS.remove(WIFI_SCREENSAVER_PATH);
            if (!LittleFS.rename(WIFI_SCREENSAVER_TEMP_PATH, WIFI_SCREENSAVER_PATH)) {
                screensaver_upload_error_ = true;
                screensaver_error_message_ = "Could not finalize screensaver image";
                LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
                break;
            }
            mark_settings_changed();
            LOG_BLE("WiFi: Screensaver image stored (%u bytes)\n",
                    static_cast<unsigned int>(screensaver_received_bytes_));
            break;

        case UPLOAD_FILE_ABORTED:
            screensaver_upload_error_ = true;
            screensaver_error_message_ = "Screensaver upload aborted";
            if (screensaver_upload_file_) {
                screensaver_upload_file_.close();
            }
            LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
            break;
    }
}

void ConnectivityManager::handle_screensaver_complete() {
    if (screensaver_upload_error_) {
        String error = screensaver_error_message_.isEmpty() ? "Screensaver upload failed" : screensaver_error_message_;
        send_json_response(400, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
        return;
    }

    send_json_response(200, build_screensaver_json());
}

void ConnectivityManager::handle_screensaver_clear() {
    LittleFS.remove(WIFI_SCREENSAVER_TEMP_PATH);
    bool removed = LittleFS.remove(WIFI_SCREENSAVER_PATH);
    if (removed) {
        mark_settings_changed();
    }
    send_json_response(200, build_screensaver_json());
}

void ConnectivityManager::handle_wifi_save() {
    String ssid = server_.arg("ssid");
    String password = server_.arg("password");
    ssid.trim();

    if (ssid.isEmpty()) {
        send_plain_response(400, "SSID is required");
        return;
    }
    if (ssid.length() > 32) {
        send_plain_response(400, "SSID too long (max 32 characters)");
        return;
    }
    // WPA2 passphrases are 8-63 characters; an empty password means an open network.
    if (!password.isEmpty() && password.length() < 8) {
        send_plain_response(400, "WiFi password must be at least 8 characters");
        return;
    }
    if (password.length() > 63) {
        send_plain_response(400, "WiFi password too long (max 63 characters)");
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

void ConnectivityManager::handle_ota_page() {
    static const char body[] = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GrindByWeight OTA</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#101010;color:#f4f4f4;font-family:Arial,sans-serif}main{max-width:480px;margin:0 auto;padding:18px}section{border:1px solid #333;background:#1a1a1a;border-radius:8px;padding:14px}h1{font-size:26px;margin:0 0 12px}p{color:#aaa;line-height:1.4}a{color:#58b7ff}input,button{width:100%;font:inherit;border:0;border-radius:6px;padding:12px;margin:0 0 10px}input{background:#080808;color:#fff;border:1px solid #444}button{background:#d71920;color:#fff;font-weight:700}button:disabled{background:#555;color:#aaa}progress{width:100%;height:18px;margin:4px 0 10px}.ok{color:#64d37a}.warn{color:#ffb35c}
</style>
</head>
<body><main><section><h1>Firmware OTA</h1><p>Upload a production firmware .bin. The grinder restarts after the update is applied.</p><form id="otaForm"><input id="firmware" type="file" name="firmware" accept=".bin,application/octet-stream" required><button id="uploadButton" type="submit">Upload Firmware</button></form><progress id="progress" max="100" value="0" hidden></progress><p id="message"></p><p><a href="/api/status">Status JSON</a></p></section></main><script>
const form=document.getElementById("otaForm"),fileInput=document.getElementById("firmware"),button=document.getElementById("uploadButton"),progress=document.getElementById("progress"),message=document.getElementById("message");
function setMessage(text,cls){message.textContent=text;message.className=cls||""}
form.addEventListener("submit",e=>{
  e.preventDefault();
  const file=fileInput.files&&fileInput.files[0];
  if(!file){setMessage("Select a firmware file.","warn");return}
  const xhr=new XMLHttpRequest();
  button.disabled=true;
  progress.hidden=false;
  progress.value=0;
  setMessage("Uploading...");
  const body=new FormData();
  body.append("firmware",file,file.name);
  xhr.open("POST","/ota");
  xhr.upload.onprogress=ev=>{
    if(ev.lengthComputable){progress.value=Math.round((ev.loaded*100)/ev.total)}
  };
  xhr.onload=()=>{
    button.disabled=false;
    if(xhr.status>=200&&xhr.status<300){progress.value=100;setMessage("Update complete. Device restarting.","ok");return}
    let text=xhr.responseText||"OTA failed";
    try{const json=JSON.parse(text);if(json.error)text=json.error}catch(_){}
    setMessage(text,"warn");
  };
  xhr.onerror=()=>{button.disabled=false;setMessage("Upload connection failed.","warn")};
  xhr.send(body);
});
</script></body>
</html>)HTML";
    send_html_response(body);
}

bool ConnectivityManager::begin_ota(size_t expected_size) {
    if (ota_in_progress_) {
        ota_error_ = true;
        ota_error_message_ = "OTA already in progress";
        return false;
    }

    // Suspend the real-time grind/weight tasks so a grind can't run while we
    // write flash. The Arduino Update library erases the OTA partition
    // incrementally as data arrives (one sector at a time), so there is no long
    // blocking erase up front and no need to reconfigure the task watchdog.
    task_manager.suspend_hardware_tasks();

    if (!ota_updater_.begin(UPDATE_SIZE_UNKNOWN)) {
        ota_error_ = true;
        ota_error_message_ = ota_updater_.errorString();
        LOG_BLE("WiFi OTA: Update.begin failed: %s\n", ota_updater_.errorString());
        task_manager.resume_hardware_tasks();
        return false;
    }

    if (expected_size > 0) {
        LOG_BLE("WiFi OTA: Starting upload, %u bytes expected\n",
                static_cast<unsigned int>(expected_size));
    } else {
        LOG_BLE("WiFi OTA: Starting upload with unknown size\n");
    }
    ota_received_bytes_ = 0;
    ota_total_bytes_ = expected_size;
    ota_error_ = false;
    ota_error_message_ = "";
    ota_in_progress_ = true;
    set_state(ConnectivityState::WIFI_OTA_RECEIVING, "Receiving update...");
    return true;
}

bool ConnectivityManager::write_ota_chunk(uint8_t* data, size_t size) {
    if (!ota_in_progress_ || !data || size == 0) {
        return false;
    }

    if (ota_updater_.write(data, size) != size) {
        ota_error_ = true;
        ota_error_message_ = ota_updater_.errorString();
        LOG_BLE("WiFi OTA: Update.write failed: %s\n", ota_updater_.errorString());
        return false;
    }

    size_t previous = ota_received_bytes_;
    ota_received_bytes_ += size;
    if ((previous / 16384) != (ota_received_bytes_ / 16384)) {
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

    // Update.end(true) finalizes the image and sets it as the boot partition.
    if (!ota_updater_.end(true)) {
        ota_error_ = true;
        ota_error_message_ = ota_updater_.errorString();
        LOG_BLE("WiFi OTA: Update.end failed: %s\n", ota_updater_.errorString());
        abort_ota();
        return false;
    }

    LOG_BLE("WiFi OTA: Update complete (%u KB), reboot scheduled\n",
            static_cast<unsigned int>(ota_received_bytes_ / 1024));
    // Leave ota_in_progress_ set so the loop keeps tasks suspended until the
    // scheduled reboot swaps in the new firmware.
    set_state(ConnectivityState::WIFI_OTA_APPLYING, "Restarting...");
    schedule_restart();
    return true;
}

void ConnectivityManager::abort_ota() {
    if (ota_in_progress_) {
        ota_updater_.abort();
    }

    ota_in_progress_ = false;
    ota_received_bytes_ = 0;
    ota_total_bytes_ = 0;
    task_manager.resume_hardware_tasks();

    if (enabled_) {
        set_state(ConnectivityState::WIFI_ERROR, "OTA failed");
    }
}

void ConnectivityManager::handle_ota_upload() {
    String content_type = server_.header("Content-Type");
    content_type.toLowerCase();
    if (!content_type.startsWith("multipart/")) {
        handle_ota_raw_upload();
        return;
    }

    HTTPUpload& upload = server_.upload();

    switch (upload.status) {
        case UPLOAD_FILE_START:
        {
            size_t expected_size = upload.totalSize;
            int request_size = server_.clientContentLength();
            if (expected_size == 0 && request_size > 0) {
                expected_size = static_cast<size_t>(request_size);
            }
            begin_ota(expected_size);
            break;
        }

        case UPLOAD_FILE_WRITE:
            // Stop writing (and abort) on the first failure instead of silently
            // consuming the rest of the upload.
            if (ota_in_progress_ && !ota_error_) {
                if (!write_ota_chunk(upload.buf, upload.currentSize)) {
                    abort_ota();
                }
            }
            break;

        case UPLOAD_FILE_END:
            ota_total_bytes_ = upload.totalSize;
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

void ConnectivityManager::handle_ota_raw_upload() {
    HTTPRaw& upload = server_.raw();

    switch (upload.status) {
        case RAW_START:
        {
            int content_length = server_.clientContentLength();
            if (content_length <= 0) {
                ota_error_ = true;
                ota_error_message_ = "Missing firmware content length";
                LOG_BLE("WiFi OTA: Raw upload missing Content-Length\n");
                break;
            }
            begin_ota(static_cast<size_t>(content_length));
            break;
        }

        case RAW_WRITE:
            if (ota_in_progress_ && !ota_error_) {
                if (!write_ota_chunk(upload.buf, upload.currentSize)) {
                    abort_ota();
                }
            }
            break;

        case RAW_END:
            if (ota_total_bytes_ == 0) {
                ota_total_bytes_ = upload.totalSize;
            }
            LOG_BLE("WiFi OTA: Raw upload finished, %u bytes\n",
                    static_cast<unsigned int>(upload.totalSize));
            break;

        case RAW_ABORTED:
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
    if (version.isEmpty()) {
        version = server_.header("X-Firmware-Version");
    }

    if (!version.isEmpty()) {
        store_expected_firmware_version(version);
    }

    if (ota_error_ || !finish_ota()) {
        String error = ota_error_message_.isEmpty() ? "OTA failed" : ota_error_message_;
        send_json_response(500, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
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

    if (setup_ap_active_ && server_.method() == HTTP_GET) {
        handle_setup_recovery_root();
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

String ConnectivityManager::get_mac_address() const {
    if (setup_ap_active_) {
        return WiFi.softAPmacAddress();
    }
    return WiFi.macAddress();
}

int32_t ConnectivityManager::get_rssi_dbm() const {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

const char* ConnectivityManager::get_mode_label() const {
    if (!enabled_) {
        return "Disabled";
    }
    if (setup_ap_active_) {
        return "Setup AP";
    }
    if (WiFi.status() == WL_CONNECTED) {
        return "Station";
    }
    return "Station";
}

String ConnectivityManager::get_ota_url() const {
    String ip = get_ip_address();
    if (ip.isEmpty()) {
        return "";
    }

    if (setup_ap_active_) {
        return String("http://") + ip + WIFI_OTA_PATH;
    }

    return String("http://") + WIFI_HOSTNAME + ".local" + WIFI_OTA_PATH;
}

bool ConnectivityManager::has_screensaver_image() const {
    return LittleFS.exists(WIFI_SCREENSAVER_PATH) &&
           get_screensaver_image_size() == WIFI_SCREENSAVER_BYTES;
}

size_t ConnectivityManager::get_screensaver_image_size() const {
    File file = LittleFS.open(WIFI_SCREENSAVER_PATH, "r");
    if (!file) {
        return 0;
    }
    size_t size = file.size();
    file.close();
    return size;
}

void ConnectivityManager::mark_settings_changed() {
    settings_changed_ = true;
}

bool ConnectivityManager::consume_settings_changed() {
    if (!settings_changed_) {
        return false;
    }
    settings_changed_ = false;
    return true;
}
