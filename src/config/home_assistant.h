#pragma once

//==============================================================================
// HOME ASSISTANT / MQTT CONFIGURATION
//==============================================================================
// Defaults are intentionally disabled. They can be overridden with build flags or
// stored later in the "ha" Preferences namespace.

#ifndef HA_INTEGRATION_ENABLED_DEFAULT
#define HA_INTEGRATION_ENABLED_DEFAULT false
#endif

#ifndef HA_WIFI_SSID
#define HA_WIFI_SSID ""
#endif

#ifndef HA_WIFI_PASSWORD
#define HA_WIFI_PASSWORD ""
#endif

#ifndef HA_MQTT_HOST
#define HA_MQTT_HOST ""
#endif

#ifndef HA_MQTT_PORT
#define HA_MQTT_PORT 1883
#endif

#ifndef HA_MQTT_USERNAME
#define HA_MQTT_USERNAME ""
#endif

#ifndef HA_MQTT_PASSWORD
#define HA_MQTT_PASSWORD ""
#endif

#ifndef HA_MQTT_DISCOVERY_PREFIX
#define HA_MQTT_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef HA_MQTT_BASE_TOPIC
#define HA_MQTT_BASE_TOPIC "smartgrind"
#endif

#ifndef HA_MQTT_DEVICE_NAME
#define HA_MQTT_DEVICE_NAME "Smart Grind-by-Weight"
#endif

#define HA_WIFI_RECONNECT_INTERVAL_MS 15000
#define HA_MQTT_RECONNECT_INTERVAL_MS 5000
#define HA_MQTT_REALTIME_ACTIVE_INTERVAL_MS 1000
#define HA_MQTT_REALTIME_IDLE_INTERVAL_MS 5000
#define HA_MQTT_CONFIG_INTERVAL_MS 30000
#define HA_MQTT_STATS_INTERVAL_MS 60000
#define HA_MQTT_PACKET_BUFFER_SIZE 2048
