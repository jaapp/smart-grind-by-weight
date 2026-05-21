#include "manager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <esp_system.h>

#include "../config/constants.h"
#include "../controllers/basket_detector.h"
#include "../controllers/grind_controller.h"
#include "../controllers/profile_controller.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/hardware_manager.h"
#include "../logging/grind_logging.h"
#include "../system/state_machine.h"
#include "../system/statistics_manager.h"
#include "../ui/ui_manager.h"

HomeAssistantManager home_assistant_manager;
HomeAssistantManager* HomeAssistantManager::instance_ = nullptr;

namespace {
void copy_string(char* destination, size_t destination_size, const String& source) {
    if (!destination || destination_size == 0) {
        return;
    }
    strncpy(destination, source.c_str(), destination_size - 1);
    destination[destination_size - 1] = '\0';
}

const char* grind_mode_to_state(GrindMode mode) {
    return mode == GrindMode::TIME ? "Time" : "Weight";
}

const char* session_result_to_state(GrindController::GrindSessionResult result) {
    switch (result) {
        case GrindController::GrindSessionResult::SUCCESS: return "success";
        case GrindController::GrindSessionResult::OVERSHOOT: return "overshoot";
        case GrindController::GrindSessionResult::MAX_PULSES: return "max_pulses";
        case GrindController::GrindSessionResult::TIMEOUT: return "timeout";
        case GrindController::GrindSessionResult::ERROR: return "error";
        case GrindController::GrindSessionResult::UNKNOWN:
        default: return "unknown";
    }
}

const char* profile_name_for_index(int index) {
    switch (index) {
        case 0: return "Single";
        case 1: return "Double";
        case 2: return "Custom";
        default: return "Unknown";
    }
}

float clamp_float(float value, float min_value, float max_value) {
    if (!std::isfinite(value)) return min_value;
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}
} // namespace

HomeAssistantManager::HomeAssistantManager()
    : mqtt_client_(wifi_client_) {
    instance_ = this;
}

void HomeAssistantManager::init(HardwareManager* hardware,
                                StateMachine* state_machine,
                                ProfileController* profiles,
                                GrindController* grind_controller,
                                UIManager* ui_manager) {
    hardware_ = hardware;
    state_machine_ = state_machine;
    profiles_ = profiles;
    grind_controller_ = grind_controller;
    ui_manager_ = ui_manager;

    load_config();
    build_device_identity();
    mqtt_client_.setBufferSize(HA_MQTT_PACKET_BUFFER_SIZE);
    mqtt_client_.setKeepAlive(30);
    mqtt_client_.setSocketTimeout(2);
    mqtt_client_.setCallback(HomeAssistantManager::mqtt_callback);

    LOG_BLE("Home Assistant: %s (device_id=%s, broker=%s:%u)\n",
            config_.enabled ? "enabled" : "disabled",
            device_id_,
            config_.mqtt_host,
            static_cast<unsigned>(config_.mqtt_port));
}

void HomeAssistantManager::load_config() {
    Preferences prefs;
    if (!prefs.begin("ha", true)) {
        return;
    }

    config_.enabled = prefs.getBool("enabled", HA_INTEGRATION_ENABLED_DEFAULT);
    copy_string(config_.wifi_ssid, sizeof(config_.wifi_ssid), prefs.getString("wifi_ssid", HA_WIFI_SSID));
    copy_string(config_.wifi_password, sizeof(config_.wifi_password), prefs.getString("wifi_pass", HA_WIFI_PASSWORD));
    copy_string(config_.mqtt_host, sizeof(config_.mqtt_host), prefs.getString("mqtt_host", HA_MQTT_HOST));
    config_.mqtt_port = prefs.getUShort("mqtt_port", HA_MQTT_PORT);
    copy_string(config_.mqtt_username, sizeof(config_.mqtt_username), prefs.getString("mqtt_user", HA_MQTT_USERNAME));
    copy_string(config_.mqtt_password, sizeof(config_.mqtt_password), prefs.getString("mqtt_pass", HA_MQTT_PASSWORD));
    copy_string(config_.discovery_prefix, sizeof(config_.discovery_prefix), prefs.getString("disc_prefix", HA_MQTT_DISCOVERY_PREFIX));
    copy_string(config_.base_topic, sizeof(config_.base_topic), prefs.getString("base_topic", HA_MQTT_BASE_TOPIC));
    copy_string(config_.device_name, sizeof(config_.device_name), prefs.getString("dev_name", HA_MQTT_DEVICE_NAME));
    prefs.end();
}

void HomeAssistantManager::build_device_identity() {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(device_id_, sizeof(device_id_), "%04X%08lX",
             static_cast<unsigned>((mac >> 32) & 0xFFFF),
             static_cast<unsigned long>(mac & 0xFFFFFFFF));
    snprintf(client_id_, sizeof(client_id_), "sgbw_%s", device_id_);
}

bool HomeAssistantManager::has_required_config() const {
    return config_.enabled &&
           config_.wifi_ssid[0] != '\0' &&
           config_.mqtt_host[0] != '\0';
}

void HomeAssistantManager::handle() {
    if (!has_required_config()) {
        return;
    }

    ensure_wifi();
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    ensure_mqtt();
    if (!mqtt_client_.connected()) {
        return;
    }

    mqtt_client_.loop();

    uint8_t phase_id = grind_controller_ ? grind_controller_->get_phase_id() : 255;
    bool grind_active = grind_controller_ && grind_controller_->is_active();
    auto session_result = grind_controller_
        ? grind_controller_->get_last_session_result()
        : GrindController::GrindSessionResult::UNKNOWN;
    uint8_t session_result_id = static_cast<uint8_t>(session_result);

    if (phase_id != last_phase_id_) {
        last_phase_id_ = phase_id;
        publish_grind_event("phase_changed");
        publish_realtime_state(true);
    }

    if (last_grind_active_ != grind_active) {
        last_grind_active_ = grind_active;
        publish_grind_event(grind_active ? "started" : "ended");
        publish_realtime_state(true);
    }

    if (last_session_result_ != session_result_id) {
        last_session_result_ = session_result_id;
        publish_session_state(true);
        if (session_result != GrindController::GrindSessionResult::UNKNOWN) {
            publish_grind_event("session_result");
        }
    }

    publish_all_state(false);
}

void HomeAssistantManager::ensure_wifi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    uint32_t now = millis();
    if (now - last_wifi_attempt_ms_ < HA_WIFI_RECONNECT_INTERVAL_MS) {
        return;
    }
    last_wifi_attempt_ms_ = now;

    LOG_BLE("Home Assistant: connecting Wi-Fi SSID '%s'\n", config_.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config_.wifi_ssid, config_.wifi_password);
}

