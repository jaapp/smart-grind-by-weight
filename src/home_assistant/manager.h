#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "../config/constants.h"
#include "../controllers/grind_mode.h"

class HardwareManager;
class StateMachine;
class ProfileController;
class GrindController;
class UIManager;
class ConnectivityManager;

class HomeAssistantManager {
public:
    HomeAssistantManager();

    void init(HardwareManager* hardware,
              StateMachine* state_machine,
              ProfileController* profiles,
              GrindController* grind_controller,
              UIManager* ui_manager,
              ConnectivityManager* connectivity_manager = nullptr);
    void handle();

    bool is_enabled() const { return config_.enabled; }
    bool is_connected() { return mqtt_client_.connected(); }
    void publish_all_state(bool force = false);
    void publish_discovery(bool force = false);

private:
    struct Config {
        bool enabled = HA_INTEGRATION_ENABLED_DEFAULT;
        char wifi_ssid[33] = HA_WIFI_SSID;
        char wifi_password[65] = HA_WIFI_PASSWORD;
        char mqtt_host[65] = HA_MQTT_HOST;
        uint16_t mqtt_port = HA_MQTT_PORT;
        char mqtt_username[65] = HA_MQTT_USERNAME;
        char mqtt_password[65] = HA_MQTT_PASSWORD;
        char discovery_prefix[33] = HA_MQTT_DISCOVERY_PREFIX;
        char base_topic[49] = HA_MQTT_BASE_TOPIC;
        char device_name[49] = HA_MQTT_DEVICE_NAME;
    };

    void load_config();
    void build_device_identity();
    bool has_required_config() const;
    void ensure_wifi();
    void ensure_mqtt();
    void on_mqtt_connected();
    void handle_mqtt_message(char* topic, uint8_t* payload, unsigned int length);
    static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length);

    void publish_realtime_state(bool force);
    void publish_config_state(bool force);
    void publish_stats_state(bool force);
    void publish_session_state(bool force);
    void publish_grind_event(const char* event_type);
    void publish_command_ack(const char* command, bool accepted, const char* reason);

    void publish_sensor_discovery(const char* object_id, const char* name, const char* state_topic,
                                  const char* value_template, const char* unit = nullptr,
                                  const char* icon = nullptr, const char* entity_category = nullptr,
                                  const char* state_class = nullptr, const char* device_class = nullptr);
    void publish_binary_sensor_discovery(const char* object_id, const char* name, const char* state_topic,
                                         const char* value_template, const char* icon = nullptr,
                                         const char* entity_category = nullptr);
    void publish_number_discovery(const char* object_id, const char* name, const char* state_key,
                                  const char* command_key, float min_value, float max_value,
                                  float step, const char* unit = nullptr,
                                  const char* icon = nullptr, const char* entity_category = nullptr);
    void publish_select_discovery(const char* object_id, const char* name, const char* state_key,
                                  const char* command_key, const char* options_json,
                                  const char* icon = nullptr, const char* entity_category = nullptr);
    void publish_switch_discovery(const char* object_id, const char* name, const char* state_key,
                                  const char* command_key, const char* icon = nullptr,
                                  const char* entity_category = nullptr);
    void publish_button_discovery(const char* object_id, const char* name, const char* command_key,
                                  const char* icon = nullptr, const char* entity_category = nullptr);
    void publish_event_discovery();

    bool publish_mqtt(const String& topic, const String& payload, bool retain);
    String availability_topic() const;
    String command_topic(const char* command_key) const;
    String state_topic(const char* state_name) const;
    String discovery_topic(const char* platform, const char* object_id) const;
    String device_info_json() const;
    String origin_info_json() const;
    String json_escape(const char* value) const;

    bool command_matches(const String& command, const char* expected) const;
    bool parse_bool_payload(const String& payload, bool* value_out) const;
    bool parse_float_payload(const String& payload, float* value_out) const;
    bool parse_profile_index(const String& payload, int* index_out) const;
    bool apply_command(const String& command, const String& payload, const char** reason_out);
    void mark_config_dirty();

    HardwareManager* hardware_ = nullptr;
    StateMachine* state_machine_ = nullptr;
    ProfileController* profiles_ = nullptr;
    GrindController* grind_controller_ = nullptr;
    UIManager* ui_manager_ = nullptr;
    ConnectivityManager* connectivity_manager_ = nullptr;

    Config config_;
    WiFiClient wifi_client_;
    PubSubClient mqtt_client_;

    char device_id_[25] = {0};
    char client_id_[33] = {0};

    uint32_t last_wifi_attempt_ms_ = 0;
    uint32_t last_mqtt_attempt_ms_ = 0;
    uint32_t last_realtime_publish_ms_ = 0;
    uint32_t last_config_publish_ms_ = 0;
    uint32_t last_stats_publish_ms_ = 0;
    uint8_t last_phase_id_ = 255;
    uint8_t last_session_result_ = 255;
    bool last_grind_active_ = false;
    bool config_dirty_ = true;
    bool discovery_published_ = false;

    static HomeAssistantManager* instance_;
};

extern HomeAssistantManager home_assistant_manager;
