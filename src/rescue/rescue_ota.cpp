#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "../config/build_info.h"
#include "../config/connectivity.h"

namespace {

WebServer server(WIFI_HTTP_PORT);

String sta_ssid;
String sta_password;
String ota_error_message;

bool station_started = false;
bool ota_running = false;
bool ota_error = false;
bool ota_success = false;
bool restart_pending = false;

uint32_t last_station_attempt_ms = 0;
uint32_t restart_at_ms = 0;
size_t ota_received_bytes = 0;

String html_escape(const String& value) {
    String escaped;
    escaped.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '&': escaped += F("&amp;"); break;
            case '<': escaped += F("&lt;"); break;
            case '>': escaped += F("&gt;"); break;
            case '"': escaped += F("&quot;"); break;
            default: escaped += value[i]; break;
        }
    }
    return escaped;
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

void send_cors_headers() {
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.sendHeader(F("Access-Control-Allow-Methods"), F("GET,POST,OPTIONS"));
    server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type,Accept"));
}

void send_plain(int code, const char* body) {
    send_cors_headers();
    server.send(code, F("text/plain"), body ? body : "");
}

bool load_wifi_credentials() {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREF_NAMESPACE, true)) {
        return false;
    }
    sta_ssid = prefs.getString(WIFI_PREF_KEY_SSID, "");
    sta_password = prefs.getString(WIFI_PREF_KEY_PASSWORD, "");
    prefs.end();
    sta_ssid.trim();
    return !sta_ssid.isEmpty();
}

void save_wifi_credentials(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
        return;
    }
    prefs.putString(WIFI_PREF_KEY_SSID, ssid);
    prefs.putString(WIFI_PREF_KEY_PASSWORD, password);
    prefs.putBool(WIFI_PREF_KEY_STARTUP, true);
    prefs.end();
}

void clear_wifi_credentials() {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
        return;
    }
    prefs.remove(WIFI_PREF_KEY_SSID);
    prefs.remove(WIFI_PREF_KEY_PASSWORD);
    prefs.putBool(WIFI_PREF_KEY_STARTUP, true);
    prefs.end();
}

void schedule_restart(uint32_t delay_ms = 1200) {
    restart_pending = true;
    restart_at_ms = millis() + delay_ms;
}

String station_ip() {
    return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
}

void begin_station_connect() {
    if (sta_ssid.isEmpty() || ota_running) {
        return;
    }
    station_started = true;
    last_station_attempt_ms = millis();
    WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
}

void start_network() {
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(WIFI_HOSTNAME);

    WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                      IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_SETUP_AP_SSID);

    if (load_wifi_credentials()) {
        begin_station_connect();
        const uint32_t start_ms = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start_ms < 8000) {
            delay(100);
        }
        if (WiFi.status() == WL_CONNECTED) {
            MDNS.begin(WIFI_HOSTNAME);
        }
    }
}

String build_status_json() {
    const esp_reset_reason_t reason = esp_reset_reason();
    String json;
    json.reserve(700);
    json += F("{\"device\":\"");
    json += CONNECTIVITY_DEVICE_NAME;
    json += F("\",\"firmware\":\"rescue-ota\",\"version\":\"");
    json += BUILD_FIRMWARE_VERSION;
    json += F("\",\"build\":");
    json += BUILD_NUMBER;
    json += F(",\"uptime_ms\":");
    json += millis();
    json += F(",\"reset_reason\":\"");
    json += reset_reason_label(reason);
    json += F("\",\"free_heap_bytes\":");
    json += ESP.getFreeHeap();
    json += F(",\"ap_ssid\":\"");
    json += WIFI_SETUP_AP_SSID;
    json += F("\",\"ap_ip\":\"");
    json += WiFi.softAPIP().toString();
    json += F("\",\"sta_ssid\":\"");
    json += html_escape(sta_ssid);
    json += F("\",\"sta_connected\":");
    json += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
    json += F(",\"sta_ip\":\"");
    json += station_ip();
    json += F("\",\"ota_running\":");
    json += ota_running ? F("true") : F("false");
    json += F(",\"ota_received_bytes\":");
    json += ota_received_bytes;
    json += F("}");
    return json;
}