void HomeAssistantManager::ensure_mqtt() {
    if (mqtt_client_.connected()) {
        return;
    }

    uint32_t now = millis();
    if (now - last_mqtt_attempt_ms_ < HA_MQTT_RECONNECT_INTERVAL_MS) {
        return;
    }
    last_mqtt_attempt_ms_ = now;

    mqtt_client_.setServer(config_.mqtt_host, config_.mqtt_port);

    String will_topic = availability_topic();
    bool connected = false;
    if (config_.mqtt_username[0] != '\0') {
        connected = mqtt_client_.connect(client_id_,
                                         config_.mqtt_username,
                                         config_.mqtt_password,
                                         will_topic.c_str(),
                                         0,
                                         true,
                                         "offline");
    } else {
        connected = mqtt_client_.connect(client_id_,
                                         will_topic.c_str(),
                                         0,
                                         true,
                                         "offline");
    }

    if (connected) {
        on_mqtt_connected();
    } else {
        LOG_BLE("Home Assistant: MQTT connect failed rc=%d\n", mqtt_client_.state());
    }
}

void HomeAssistantManager::on_mqtt_connected() {
    LOG_BLE("Home Assistant: MQTT connected\n");
    publish_mqtt(availability_topic(), "online", true);
    mqtt_client_.subscribe("homeassistant/status");
    String command_filter = String(config_.base_topic) + "/" + device_id_ + "/cmd/#";
    mqtt_client_.subscribe(command_filter.c_str());
    discovery_published_ = false;
    publish_discovery(true);
    publish_all_state(true);
}

void HomeAssistantManager::mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
    if (instance_) {
        instance_->handle_mqtt_message(topic, payload, length);
    }
}

void HomeAssistantManager::handle_mqtt_message(char* topic, uint8_t* payload, unsigned int length) {
    String topic_string(topic);
    String payload_string;
    payload_string.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) {
        payload_string += static_cast<char>(payload[i]);
    }
    payload_string.trim();

    if (topic_string == "homeassistant/status") {
        if (payload_string == "online") {
            publish_discovery(true);
            publish_all_state(true);
        }
        return;
    }

    String prefix = String(config_.base_topic) + "/" + device_id_ + "/cmd/";
    if (!topic_string.startsWith(prefix)) {
        return;
    }

    String command = topic_string.substring(prefix.length());
    const char* reason = "ok";
    bool accepted = apply_command(command, payload_string, &reason);
    publish_command_ack(command.c_str(), accepted, reason);
    if (accepted) {
        publish_all_state(true);
    }
}

void HomeAssistantManager::publish_all_state(bool force) {
    publish_realtime_state(force);
    publish_config_state(force || config_dirty_);
    publish_stats_state(force);
    publish_session_state(force);
}

void HomeAssistantManager::publish_realtime_state(bool force) {
    uint32_t now = millis();
    bool active = grind_controller_ && grind_controller_->is_active();
    uint32_t interval = active ? HA_MQTT_REALTIME_ACTIVE_INTERVAL_MS : HA_MQTT_REALTIME_IDLE_INTERVAL_MS;
    if (!force && now - last_realtime_publish_ms_ < interval) {
        return;
    }
    last_realtime_publish_ms_ = now;

    WeightSensor* sensor = hardware_ ? hardware_->get_weight_sensor() : nullptr;
    float display_weight = sensor ? sensor->get_display_weight() : 0.0f;
    float instant_weight = sensor ? sensor->get_instant_weight() : 0.0f;
    float flow_rate = sensor ? sensor->get_flow_rate() : 0.0f;
    int progress = grind_controller_ ? grind_controller_->get_progress_percent_for_home_assistant() : 0;
    const char* phase_name = grind_controller_ ? grind_controller_->get_current_phase_name() : "UNKNOWN";
    bool motor_on = hardware_ && hardware_->get_grinder() && hardware_->get_grinder()->is_grinding();

    String payload = "{";
    payload += "\"display_weight_g\":" + String(display_weight, 3) + ",";
    payload += "\"instant_weight_g\":" + String(instant_weight, 3) + ",";
    payload += "\"flow_rate_gps\":" + String(flow_rate, 3) + ",";
    payload += "\"phase\":\"" + json_escape(phase_name) + "\",";
    payload += "\"phase_id\":" + String(grind_controller_ ? grind_controller_->get_phase_id() : 0) + ",";
    payload += "\"progress_pct\":" + String(progress) + ",";
    payload += "\"grind_active\":" + String(active ? "true" : "false") + ",";
    payload += "\"motor_on\":" + String(motor_on ? "true" : "false") + ",";
    payload += "\"target_weight_g\":" + String(grind_controller_ ? grind_controller_->get_target_weight() : 0.0f, 3) + ",";
    payload += "\"target_time_ms\":" + String(grind_controller_ ? grind_controller_->get_target_time_ms() : 0) + ",";
    payload += "\"motor_stop_target_weight_g\":" + String(grind_controller_ ? grind_controller_->get_motor_stop_target_weight() : 0.0f, 3) + ",";
    payload += "\"grind_latency_ms\":" + String(grind_controller_ ? grind_controller_->get_grind_latency_ms() : 0.0f, 1) + ",";
    payload += "\"pulse_count\":" + String(grind_controller_ ? grind_controller_->get_pulse_attempt_count() : 0) + ",";
    payload += "\"can_pulse\":" + String((grind_controller_ && grind_controller_->can_pulse()) ? "true" : "false");
    payload += "}";

    publish_mqtt(state_topic("realtime"), payload, true);
}

