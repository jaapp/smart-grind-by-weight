#include "manager.h"

#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <ctype.h>

#include "../config/build_info.h"
#include "../controllers/grind_controller.h"
#include "../controllers/grind_mode.h"
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

} // namespace

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
    , screensaver_upload_error_(false)
    , restart_pending_(false)
    , settings_changed_(false)
    , ota_partition_(nullptr)
    , ota_handle_(0)
    , ota_received_bytes_(0)
    , ota_total_bytes_(0)
    , screensaver_received_bytes_(0)
    , last_reconnect_attempt_ms_(0)
    , restart_at_ms_(0) {
}

ConnectivityManager::~ConnectivityManager() {
    if (screensaver_upload_file_) {
        screensaver_upload_file_.close();
    }
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
    server_.on(WIFI_API_STATUS_PATH, HTTP_GET, [this]() { handle_status(); });
    server_.on(WIFI_API_SETTINGS_PATH, HTTP_GET, [this]() { handle_settings_get(); });
    server_.on(WIFI_API_SETTINGS_PATH, HTTP_POST, [this]() { handle_settings_post(); });
    server_.on(WIFI_API_SCREENSAVER_PATH, HTTP_GET, [this]() { handle_screensaver_status(); });
    server_.on(WIFI_API_SCREENSAVER_PATH, HTTP_POST,
               [this]() { handle_screensaver_complete(); },
               [this]() { handle_screensaver_upload(); });
    server_.on(WIFI_API_SCREENSAVER_CLEAR_PATH, HTTP_POST, [this]() { handle_screensaver_clear(); });
    server_.on("/wifi", HTTP_POST, [this]() { handle_wifi_save(); });
    server_.on("/wifi/clear", HTTP_POST, [this]() { handle_wifi_clear(); });
    server_.on(WIFI_OTA_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_STATUS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_STATUS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_SETTINGS_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_SCREENSAVER_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
    server_.on(WIFI_API_SCREENSAVER_CLEAR_PATH, HTTP_OPTIONS, [this]() { handle_options(); });
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
    server_.sendHeader("Access-Control-Allow-Headers", "Content-Type,Accept");
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
    send_cors_headers();

    static const char body[] = R"HTML(<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GrindByWeight WiFi</title>
<style>
:root{color-scheme:dark;--bg:#101010;--panel:#1b1b1b;--muted:#a8a8a8;--line:#323232;--red:#d71920;--blue:#00aaff;--green:#2aa84a;--text:#f5f5f5}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Arial,sans-serif}main{max-width:760px;margin:0 auto;padding:22px}h1{margin:0 0 18px;font-size:30px}h2{margin:0 0 14px;font-size:21px}section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:16px;margin:14px 0}label{display:block;margin:12px 0 6px;color:var(--muted)}input,select,button{width:100%;font:inherit;border:0;border-radius:6px;padding:11px;margin:0 0 10px}input,select{background:#0b0b0b;color:var(--text);border:1px solid var(--line)}input[type=checkbox]{width:auto;margin-right:8px}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.check{display:flex;align-items:center;color:var(--text);margin:10px 0}.actions{display:flex;gap:10px;flex-wrap:wrap}.actions button{flex:1 1 160px}button{background:var(--red);color:#fff;font-weight:700;cursor:pointer}.secondary{background:#333}.ok{color:var(--green)}.warn{color:#f2a12b}.muted{color:var(--muted)}dl{display:grid;grid-template-columns:135px 1fr;gap:8px;margin:0}dt{color:var(--muted)}dd{margin:0;word-break:break-word}.value{float:right;color:var(--muted)}@media(max-width:620px){main{padding:14px}.row{grid-template-columns:1fr}dl{grid-template-columns:1fr}dt{margin-top:8px}}
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
<h2>Network</h2>
<form method="post" action="/wifi">
<label>SSID</label><input name="ssid" id="ssid" required>
<label>Password</label><input name="password" type="password" autocomplete="current-password">
<button type="submit">Save WiFi</button>
</form>
<form method="post" action="/wifi/clear"><button class="secondary" type="submit">Clear WiFi</button></form>
</section>

<section>
<h2>Device Settings</h2>
<form id="settingsForm">
<div class="row">
<div><label>Grind Mode</label><select id="grind_mode"><option value="0">Weight</option><option value="1">Time</option></select></div>
<div><label>Purge Mode</label><select id="purge_mode"><option value="0">Keep purge grinds</option><option value="1">Remove purge grinds</option></select></div>
</div>
<label>Purge Amount <span class="value" id="purge_amount_value"></span></label><input id="purge_amount_g" type="range" min="0.1" max="2.5" step="0.1">
<label>Freshness <span class="value" id="freshness_value"></span></label><input id="freshness_hours" type="range" min="0.5" max="48" step="0.5">
<label>Brightness <span class="value" id="brightness_normal_value"></span></label><input id="brightness_normal" type="range" min="0.15" max="1" step="0.01">
<label>Screensaver Brightness <span class="value" id="brightness_screensaver_value"></span></label><input id="brightness_screensaver" type="range" min="0.15" max="1" step="0.01">
<label class="check"><input id="wifi_startup" type="checkbox">Start WiFi on boot</label>
<label class="check"><input id="swipe_enabled" type="checkbox">Swipe mode switching</label>
<label class="check"><input id="auto_start" type="checkbox">Start on cup</label>
<label class="check"><input id="auto_return" type="checkbox">Return on cup removal</label>
<label class="check"><input id="logging_enabled" type="checkbox">Grind logging</label>
<button type="submit">Save Settings</button>
<p id="settingsMessage" class="muted"></p>
</form>
</section>

<section>
<h2>Screensaver Image</h2>
<input id="screensaverFile" type="file" accept="image/*">
<div class="actions"><button id="uploadScreensaver" type="button">Upload Image</button><button id="clearScreensaver" class="secondary" type="button">Clear Image</button></div>
<p id="imageMessage" class="muted">Images are resized to the device display before upload.</p>
</section>

<section>
<h2>OTA Update</h2>
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
function renderStatus(s){
  const rows=[["State",s.status],["Mode",s.mode],["SSID",s.ssid||"--"],["IP",s.ip||"--"],["Host",s.host_url||"--"],["MAC",s.mac||"--"],["RSSI",s.connected?(s.rssi_dbm+" dBm"):"--"],["OTA",s.ota_active?(s.ota_progress+"%"):(s.ota_url||"--")],["Build","#"+s.build],["Version",s.version]];
  $("statusList").innerHTML=rows.map(r=>"<dt>"+r[0]+"</dt><dd>"+r[1]+"</dd>").join("");
  $("ssid").value=s.setup_ap?"":(s.ssid||"");
}
function bindRange(id,fmt){const el=$(id),out=$(id+"_value");const upd=()=>out.textContent=fmt(el.value);el.addEventListener("input",upd);upd()}
function renderSettings(x){
  const s=x.settings||x;
  $("wifi_startup").checked=!!s.wifi_startup;$("logging_enabled").checked=!!s.logging_enabled;$("swipe_enabled").checked=!!s.swipe_enabled;$("auto_start").checked=!!s.auto_start;$("auto_return").checked=!!s.auto_return;
  $("grind_mode").value=String(s.grind_mode_index);$("purge_mode").value=String(s.purge_mode_index);
  $("purge_amount_g").value=s.purge_amount_g;$("freshness_hours").value=s.freshness_hours;$("brightness_normal").value=s.brightness_normal;$("brightness_screensaver").value=s.brightness_screensaver;
  ["purge_amount_g","freshness_hours","brightness_normal","brightness_screensaver"].forEach(id=>$(id).dispatchEvent(new Event("input")));
  $("screensaverState").textContent=s.screensaver_image?"Screensaver image stored: "+s.screensaver_image_bytes+" bytes":"No screensaver image stored";
}
async function load(){try{renderStatus(await json("/api/status"));renderSettings(await json("/api/settings"))}catch(e){setMsg("settingsMessage",e.message,"warn")}}
function settingsPayload(){
  const p=new URLSearchParams();
  ["wifi_startup","logging_enabled","swipe_enabled","auto_start","auto_return"].forEach(id=>p.set(id,$(id).checked?"1":"0"));
  ["grind_mode","purge_mode","purge_amount_g","freshness_hours","brightness_normal","brightness_screensaver"].forEach(id=>p.set(id,$(id).value));
  return p;
}
$("settingsForm").addEventListener("submit",async e=>{e.preventDefault();setMsg("settingsMessage","Saving...");try{await json("/api/settings",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:settingsPayload()});setMsg("settingsMessage","Settings saved","ok");load()}catch(err){setMsg("settingsMessage",err.message,"warn")}});
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
bindRange("purge_amount_g",v=>Number(v).toFixed(1)+"g");bindRange("freshness_hours",v=>Number(v).toFixed(1)+"h");bindRange("brightness_normal",pct);bindRange("brightness_screensaver",pct);
load();setInterval(load,5000);
</script>
</body>
</html>)HTML";

    server_.send(200, "text/html", body);
}