void handle_status() {
    send_cors_headers();
    server.send(200, F("application/json"), build_status_json());
}

void handle_root() {
    String page;
    page.reserve(5000);
    page += F("<!doctype html><html><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>GrindByWeight Rescue OTA</title><style>"
              ":root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#101010;color:#f5f5f5;font-family:Arial,sans-serif}"
              "main{max-width:540px;margin:0 auto;padding:16px}section{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:14px;margin:12px 0}"
              "h1{font-size:25px;margin:0 0 8px}h2{font-size:19px;margin:0 0 8px}p,small{color:#aaa;line-height:1.4}"
              "input,button{width:100%;font:inherit;border:0;border-radius:6px;padding:12px;margin:0 0 10px}"
              "input{background:#080808;color:#fff;border:1px solid #444}button{background:#d71920;color:#fff;font-weight:700}"
              ".secondary{background:#333}.ok{color:#36c36b}.warn{color:#ffb13b}dl{display:grid;grid-template-columns:120px 1fr;gap:7px}dt{color:#aaa}dd{margin:0;word-break:break-word}"
              "</style></head><body><main><h1>Rescue OTA</h1>"
              "<p>This temporary firmware only keeps WiFi and firmware upload running.</p>");

    if (ota_error) {
        page += F("<section><h2 class=\"warn\">Last OTA Failed</h2><p>");
        page += html_escape(ota_error_message);
        page += F("</p></section>");
    } else if (ota_success) {
        page += F("<section><h2 class=\"ok\">OTA Complete</h2><p>Restarting into the uploaded firmware...</p></section>");
    }

    page += F("<section><h2>Status</h2><dl><dt>AP</dt><dd>");
    page += WIFI_SETUP_AP_SSID;
    page += F(" at ");
    page += WiFi.softAPIP().toString();
    page += F("</dd><dt>Router WiFi</dt><dd>");
    page += html_escape(sta_ssid.isEmpty() ? String("not configured") : sta_ssid);
    page += F("</dd><dt>Router IP</dt><dd>");
    page += WiFi.status() == WL_CONNECTED ? station_ip() : String("not connected");
    page += F("</dd><dt>Build</dt><dd>");
    page += BUILD_NUMBER;
    page += F("</dd><dt>Free heap</dt><dd>");
    page += ESP.getFreeHeap();
    page += F(" bytes</dd></dl><p><a class=\"ok\" href=\"/api/status\">Status JSON</a></p></section>");

    page += F("<section><h2>Firmware Upload</h2>"
              "<p>Upload the production .bin here. Keep the phone awake until the response page appears.</p>"
              "<form method=\"post\" action=\"/ota\" enctype=\"multipart/form-data\">"
              "<input type=\"file\" name=\"firmware\" accept=\".bin,application/octet-stream\" required>"
              "<button type=\"submit\">Upload Firmware</button></form>"
              "<small>Use this page for the full production firmware after the rescue firmware is running.</small></section>");

    page += F("<section><h2>WiFi</h2>"
              "<form method=\"post\" action=\"/wifi\">"
              "<input name=\"ssid\" placeholder=\"SSID\" value=\"");
    page += html_escape(sta_ssid);
    page += F("\"><input name=\"password\" type=\"password\" placeholder=\"Password\">"
              "<button class=\"secondary\" type=\"submit\">Save WiFi and Restart</button></form>"
              "<form method=\"post\" action=\"/wifi/clear\"><button class=\"secondary\" type=\"submit\">Clear WiFi and Restart</button></form>"
              "</section></main></body></html>");

    send_cors_headers();
    server.send(200, F("text/html; charset=utf-8"), page);
}

void handle_wifi_save() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    ssid.trim();
    if (ssid.isEmpty()) {
        send_plain(400, "SSID is required");
        return;
    }
    save_wifi_credentials(ssid, password);
    server.send(200, F("text/html; charset=utf-8"),
                F("<!doctype html><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<body style=\"font-family:Arial;background:#111;color:#eee;padding:20px\">"
                  "<h1>WiFi saved</h1><p>Restarting rescue firmware...</p></body>"));
    schedule_restart();
}