void HomeAssistantManager::publish_config_state(bool force) {
    uint32_t now = millis();
    if (!force && now - last_config_publish_ms_ < HA_MQTT_CONFIG_INTERVAL_MS) {
        return;
    }
    last_config_publish_ms_ = now;
    config_dirty_ = false;

    Preferences prefs;
    bool auto_start = false;
    bool auto_return = false;
    bool logging_enabled = false;
    bool ble_startup = true;
    bool swipe_enabled = true;
    float normal_brightness = USER_SCREEN_BRIGHTNESS_NORMAL;
    float screensaver_brightness = USER_SCREEN_BRIGHTNESS_DIMMED;
    bool screensaver_startup = false;
    bool screensaver_sleep = false;

    prefs.begin("autogrind", true);
    auto_start = prefs.getBool("auto_start", false);
    auto_return = prefs.getBool("auto_return", false);
    prefs.end();

    prefs.begin("logging", true);
    logging_enabled = prefs.getBool("enabled", false);
    prefs.end();

    prefs.begin("bluetooth", true);
    ble_startup = prefs.getBool("startup", true);
    prefs.end();

    prefs.begin("swipe", true);
    swipe_enabled = prefs.getBool("enabled", true);
    prefs.end();

    prefs.begin("brightness", true);
    normal_brightness = prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
    screensaver_brightness = prefs.getFloat("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED);
    prefs.end();

    prefs.begin("screensaver", true);
    screensaver_startup = prefs.getBool("startup", false);
    screensaver_sleep = prefs.getBool("sleep", false);
    prefs.end();

    int purge_mode = GRIND_PURGE_MODE_DEFAULT;
    float purge_amount = GRIND_PURGE_AMOUNT_DEFAULT_G;
    float freshness_hours = GRIND_FRESHNESS_DEFAULT_HOURS;
    if (hardware_ && hardware_->get_preferences()) {
        auto* p = hardware_->get_preferences();
        purge_mode = p->getInt(GrindController::PREF_KEY_GRINDER_MODE, GRIND_PURGE_MODE_DEFAULT);
        purge_amount = p->getFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, GRIND_PURGE_AMOUNT_DEFAULT_G);
        freshness_hours = p->getFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, GRIND_FRESHNESS_DEFAULT_HOURS);
    }

    BasketDetector* basket = ui_manager_ ? ui_manager_->get_basket_detector() : nullptr;

    int active_profile = profiles_ ? profiles_->get_current_profile() : 0;
    GrindMode mode = profiles_ ? profiles_->get_grind_mode() : GrindMode::WEIGHT;

    String payload = "{";
    payload += "\"active_profile_index\":" + String(active_profile) + ",";
    payload += "\"active_profile\":\"" + String(profile_name_for_index(active_profile)) + "\",";
    payload += "\"grind_mode\":\"" + String(grind_mode_to_state(mode)) + "\",";
    for (int i = 0; i < USER_PROFILE_COUNT; ++i) {
        payload += "\"profile_" + String(i) + "_weight_g\":" + String(profiles_ ? profiles_->get_profile_weight(i) : 0.0f, 2) + ",";
        payload += "\"profile_" + String(i) + "_time_s\":" + String(profiles_ ? profiles_->get_profile_time(i) : 0.0f, 2) + ",";
    }
    payload += "\"purge_mode\":\"" + String(purge_mode == static_cast<int>(GrinderPurgeMode::PRIME) ? "Prime" : "Purge") + "\",";
    payload += "\"purge_amount_g\":" + String(purge_amount, 2) + ",";
    payload += "\"freshness_hours\":" + String(freshness_hours, 2) + ",";
    payload += "\"coast_ratio\":" + String(grind_controller_ ? grind_controller_->get_coast_ratio() : GRIND_LATENCY_TO_COAST_RATIO_DEFAULT, 2) + ",";
    payload += "\"motor_latency_ms\":" + String(grind_controller_ ? grind_controller_->get_motor_response_latency() : GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS, 1) + ",";
    payload += "\"auto_start\":" + String(auto_start ? "true" : "false") + ",";
    payload += "\"auto_return\":" + String(auto_return ? "true" : "false") + ",";
    payload += "\"basket_detection\":" + String((basket && basket->is_enabled()) ? "true" : "false") + ",";
    payload += "\"basket_single_weight_g\":" + String(basket ? basket->get_single_weight() : 0.0f, 2) + ",";
    payload += "\"basket_double_weight_g\":" + String(basket ? basket->get_double_weight() : 0.0f, 2) + ",";
    payload += "\"basket_tolerance_g\":" + String(basket ? basket->get_tolerance() : USER_BASKET_DETECTION_TOLERANCE_DEFAULT_G, 2) + ",";
    payload += "\"logging\":" + String(logging_enabled ? "true" : "false") + ",";
    payload += "\"ble_startup\":" + String(ble_startup ? "true" : "false") + ",";
    payload += "\"swipe_enabled\":" + String(swipe_enabled ? "true" : "false") + ",";
    payload += "\"display_brightness_pct\":" + String(static_cast<int>(normal_brightness * 100.0f + 0.5f)) + ",";
    payload += "\"screensaver_brightness_pct\":" + String(static_cast<int>(screensaver_brightness * 100.0f + 0.5f)) + ",";
    payload += "\"screensaver_startup\":" + String(screensaver_startup ? "true" : "false") + ",";
    payload += "\"screensaver_sleep\":" + String(screensaver_sleep ? "true" : "false");
    payload += "}";

    publish_mqtt(state_topic("config"), payload, true);
}

void HomeAssistantManager::publish_stats_state(bool force) {
    uint32_t now = millis();
    if (!force && now - last_stats_publish_ms_ < HA_MQTT_STATS_INTERVAL_MS) {
        return;
    }
    last_stats_publish_ms_ = now;

    WeightSensor* sensor = hardware_ ? hardware_->get_weight_sensor() : nullptr;
    String payload = "{";
    payload += "\"total_grinds\":" + String(statistics_manager.get_total_grinds()) + ",";
    payload += "\"single_shots\":" + String(statistics_manager.get_single_shots()) + ",";
    payload += "\"double_shots\":" + String(statistics_manager.get_double_shots()) + ",";
    payload += "\"custom_shots\":" + String(statistics_manager.get_custom_shots()) + ",";
    payload += "\"motor_runtime_ms\":" + String(static_cast<unsigned long>(statistics_manager.get_motor_runtime_ms())) + ",";
    payload += "\"device_uptime_h\":" + String(statistics_manager.get_device_uptime_hrs()) + ",";
    payload += "\"device_uptime_min_remainder\":" + String(statistics_manager.get_device_uptime_min_remainder()) + ",";
    payload += "\"total_weight_kg\":" + String(statistics_manager.get_total_weight_kg(), 3) + ",";
    payload += "\"weight_mode_grinds\":" + String(statistics_manager.get_weight_mode_grinds()) + ",";
    payload += "\"time_mode_grinds\":" + String(statistics_manager.get_time_mode_grinds()) + ",";
    payload += "\"time_pulses\":" + String(statistics_manager.get_time_pulses()) + ",";
    payload += "\"avg_accuracy_g\":" + String(statistics_manager.get_avg_accuracy_g(), 3) + ",";
    payload += "\"total_pulses\":" + String(statistics_manager.get_total_pulses()) + ",";
    payload += "\"avg_pulses\":" + String(statistics_manager.get_avg_pulses(), 2) + ",";
    payload += "\"calibrated\":" + String((sensor && sensor->is_calibrated()) ? "true" : "false") + ",";
    payload += "\"hardware_fault\":" + String(sensor ? static_cast<int>(sensor->get_hardware_fault()) : 0) + ",";
    payload += "\"stddev_g\":" + String(sensor ? sensor->get_standard_deviation_g(GRIND_SCALE_PRECISION_SETTLING_TIME_MS) : 0.0f, 4) + ",";
    payload += "\"stddev_adc\":" + String(sensor ? sensor->get_standard_deviation_adc(GRIND_SCALE_PRECISION_SETTLING_TIME_MS) : 0) + ",";
    payload += "\"mechanical_anomalies\":" + String(grind_controller_ ? grind_controller_->get_mechanical_anomaly_count() : 0);
    payload += "}";

    publish_mqtt(state_topic("stats"), payload, true);
}

