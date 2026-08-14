#pragma once

//==============================================================================
// NETWORK CONFIGURATION CONSTANTS
//==============================================================================
// WiFi and train-arrivals gateway settings. The device polls a small gateway
// service (see gateway/ in the repo) for pre-parsed MTA subway arrival times
// shown by the trains screensaver. Credentials and the gateway URL are
// provisioned over BLE (python3 tools/grinder.py wifi) and stored in NVS.
//
// All constants use the NET_ prefix.

#define NET_PREFS_NAMESPACE "network"                                          // NVS namespace for WiFi/gateway config
#define NET_PREFS_KEY_SSID "ssid"                                              // WiFi SSID (string)
#define NET_PREFS_KEY_PASSWORD "pass"                                          // WiFi password (string)
#define NET_PREFS_KEY_GATEWAY_URL "url"                                        // Gateway base URL, e.g. http://host:8600

#define NET_ARRIVALS_PATH "/api/arrivals"                                      // Gateway endpoint appended to the base URL

#define NET_POLL_INTERVAL_MS 30000                                             // Arrivals fetch interval while the trains screensaver is active
#define NET_DATA_STALE_MS 90000                                                // Snapshot age (3 missed polls) before the screensaver flags arrivals as stale
#define NET_DATA_EXPIRED_MS 300000                                             // Snapshot age after which arrivals are hidden in favor of the error state
#define NET_HTTP_TIMEOUT_MS 5000                                               // HTTP request timeout
#define NET_WIFI_RETRY_INTERVAL_MS 15000                                       // Delay between WiFi reconnect attempts
#define NET_MAX_CONFIG_STRING_LEN 96                                           // Max stored length for SSID/password/URL

#define NET_MAX_ARRIVAL_ITEMS 8                                                // Watches shown by the screensaver (matches gateway limit)
#define NET_MAX_ARRIVAL_MINS 4                                                 // Upcoming arrival times kept per watch