String ConnectivityManager::build_status_json() const {
    String ip = get_ip_address();
    String host_url = "";
    if (!ip.isEmpty()) {
        host_url = setup_ap_active_ ? String("http://") + ip
                                    : String("http://") + WIFI_HOSTNAME + ".local";
    }
    String json;
    json.reserve(900);
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

    bool auto_start = false;
    bool auto_return = false;
    Preferences auto_prefs;
    if (auto_prefs.begin("autogrind", true)) {
        auto_start = auto_prefs.getBool("auto_start", false);
        auto_return = auto_prefs.getBool("auto_return", false);
        auto_prefs.end();
    }

    int grind_mode_index = static_cast<int>(GrindMode::WEIGHT);
    int purge_mode_index = GRIND_PURGE_MODE_DEFAULT;
    float purge_amount_g = GRIND_PURGE_AMOUNT_DEFAULT_G;
    float freshness_hours = GRIND_FRESHNESS_DEFAULT_HOURS;
    if (preferences_) {
        grind_mode_index = preferences_->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
        purge_mode_index = preferences_->getInt(GrindController::PREF_KEY_GRINDER_MODE, GRIND_PURGE_MODE_DEFAULT);
        purge_amount_g = preferences_->getFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, GRIND_PURGE_AMOUNT_DEFAULT_G);
        freshness_hours = preferences_->getFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, GRIND_FRESHNESS_DEFAULT_HOURS);
    }

    grind_mode_index = clamp_int(grind_mode_index, 0, 1);
    purge_mode_index = clamp_int(purge_mode_index, 0, 1);
    purge_amount_g = clamp_float(purge_amount_g, GRIND_PURGE_AMOUNT_MIN_G, GRIND_PURGE_AMOUNT_MAX_G);
    freshness_hours = clamp_float(freshness_hours, 0.5f, 48.0f);
    brightness_normal = clamp_float(brightness_normal, 0.15f, 1.0f);
    brightness_screensaver = clamp_float(brightness_screensaver, 0.15f, 1.0f);

    String json;
    json.reserve(900);
    json += "{\"ok\":true,\"settings\":{";
    json += "\"wifi_startup\":";
    json += wifi_startup ? "true" : "false";
    json += ",\"logging_enabled\":";
    json += logging_enabled ? "true" : "false";
    json += ",\"swipe_enabled\":";
    json += swipe_enabled ? "true" : "false";
    json += ",\"auto_start\":";
    json += auto_start ? "true" : "false";
    json += ",\"auto_return\":";
    json += auto_return ? "true" : "false";
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
    json += ",\"brightness_normal\":";
    json += String(brightness_normal, 2);
    json += ",\"brightness_screensaver\":";
    json += String(brightness_screensaver, 2);
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

    Preferences wifi_prefs;
    if (wifi_prefs.begin(WIFI_PREF_NAMESPACE, false)) {
        bool current = wifi_prefs.getBool(WIFI_PREF_KEY_STARTUP, true);
        bool value = get_request_bool("wifi_startup", current, found);
        if (found) {
            wifi_prefs.putBool(WIFI_PREF_KEY_STARTUP, value);
            changed = true;
        }
        wifi_prefs.end();
    }

    Preferences logging_prefs;
    if (logging_prefs.begin("logging", false)) {
        bool current = logging_prefs.getBool("enabled", false);
        bool value = get_request_bool("logging_enabled", current, found);
        if (found) {
            logging_prefs.putBool("enabled", value);
            changed = true;
        }
        logging_prefs.end();
    }

    Preferences swipe_prefs;
    if (swipe_prefs.begin("swipe", false)) {
        bool current = swipe_prefs.getBool("enabled", false);
        bool value = get_request_bool("swipe_enabled", current, found);
        if (found) {
            swipe_prefs.putBool("enabled", value);
            changed = true;
        }
        swipe_prefs.end();
    }

    Preferences auto_prefs;
    if (auto_prefs.begin("autogrind", false)) {
        bool current_start = auto_prefs.getBool("auto_start", false);
        bool start_value = get_request_bool("auto_start", current_start, found);
        if (found) {
            auto_prefs.putBool("auto_start", start_value);
            changed = true;
        }

        bool current_return = auto_prefs.getBool("auto_return", false);
        bool return_value = get_request_bool("auto_return", current_return, found);
        if (found) {
            auto_prefs.putBool("auto_return", return_value);
            changed = true;
        }
        auto_prefs.end();
    }

    Preferences brightness_prefs;
    if (brightness_prefs.begin("brightness", false)) {
        float current_normal = brightness_prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
        float normal = get_request_float("brightness_normal", current_normal, 0.15f, 1.0f, found);
        if (found) {
            brightness_prefs.putFloat("normal", normal);
            changed = true;
        }

        float current_screensaver = brightness_prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
        float screensaver = get_request_float("brightness_screensaver", current_screensaver, 0.15f, 1.0f, found);
        if (found) {
            brightness_prefs.putFloat("screensaver", screensaver);
            changed = true;
        }
        brightness_prefs.end();
    }

    if (preferences_) {
        int current_grind_mode = preferences_->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
        int grind_mode = get_request_int("grind_mode", current_grind_mode, 0, 1, found);
        if (found) {
            preferences_->putInt("grind_mode", grind_mode);
            changed = true;
        }

        int current_purge_mode = preferences_->getInt(GrindController::PREF_KEY_GRINDER_MODE, GRIND_PURGE_MODE_DEFAULT);
        int purge_mode = get_request_int("purge_mode", current_purge_mode, 0, 1, found);
        if (found) {
            preferences_->putInt(GrindController::PREF_KEY_GRINDER_MODE, purge_mode);
            changed = true;
        }

        float current_purge_amount = preferences_->getFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, GRIND_PURGE_AMOUNT_DEFAULT_G);
        float purge_amount = get_request_float("purge_amount_g", current_purge_amount,
                                               GRIND_PURGE_AMOUNT_MIN_G, GRIND_PURGE_AMOUNT_MAX_G, found);
        if (found) {
            preferences_->putFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, purge_amount);
            changed = true;
        }

        float current_freshness = preferences_->getFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, GRIND_FRESHNESS_DEFAULT_HOURS);
        float freshness = get_request_float("freshness_hours", current_freshness, 0.5f, 48.0f, found);
        if (found) {
            preferences_->putFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, freshness);
            changed = true;
        }
    }

    if (changed) {
        mark_settings_changed();
    }

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