void HomeAssistantManager::publish_session_state(bool force) {
    static uint32_t last_publish_ms = 0;
    uint32_t now = millis();
    if (!force && now - last_publish_ms < HA_MQTT_CONFIG_INTERVAL_MS) {
        return;
    }
    last_publish_ms = now;

    auto result = grind_controller_
        ? grind_controller_->get_last_session_result()
        : GrindController::GrindSessionResult::UNKNOWN;

    String payload = "{";
    payload += "\"result\":\"" + String(session_result_to_state(result)) + "\",";
    payload += "\"final_weight_g\":" + String(grind_controller_ ? grind_controller_->get_final_weight() : 0.0f, 3) + ",";
    payload += "\"target_weight_g\":" + String(grind_controller_ ? grind_controller_->get_target_weight() : 0.0f, 3) + ",";
    payload += "\"target_time_ms\":" + String(grind_controller_ ? grind_controller_->get_target_time_ms() : 0) + ",";
    payload += "\"error_g\":" + String(grind_controller_ ? (grind_controller_->get_target_weight() - grind_controller_->get_final_weight()) : 0.0f, 3) + ",";
    payload += "\"pulse_count\":" + String(grind_controller_ ? grind_controller_->get_pulse_attempt_count() : 0) + ",";
    payload += "\"mode\":\"" + String(grind_controller_ ? grind_mode_to_state(grind_controller_->get_mode()) : "Weight") + "\",";
    payload += "\"phase\":\"" + json_escape(grind_controller_ ? grind_controller_->get_current_phase_name() : "UNKNOWN") + "\",";
    payload += "\"error_message\":\"" + json_escape(grind_controller_ ? grind_controller_->get_last_error_message() : "") + "\"";
    payload += "}";

    publish_mqtt(state_topic("session_last"), payload, true);
}

void HomeAssistantManager::publish_grind_event(const char* event_type) {
    if (!mqtt_client_.connected()) {
        return;
    }

    String payload = "{";
    payload += "\"event_type\":\"" + json_escape(event_type) + "\",";
    payload += "\"phase\":\"" + json_escape(grind_controller_ ? grind_controller_->get_current_phase_name() : "UNKNOWN") + "\",";
    payload += "\"active\":" + String((grind_controller_ && grind_controller_->is_active()) ? "true" : "false") + ",";
    payload += "\"result\":\"" + String(grind_controller_ ? session_result_to_state(grind_controller_->get_last_session_result()) : "unknown") + "\",";
    payload += "\"final_weight_g\":" + String(grind_controller_ ? grind_controller_->get_final_weight() : 0.0f, 3);
    payload += "}";
    publish_mqtt(String(config_.base_topic) + "/" + device_id_ + "/event/grind", payload, false);
}

void HomeAssistantManager::publish_command_ack(const char* command, bool accepted, const char* reason) {
    String payload = "{";
    payload += "\"command\":\"" + json_escape(command) + "\",";
    payload += "\"accepted\":" + String(accepted ? "true" : "false") + ",";
    payload += "\"reason\":\"" + json_escape(reason ? reason : "") + "\"";
    payload += "}";
    publish_mqtt(String(config_.base_topic) + "/" + device_id_ + "/event/command_ack", payload, false);
}

