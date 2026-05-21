# Home Assistant Integration

The firmware includes a native Home Assistant MQTT Discovery bridge. Home
Assistant only needs its built-in MQTT integration and a reachable MQTT broker;
no HACS component or custom Home Assistant plugin is required.

The bridge is disabled by default so existing builds keep the same behavior
unless MQTT details are provided. It uses the firmware's built-in WiFi
connectivity manager, so configure WiFi from the device setup AP or WiFi menu
first.

## Enable At Build Time

Add a local PlatformIO environment with build flags for your broker:

```ini
[env:waveshare-esp32s3-touch-amoled-164-ha]
extends = env:waveshare-esp32s3-touch-amoled-164
build_flags =
    ${env:waveshare-esp32s3-touch-amoled-164.build_flags}
    -DHA_INTEGRATION_ENABLED_DEFAULT=true
    '-DHA_MQTT_HOST="192.168.1.10"'
    -DHA_MQTT_PORT=1883
    '-DHA_MQTT_USERNAME="mqtt-user"'
    '-DHA_MQTT_PASSWORD="mqtt-password"'
```

Username and password are optional if the broker allows anonymous clients.
`HA_WIFI_SSID` and `HA_WIFI_PASSWORD` remain available as a fallback for custom
builds that do not pass the firmware connectivity manager into the Home
Assistant bridge.

## Runtime Configuration

The implementation also reads the `ha` Preferences namespace, which allows a
future provisioning UI or BLE command to set these values without rebuilding:

| Key | Type | Default |
| --- | --- | --- |
| `enabled` | bool | `HA_INTEGRATION_ENABLED_DEFAULT` |
| `wifi_ssid` | string | `HA_WIFI_SSID` |
| `wifi_pass` | string | `HA_WIFI_PASSWORD` |
| `mqtt_host` | string | `HA_MQTT_HOST` |
| `mqtt_port` | ushort | `HA_MQTT_PORT` |
| `mqtt_user` | string | `HA_MQTT_USERNAME` |
| `mqtt_pass` | string | `HA_MQTT_PASSWORD` |
| `disc_prefix` | string | `homeassistant` |
| `base_topic` | string | `smartgrind` |
| `dev_name` | string | `Smart Grind-by-Weight` |

## MQTT Topics

Topics include the ESP32 MAC-derived device id:

```text
smartgrind/<device_id>/availability
smartgrind/<device_id>/state/realtime
smartgrind/<device_id>/state/config
smartgrind/<device_id>/state/stats
smartgrind/<device_id>/state/session_last
smartgrind/<device_id>/event/grind
smartgrind/<device_id>/event/command_ack
smartgrind/<device_id>/cmd/<command>
```

Home Assistant discovery config is published under:

```text
homeassistant/<platform>/<device_id>/<object_id>/config
```

The bridge also listens for `homeassistant/status` and republishes discovery and
current state when Home Assistant announces `online`.

## Exposed Entities

Telemetry:

- Display weight, instant weight, flow rate, phase, progress, target weight,
  motor stop target, latency, pulse count, motor state, scale calibration state,
  noise, hardware fault, mechanical anomaly count.
- Last grind result, final weight, target, error, pulse count, mode, phase, and
  error message.
- Lifetime statistics including total grinds, total coffee weight, motor
  runtime, average accuracy, mode counts, and pulse counts.

Controls:

- Buttons: start grind, stop or return, tare, time-mode pulse.
- Selects: active profile, grind mode, purge mode.
- Numbers: profile weights and times, purge amount, freshness window, coast
  ratio, motor latency, basket weights and tolerance, display brightness,
  screensaver brightness.
- Switches: auto start, auto return, basket detection, session logging, BLE
  startup, mode swipe, screensaver startup, screensaver sleep.

Most configuration commands are rejected while the grinder is active. Start,
stop, tare, and pulse commands are handled through the UI task so LVGL and grind
state transitions stay serialized with the existing UI flow.
