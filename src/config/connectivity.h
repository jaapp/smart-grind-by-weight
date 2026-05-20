#pragma once

//==============================================================================
// CONNECTIVITY CONFIGURATION
//==============================================================================

#define CONNECTIVITY_TRANSPORT_WIFI 1
#define CONNECTIVITY_TRANSPORT_BLE 2

#ifndef CONNECTIVITY_TRANSPORT
#define CONNECTIVITY_TRANSPORT CONNECTIVITY_TRANSPORT_WIFI
#endif

#define CONNECTIVITY_DEVICE_NAME "GrindByWeight"

// Wi-Fi station/setup mode
#define WIFI_HOSTNAME "grindbyweight"
#define WIFI_SETUP_AP_SSID "GrindByWeight-Setup"
#define WIFI_SETUP_AP_PASSWORD ""
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RECONNECT_INTERVAL_MS 30000

// Embedded HTTP server for setup/status/OTA
#define WIFI_HTTP_PORT 80
#define WIFI_OTA_PATH "/ota"
#define WIFI_STATUS_PATH "/status"
#define WIFI_SETUP_PATH "/"

// Preference keys
#define WIFI_PREF_NAMESPACE "wifi"
#define WIFI_PREF_KEY_SSID "ssid"
#define WIFI_PREF_KEY_PASSWORD "password"
#define WIFI_PREF_KEY_STARTUP "startup"