void HomeAssistantManager::publish_discovery(bool force) {
    if (!mqtt_client_.connected() || (discovery_published_ && !force)) {
        return;
    }

    String realtime = state_topic("realtime");
    String config = state_topic("config");
    String stats = state_topic("stats");
    String session = state_topic("session_last");

    publish_sensor_discovery("display_weight", "Display weight", realtime.c_str(), "{{ value_json.display_weight_g }}", "g", "mdi:scale", nullptr, "measurement", "weight");
    publish_sensor_discovery("instant_weight", "Instant weight", realtime.c_str(), "{{ value_json.instant_weight_g }}", "g", "mdi:scale-balance", nullptr, "measurement", "weight");
    publish_sensor_discovery("flow_rate", "Flow rate", realtime.c_str(), "{{ value_json.flow_rate_gps }}", "g/s", "mdi:speedometer", nullptr, "measurement");
    publish_sensor_discovery("phase", "Phase", realtime.c_str(), "{{ value_json.phase }}", nullptr, "mdi:state-machine");
    publish_sensor_discovery("progress", "Progress", realtime.c_str(), "{{ value_json.progress_pct }}", "%", "mdi:progress-clock", nullptr, "measurement");
    publish_binary_sensor_discovery("grind_active", "Grind active", realtime.c_str(), "{{ 'ON' if value_json.grind_active else 'OFF' }}", "mdi:coffee-maker");
    publish_binary_sensor_discovery("motor_on", "Motor on", realtime.c_str(), "{{ 'ON' if value_json.motor_on else 'OFF' }}", "mdi:engine");
    publish_sensor_discovery("target_weight", "Target weight", realtime.c_str(), "{{ value_json.target_weight_g }}", "g", "mdi:target", nullptr, "measurement", "weight");
    publish_sensor_discovery("motor_stop_target", "Motor stop target", realtime.c_str(), "{{ value_json.motor_stop_target_weight_g }}", "g", "mdi:target-variant", "diagnostic", "measurement", "weight");
    publish_sensor_discovery("grind_latency", "Grind latency", realtime.c_str(), "{{ value_json.grind_latency_ms }}", "ms", "mdi:timer-sand", "diagnostic", "measurement", "duration");
    publish_binary_sensor_discovery("can_pulse", "Pulse available", realtime.c_str(), "{{ 'ON' if value_json.can_pulse else 'OFF' }}", "mdi:plus-circle");

    publish_select_discovery("active_profile", "Active profile", "active_profile", "active_profile", "[\"Single\",\"Double\",\"Custom\"]", "mdi:coffee", nullptr);
    publish_select_discovery("grind_mode", "Grind mode", "grind_mode", "grind_mode", "[\"Weight\",\"Time\"]", "mdi:scale", nullptr);
    publish_select_discovery("purge_mode", "Purge mode", "purge_mode", "purge_mode", "[\"Prime\",\"Purge\"]", "mdi:autorenew", "config");

    for (int i = 0; i < USER_PROFILE_COUNT; ++i) {
        char object_id[40];
        char name[48];
        char key[32];
        snprintf(object_id, sizeof(object_id), "profile_%d_weight", i);
        snprintf(name, sizeof(name), "%s weight", profile_name_for_index(i));
        snprintf(key, sizeof(key), "profile_%d_weight_g", i);
        publish_number_discovery(object_id, name, key, key, USER_MIN_TARGET_WEIGHT_G, USER_MAX_TARGET_WEIGHT_G, USER_FINE_WEIGHT_ADJUSTMENT_G, "g", "mdi:scale", "config");

        snprintf(object_id, sizeof(object_id), "profile_%d_time", i);
        snprintf(name, sizeof(name), "%s time", profile_name_for_index(i));
        snprintf(key, sizeof(key), "profile_%d_time_s", i);
        publish_number_discovery(object_id, name, key, key, USER_MIN_TARGET_TIME_S, USER_MAX_TARGET_TIME_S, USER_FINE_TIME_ADJUSTMENT_S, "s", "mdi:timer", "config");
    }

    publish_number_discovery("purge_amount", "Purge amount", "purge_amount_g", "purge_amount_g", GRIND_PURGE_AMOUNT_MIN_G, GRIND_PURGE_AMOUNT_MAX_G, 0.1f, "g", "mdi:delete-sweep", "config");
    publish_number_discovery("freshness_hours", "Freshness", "freshness_hours", "freshness_hours", 0.5f, 48.0f, 0.5f, "h", "mdi:clock-outline", "config");
    publish_number_discovery("coast_ratio", "Coast ratio", "coast_ratio", "coast_ratio", GRIND_LATENCY_TO_COAST_RATIO_MIN, GRIND_LATENCY_TO_COAST_RATIO_MAX, 0.05f, nullptr, "mdi:ray-end", "config");
    publish_number_discovery("motor_latency_config", "Motor latency", "motor_latency_ms", "motor_latency_ms", GRIND_AUTOTUNE_LATENCY_MIN_MS, GRIND_AUTOTUNE_LATENCY_MAX_MS, 1.0f, "ms", "mdi:timer-sand", "config");
    publish_number_discovery("basket_single_weight", "Single basket weight", "basket_single_weight_g", "basket_single_weight_g", 0.0f, USER_MAX_TARGET_WEIGHT_G, 0.1f, "g", "mdi:filter-variant", "config");
    publish_number_discovery("basket_double_weight", "Double basket weight", "basket_double_weight_g", "basket_double_weight_g", 0.0f, USER_MAX_TARGET_WEIGHT_G, 0.1f, "g", "mdi:filter-variant", "config");
    publish_number_discovery("basket_tolerance", "Basket tolerance", "basket_tolerance_g", "basket_tolerance_g", USER_BASKET_DETECTION_TOLERANCE_MIN_G, USER_BASKET_DETECTION_TOLERANCE_MAX_G, 0.5f, "g", "mdi:plus-minus", "config");
    publish_number_discovery("display_brightness", "Display brightness", "display_brightness_pct", "display_brightness_pct", HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT, 100.0f, 1.0f, "%", "mdi:brightness-6", "config");
    publish_number_discovery("screensaver_brightness", "Screensaver brightness", "screensaver_brightness_pct", "screensaver_brightness_pct", HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT, 100.0f, 1.0f, "%", "mdi:brightness-4", "config");

    publish_switch_discovery("auto_start", "Auto start", "auto_start", "auto_start", "mdi:play-circle", "config");
    publish_switch_discovery("auto_return", "Auto return", "auto_return", "auto_return", "mdi:keyboard-return", "config");
    publish_switch_discovery("basket_detection", "Basket detection", "basket_detection", "basket_detection", "mdi:filter-check", "config");
    publish_switch_discovery("logging", "Session logging", "logging", "logging", "mdi:file-chart", "config");
    publish_switch_discovery("ble_startup", "BLE startup", "ble_startup", "ble_startup", "mdi:bluetooth", "config");
    publish_switch_discovery("swipe_enabled", "Mode swipe", "swipe_enabled", "swipe_enabled", "mdi:gesture-swipe-vertical", "config");
    publish_switch_discovery("screensaver_startup", "Screensaver startup", "screensaver_startup", "screensaver_startup", "mdi:image", "config");
    publish_switch_discovery("screensaver_sleep", "Screensaver sleep", "screensaver_sleep", "screensaver_sleep", "mdi:sleep", "config");

    publish_button_discovery("start_grind", "Start grind", "start_grind", "mdi:play");
    publish_button_discovery("stop_grind", "Stop / return", "stop_grind", "mdi:stop");
    publish_button_discovery("tare", "Tare", "tare", "mdi:scale-balance");
    publish_button_discovery("time_mode_pulse", "Time pulse", "time_mode_pulse", "mdi:plus-circle");

    publish_sensor_discovery("last_result", "Last result", session.c_str(), "{{ value_json.result }}", nullptr, "mdi:check-circle-outline");
    publish_sensor_discovery("last_final_weight", "Last final weight", session.c_str(), "{{ value_json.final_weight_g }}", "g", "mdi:scale", nullptr, "measurement", "weight");
    publish_sensor_discovery("last_error", "Last weight error", session.c_str(), "{{ value_json.error_g }}", "g", "mdi:target", nullptr, "measurement", "weight");
    publish_sensor_discovery("last_pulses", "Last pulses", session.c_str(), "{{ value_json.pulse_count }}", nullptr, "mdi:counter");

    publish_sensor_discovery("total_grinds", "Total grinds", stats.c_str(), "{{ value_json.total_grinds }}", nullptr, "mdi:counter", "diagnostic", "total_increasing");
    publish_sensor_discovery("total_weight", "Total weight", stats.c_str(), "{{ value_json.total_weight_kg }}", "kg", "mdi:scale", "diagnostic", "total_increasing", "weight");
    publish_sensor_discovery("motor_runtime", "Motor runtime", stats.c_str(), "{{ value_json.motor_runtime_ms }}", "ms", "mdi:engine", "diagnostic", "total_increasing", "duration");
    publish_sensor_discovery("avg_accuracy", "Average accuracy", stats.c_str(), "{{ value_json.avg_accuracy_g }}", "g", "mdi:target", "diagnostic", "measurement", "weight");
    publish_binary_sensor_discovery("calibrated", "Scale calibrated", stats.c_str(), "{{ 'ON' if value_json.calibrated else 'OFF' }}", "mdi:scale", "diagnostic");
    publish_sensor_discovery("hardware_fault", "Hardware fault", stats.c_str(), "{{ value_json.hardware_fault }}", nullptr, "mdi:alert", "diagnostic");
    publish_sensor_discovery("noise_stddev", "Noise stddev", stats.c_str(), "{{ value_json.stddev_g }}", "g", "mdi:waves", "diagnostic", "measurement", "weight");
    publish_sensor_discovery("mechanical_anomalies", "Mechanical anomalies", stats.c_str(), "{{ value_json.mechanical_anomalies }}", nullptr, "mdi:alert-outline", "diagnostic");

    publish_event_discovery();
    discovery_published_ = true;
}