void handle_wifi_clear() {
    clear_wifi_credentials();
    server.send(200, F("text/html; charset=utf-8"),
                F("<!doctype html><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<body style=\"font-family:Arial;background:#111;color:#eee;padding:20px\">"
                  "<h1>WiFi cleared</h1><p>Restarting rescue firmware...</p></body>"));
    schedule_restart();
}

void handle_ota_upload() {
    HTTPUpload& upload = server.upload();

    switch (upload.status) {
        case UPLOAD_FILE_START:
            ota_running = true;
            ota_error = false;
            ota_success = false;
            ota_error_message = "";
            ota_received_bytes = 0;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                ota_error = true;
                ota_error_message = Update.errorString();
            }
            break;

        case UPLOAD_FILE_WRITE:
            if (!ota_error) {
                const size_t written = Update.write(upload.buf, upload.currentSize);
                ota_received_bytes += written;
                if (written != upload.currentSize) {
                    ota_error = true;
                    ota_error_message = Update.errorString();
                }
            }
            break;

        case UPLOAD_FILE_END:
            if (!ota_error && Update.end(true)) {
                ota_success = true;
            } else {
                ota_error = true;
                if (ota_error_message.isEmpty()) {
                    ota_error_message = Update.errorString();
                }
            }
            ota_running = false;
            break;

        case UPLOAD_FILE_ABORTED:
            Update.abort();
            ota_running = false;
            ota_error = true;
            ota_error_message = F("Upload aborted");
            break;
    }
}

void handle_ota_complete() {
    server.sendHeader(F("Connection"), F("close"));
    if (ota_error || !ota_success) {
        if (ota_error_message.isEmpty()) {
            ota_error_message = F("OTA failed");
        }
        handle_root();
        return;
    }

    handle_root();
    schedule_restart(1500);
}

void handle_options() {
    send_cors_headers();
    server.send(204, F("text/plain"), "");
}

void register_routes() {
    server.on("/", HTTP_GET, handle_root);
    server.on("/generate_204", HTTP_GET, handle_root);
    server.on("/gen_204", HTTP_GET, handle_root);
    server.on("/hotspot-detect.html", HTTP_GET, handle_root);
    server.on("/library/test/success.html", HTTP_GET, handle_root);
    server.on("/connecttest.txt", HTTP_GET, handle_root);
    server.on("/ncsi.txt", HTTP_GET, handle_root);
    server.on("/ping", HTTP_GET, []() { send_plain(200, "ok"); });
    server.on("/status", HTTP_GET, handle_status);
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/wifi", HTTP_POST, handle_wifi_save);
    server.on("/wifi/clear", HTTP_POST, handle_wifi_clear);
    server.on("/ota", HTTP_POST, handle_ota_complete, handle_ota_upload);
    server.on("/ota", HTTP_GET, handle_root);
    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) {
            handle_options();
        } else if (server.method() == HTTP_GET) {
            handle_root();
        } else {
            send_plain(404, "Not found");
        }
    });
}

void maintain_station() {
    if (sta_ssid.isEmpty() || ota_running || WiFi.status() == WL_CONNECTED) {
        return;
    }
    const uint32_t now = millis();
    if (!station_started || now - last_station_attempt_ms >= WIFI_RECONNECT_INTERVAL_MS) {
        begin_station_connect();
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("GrindByWeight rescue OTA build %d\n", BUILD_NUMBER);

    start_network();
    register_routes();
    server.begin(WIFI_HTTP_PORT);
    Serial.printf("Rescue OTA AP: %s at %s\n", WIFI_SETUP_AP_SSID, WiFi.softAPIP().toString().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Rescue OTA STA: %s\n", WiFi.localIP().toString().c_str());
    }
}

void loop() {
    server.handleClient();
    maintain_station();

    if (restart_pending && millis() >= restart_at_ms) {
        Serial.flush();
        ESP.restart();
    }

    delay(1);
}
