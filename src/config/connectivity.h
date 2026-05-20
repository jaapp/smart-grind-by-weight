#pragma once

#include "hardware.h"

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
#define WIFI_API_STATUS_PATH "/api/status"
#define WIFI_API_SETTINGS_PATH "/api/settings"
#define WIFI_API_SCREENSAVER_PATH "/api/screensaver"
#define WIFI_API_SCREENSAVER_CLEAR_PATH "/api/screensaver/clear"
#define WIFI_API_BASKET_CAPTURE_SINGLE_PATH "/api/basket/capture/single"
#define WIFI_API_BASKET_CAPTURE_DOUBLE_PATH "/api/basket/capture/double"
#define WIFI_SETUP_PATH "/"

// Screensaver image upload. The web UI converts browser image uploads to this
// raw RGB565 format before sending them to the device.
#define WIFI_SCREENSAVER_PATH "/screensaver.rgb565"
#define WIFI_SCREENSAVER_TEMP_PATH "/screensaver.tmp"
#define WIFI_SCREENSAVER_WIDTH_PX HW_DISPLAY_WIDTH_PX
#define WIFI_SCREENSAVER_HEIGHT_PX HW_DISPLAY_HEIGHT_PX
#define WIFI_SCREENSAVER_BYTES (WIFI_SCREENSAVER_WIDTH_PX * WIFI_SCREENSAVER_HEIGHT_PX * 2)

// Preference keys
#define WIFI_PREF_NAMESPACE "wifi"
#define WIFI_PREF_KEY_SSID "ssid"
#define WIFI_PREF_KEY_PASSWORD "password"
#define WIFI_PREF_KEY_STARTUP "startup"