void HomeAssistantManager::publish_sensor_discovery(const char* object_id, const char* name, const char* state_topic_value,
                                                    const char* value_template, const char* unit,
                                                    const char* icon, const char* entity_category,
                                                    const char* state_class, const char* device_class) {
    String payload = "{";
    payload += "\"name\":\"" + json_escape(name) + "\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_" + object_id + "\",";
    payload += "\"state_topic\":\"" + json_escape(state_topic_value) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"value_template\":\"" + json_escape(value_template) + "\"";
    if (unit) payload += ",\"unit_of_measurement\":\"" + json_escape(unit) + "\"";
    if (icon) payload += ",\"icon\":\"" + json_escape(icon) + "\"";
    if (entity_category) payload += ",\"entity_category\":\"" + json_escape(entity_category) + "\"";
    if (state_class) payload += ",\"state_class\":\"" + json_escape(state_class) + "\"";
    if (device_class) payload += ",\"device_class\":\"" + json_escape(device_class) + "\"";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("sensor", object_id), payload, true);
}

void HomeAssistantManager::publish_binary_sensor_discovery(const char* object_id, const char* name, const char* state_topic_value,
                                                           const char* value_template, const char* icon,
                                                           const char* entity_category) {
    String payload = "{";
    payload += "\"name\":\"" + json_escape(name) + "\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_" + object_id + "\",";
    payload += "\"state_topic\":\"" + json_escape(state_topic_value) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",";
    payload += "\"value_template\":\"" + json_escape(value_template) + "\"";
    if (icon) payload += ",\"icon\":\"" + json_escape(icon) + "\"";
    if (entity_category) payload += ",\"entity_category\":\"" + json_escape(entity_category) + "\"";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("binary_sensor", object_id), payload, true);
}

void HomeAssistantManager::publish_number_discovery(const char* object_id, const char* name, const char* state_key,
                                                    const char* command_key, float min_value, float max_value,
                                                    float step, const char* unit, const char* icon,
                                                    const char* entity_category) {
    String payload = "{";
    payload += "\"name\":\"" + json_escape(name) + "\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_" + object_id + "\",";
    payload += "\"state_topic\":\"" + json_escape(state_topic("config").c_str()) + "\",";
    payload += "\"command_topic\":\"" + json_escape(command_topic(command_key).c_str()) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"value_template\":\"{{ value_json." + String(state_key) + " }}\",";
    payload += "\"min\":" + String(min_value, 3) + ",";
    payload += "\"max\":" + String(max_value, 3) + ",";
    payload += "\"step\":" + String(step, 3) + ",";
    payload += "\"mode\":\"box\"";
    if (unit) payload += ",\"unit_of_measurement\":\"" + json_escape(unit) + "\"";
    if (icon) payload += ",\"icon\":\"" + json_escape(icon) + "\"";
    if (entity_category) payload += ",\"entity_category\":\"" + json_escape(entity_category) + "\"";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("number", object_id), payload, true);
}

void HomeAssistantManager::publish_select_discovery(const char* object_id, const char* name, const char* state_key,
                                                    const char* command_key, const char* options_json,
                                                    const char* icon, const char* entity_category) {
    String payload = "{";
    payload += "\"name\":\"" + json_escape(name) + "\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_" + object_id + "\",";
    payload += "\"state_topic\":\"" + json_escape(state_topic("config").c_str()) + "\",";
    payload += "\"command_topic\":\"" + json_escape(command_topic(command_key).c_str()) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"value_template\":\"{{ value_json." + String(state_key) + " }}\",";
    payload += "\"options\":" + String(options_json);
    if (icon) payload += ",\"icon\":\"" + json_escape(icon) + "\"";
    if (entity_category) payload += ",\"entity_category\":\"" + json_escape(entity_category) + "\"";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("select", object_id), payload, true);
}

void HomeAssistantManager::publish_switch_discovery(const char* object_id, const char* name, const char* state_key,
                                                    const char* command_key, const char* icon,
                                                    const char* entity_category) {
    String payload = "{";
    payload += "\"name\":\"" + json_escape(name) + "\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_" + object_id + "\",";
    payload += "\"state_topic\":\"" + json_escape(state_topic("config").c_str()) + "\",";
    payload += "\"command_topic\":\"" + json_escape(command_topic(command_key).c_str()) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"state_on\":\"ON\",\"state_off\":\"OFF\",";
    payload += "\"value_template\":\"{{ 'ON' if value_json." + String(state_key) + " else 'OFF' }}\"";
    if (icon) payload += ",\"icon\":\"" + json_escape(icon) + "\"";
    if (entity_category) payload += ",\"entity_category\":\"" + json_escape(entity_category) + "\"";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("switch", object_id), payload, true);
}

void HomeAssistantManager::publish_button_discovery(const char* object_id, const char* name, const char* command_key,
                                                    const char* icon, const char* entity_category) {
    String payload = "{";
    payload += "\"name\":\"" + json_escape(name) + "\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_" + object_id + "\",";
    payload += "\"command_topic\":\"" + json_escape(command_topic(command_key).c_str()) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"payload_press\":\"PRESS\"";
    if (icon) payload += ",\"icon\":\"" + json_escape(icon) + "\"";
    if (entity_category) payload += ",\"entity_category\":\"" + json_escape(entity_category) + "\"";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("button", object_id), payload, true);
}

void HomeAssistantManager::publish_event_discovery() {
    String payload = "{";
    payload += "\"name\":\"Grind event\",";
    payload += "\"unique_id\":\"sgbw_" + String(device_id_) + "_grind_event\",";
    payload += "\"state_topic\":\"" + json_escape((String(config_.base_topic) + "/" + device_id_ + "/event/grind").c_str()) + "\",";
    payload += "\"availability_topic\":\"" + json_escape(availability_topic().c_str()) + "\",";
    payload += "\"value_template\":\"{{ value_json.event_type }}\",";
    payload += "\"event_types\":[\"started\",\"ended\",\"phase_changed\",\"session_result\"]";
    payload += "," + device_info_json() + "," + origin_info_json();
    payload += "}";
    publish_mqtt(discovery_topic("event", "grind_event"), payload, true);
}

bool HomeAssistantManager::publish_mqtt(const String& topic, const String& payload, bool retain) {
    if (!mqtt_client_.connected()) {
        return false;
    }
    bool ok = mqtt_client_.publish(topic.c_str(), payload.c_str(), retain);
    if (!ok) {
        LOG_BLE("Home Assistant: publish failed topic=%s len=%u\n", topic.c_str(), payload.length());
    }
    return ok;
}

String HomeAssistantManager::availability_topic() const {
    return String(config_.base_topic) + "/" + device_id_ + "/availability";
}

String HomeAssistantManager::command_topic(const char* command_key) const {
    return String(config_.base_topic) + "/" + device_id_ + "/cmd/" + command_key;
}

String HomeAssistantManager::state_topic(const char* state_name) const {
    return String(config_.base_topic) + "/" + device_id_ + "/state/" + state_name;
}

String HomeAssistantManager::discovery_topic(const char* platform, const char* object_id) const {
    return String(config_.discovery_prefix) + "/" + platform + "/" + device_id_ + "/" + object_id + "/config";
}

String HomeAssistantManager::device_info_json() const {
    String json = "\"device\":{";
    json += "\"identifiers\":[\"sgbw_" + String(device_id_) + "\"],";
    json += "\"name\":\"" + json_escape(config_.device_name) + "\",";
    json += "\"manufacturer\":\"jaapp\",";
    json += "\"model\":\"Smart Grind-by-Weight\",";
    json += "\"sw_version\":\"" BUILD_FIRMWARE_VERSION "\",";
    json += "\"hw_version\":\"ESP32-S3\"";
    json += "}";
    return json;
}

String HomeAssistantManager::origin_info_json() const {
    String json = "\"origin\":{";
    json += "\"name\":\"Smart Grind-by-Weight\",";
    json += "\"sw_version\":\"" BUILD_FIRMWARE_VERSION "\"";
    json += "}";
    return json;
}

String HomeAssistantManager::json_escape(const char* value) const {
    String escaped;
    if (!value) {
        return escaped;
    }
    for (const char* p = value; *p; ++p) {
        switch (*p) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += *p; break;
        }
    }
    return escaped;
}

bool HomeAssistantManager::command_matches(const String& command, const char* expected) const {
    return command.equalsIgnoreCase(expected);
}

bool HomeAssistantManager::parse_bool_payload(const String& payload, bool* value_out) const {
    if (!value_out) return false;
    if (payload.equalsIgnoreCase("ON") || payload.equalsIgnoreCase("true") || payload == "1") {
        *value_out = true;
        return true;
    }
    if (payload.equalsIgnoreCase("OFF") || payload.equalsIgnoreCase("false") || payload == "0") {
        *value_out = false;
        return true;
    }
    return false;
}

bool HomeAssistantManager::parse_float_payload(const String& payload, float* value_out) const {
    if (!value_out || payload.length() == 0) return false;
    char* end = nullptr;
    float value = strtof(payload.c_str(), &end);
    if (end == payload.c_str() || !std::isfinite(value)) {
        return false;
    }
    *value_out = value;
    return true;
}

bool HomeAssistantManager::parse_profile_index(const String& payload, int* index_out) const {
    if (!index_out) return false;
    if (payload.equalsIgnoreCase("Single") || payload == "0") {
        *index_out = 0;
        return true;
    }
    if (payload.equalsIgnoreCase("Double") || payload == "1") {
        *index_out = 1;
        return true;
    }
    if (payload.equalsIgnoreCase("Custom") || payload == "2") {
        *index_out = 2;
        return true;
    }
    return false;
}

bool HomeAssistantManager::apply_command(const String& command, const String& payload, const char** reason_out) {
    static const char* kOk = "ok";
    static const char* kBadPayload = "invalid_payload";
    static const char* kBusy = "grinder_active";
    static const char* kUnavailable = "unavailable";
    static const char* kUnsupported = "unsupported_command";
    if (reason_out) *reason_out = kOk;

    bool active = grind_controller_ && grind_controller_->is_active();

    if (command_matches(command, "start_grind")) {
        if (!ui_manager_ || !state_machine_ || !state_machine_->is_state(UIState::READY)) {
            if (reason_out) *reason_out = kUnavailable;
            return false;
        }
        ui_manager_->request_remote_action(UIManager::RemoteAction::START_GRIND);
        return true;
    }

    if (command_matches(command, "stop_grind")) {
        if (!ui_manager_ || !state_machine_ ||
            !(state_machine_->is_state(UIState::GRINDING) ||
              state_machine_->is_state(UIState::PURGE_CONFIRM) ||
              state_machine_->is_state(UIState::GRIND_COMPLETE) ||
              state_machine_->is_state(UIState::GRIND_TIMEOUT))) {
            if (reason_out) *reason_out = kUnavailable;
            return false;
        }
        ui_manager_->request_remote_action(UIManager::RemoteAction::STOP_OR_RETURN);
        return true;
    }

    if (command_matches(command, "tare")) {
        if (active) {
            if (reason_out) *reason_out = kBusy;
            return false;
        }
        if (!ui_manager_) {
            if (reason_out) *reason_out = kUnavailable;
            return false;
        }
        ui_manager_->request_remote_action(UIManager::RemoteAction::TARE);
        return true;
    }

    if (command_matches(command, "time_mode_pulse")) {
        if (!grind_controller_ || !grind_controller_->can_pulse() || !ui_manager_) {
            if (reason_out) *reason_out = kUnavailable;
            return false;
        }
        ui_manager_->request_remote_action(UIManager::RemoteAction::TIME_MODE_PULSE);
        return true;
    }

    if (active) {
        if (reason_out) *reason_out = kBusy;
        return false;
    }

    if (command_matches(command, "active_profile")) {
        int index = 0;
        if (!parse_profile_index(payload, &index) || !profiles_) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        profiles_->set_current_profile(index);
        if (ui_manager_) {
            ui_manager_->set_current_tab(index);
            ui_manager_->request_settings_refresh();
        }
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "grind_mode")) {
        if (!profiles_) {
            if (reason_out) *reason_out = kUnavailable;
            return false;
        }
        GrindMode mode;
        if (payload.equalsIgnoreCase("Time")) {
            mode = GrindMode::TIME;
        } else if (payload.equalsIgnoreCase("Weight")) {
            mode = GrindMode::WEIGHT;
        } else {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        profiles_->set_grind_mode(mode);
        if (ui_manager_) {
            ui_manager_->set_current_mode(mode);
            ui_manager_->request_settings_refresh();
        }
        mark_config_dirty();
        return true;
    }

    for (int i = 0; i < USER_PROFILE_COUNT; ++i) {
        String weight_key = "profile_" + String(i) + "_weight_g";
        String time_key = "profile_" + String(i) + "_time_s";
        float value = 0.0f;
        if (command == weight_key) {
            if (!profiles_ || !parse_float_payload(payload, &value)) {
                if (reason_out) *reason_out = kBadPayload;
                return false;
            }
            profiles_->set_profile_weight(i, profiles_->clamp_weight(value));
            if (ui_manager_) ui_manager_->request_settings_refresh();
            mark_config_dirty();
            return true;
        }
        if (command == time_key) {
            if (!profiles_ || !parse_float_payload(payload, &value)) {
                if (reason_out) *reason_out = kBadPayload;
                return false;
            }
            profiles_->set_profile_time(i, profiles_->clamp_time(value));
            if (ui_manager_) ui_manager_->request_settings_refresh();
            mark_config_dirty();
            return true;
        }
    }

    bool bool_value = false;
    if (command_matches(command, "auto_start") || command_matches(command, "auto_return")) {
        if (!parse_bool_payload(payload, &bool_value)) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        Preferences prefs;
        prefs.begin("autogrind", false);
        prefs.putBool(command_matches(command, "auto_start") ? "auto_start" : "auto_return", bool_value);
        prefs.end();
        if (ui_manager_) ui_manager_->request_settings_refresh();
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "basket_detection")) {
        if (!parse_bool_payload(payload, &bool_value) || !ui_manager_ || !ui_manager_->get_basket_detector()) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        ui_manager_->get_basket_detector()->save_enabled(bool_value);
        if (ui_manager_) ui_manager_->request_settings_refresh();
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "logging") || command_matches(command, "ble_startup") ||
        command_matches(command, "swipe_enabled") || command_matches(command, "screensaver_startup") ||
        command_matches(command, "screensaver_sleep")) {
        if (!parse_bool_payload(payload, &bool_value)) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        Preferences prefs;
        if (command_matches(command, "logging")) {
            prefs.begin("logging", false);
            prefs.putBool("enabled", bool_value);
        } else if (command_matches(command, "ble_startup")) {
            prefs.begin("bluetooth", false);
            prefs.putBool("startup", bool_value);
        } else if (command_matches(command, "swipe_enabled")) {
            prefs.begin("swipe", false);
            prefs.putBool("enabled", bool_value);
        } else {
            prefs.begin("screensaver", false);
            prefs.putBool(command_matches(command, "screensaver_startup") ? "startup" : "sleep", bool_value);
        }
        prefs.end();
        if (ui_manager_) ui_manager_->request_settings_refresh();
        mark_config_dirty();
        return true;
    }

    float value = 0.0f;
    if (command_matches(command, "purge_mode")) {
        int purge_mode = -1;
        if (payload.equalsIgnoreCase("Prime") || payload.equalsIgnoreCase("Keep") || payload == "0") {
            purge_mode = static_cast<int>(GrinderPurgeMode::PRIME);
        } else if (payload.equalsIgnoreCase("Purge") || payload.equalsIgnoreCase("Remove") || payload == "1") {
            purge_mode = static_cast<int>(GrinderPurgeMode::PURGE);
        }
        if (purge_mode < 0 || !hardware_ || !hardware_->get_preferences()) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        hardware_->get_preferences()->putInt(GrindController::PREF_KEY_GRINDER_MODE, purge_mode);
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "purge_amount_g") || command_matches(command, "freshness_hours")) {
        if (!parse_float_payload(payload, &value) || !hardware_ || !hardware_->get_preferences()) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        if (command_matches(command, "purge_amount_g")) {
            value = clamp_float(value, GRIND_PURGE_AMOUNT_MIN_G, GRIND_PURGE_AMOUNT_MAX_G);
            hardware_->get_preferences()->putFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, value);
        } else {
            value = clamp_float(value, 0.5f, 48.0f);
            hardware_->get_preferences()->putFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, value);
        }
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "coast_ratio")) {
        if (!parse_float_payload(payload, &value) || !grind_controller_) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        grind_controller_->save_coast_ratio(clamp_float(value, GRIND_LATENCY_TO_COAST_RATIO_MIN, GRIND_LATENCY_TO_COAST_RATIO_MAX));
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "motor_latency_ms")) {
        if (!parse_float_payload(payload, &value) || !grind_controller_) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        grind_controller_->save_motor_latency(clamp_float(value, GRIND_AUTOTUNE_LATENCY_MIN_MS, GRIND_AUTOTUNE_LATENCY_MAX_MS));
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "basket_single_weight_g") ||
        command_matches(command, "basket_double_weight_g") ||
        command_matches(command, "basket_tolerance_g")) {
        if (!parse_float_payload(payload, &value) || !ui_manager_ || !ui_manager_->get_basket_detector()) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        BasketDetector* basket = ui_manager_->get_basket_detector();
        if (command_matches(command, "basket_single_weight_g")) {
            basket->save_single_weight(value);
        } else if (command_matches(command, "basket_double_weight_g")) {
            basket->save_double_weight(value);
        } else {
            basket->save_tolerance(value);
        }
        if (ui_manager_) ui_manager_->request_settings_refresh();
        mark_config_dirty();
        return true;
    }

    if (command_matches(command, "display_brightness_pct") ||
        command_matches(command, "screensaver_brightness_pct")) {
        if (!parse_float_payload(payload, &value)) {
            if (reason_out) *reason_out = kBadPayload;
            return false;
        }
        int percent = static_cast<int>(clamp_float(value, HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT, 100.0f) + 0.5f);
        Preferences prefs;
        prefs.begin("brightness", false);
        prefs.putFloat(command_matches(command, "display_brightness_pct") ? "normal" : "screensaver",
                       static_cast<float>(percent) / 100.0f);
        prefs.end();
        if (ui_manager_) ui_manager_->request_settings_refresh();
        mark_config_dirty();
        return true;
    }

    if (reason_out) *reason_out = kUnsupported;
    return false;
}

void HomeAssistantManager::mark_config_dirty() {
    config_dirty_ = true;
}
