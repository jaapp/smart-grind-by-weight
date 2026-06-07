# Development Guide

This guide is for developers who want to build the Smart Grind-by-Weight firmware from source, contribute to the project, or modify the code for their own use.

**End users:** If you just want to use the device, download pre-built firmware from [Releases](https://github.com/jaapp/smart-grind-by-weight/releases) instead.

---

## 🛠️ Development Setup

### Prerequisites

- **Python 3.8+** with pip
- **Git** for version control
- **USB cable** for initial firmware flashing
- **Hardware** (ESP32-S3 board, HX711, load cell) for testing

### Initial Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/jaapp/smart-grind-by-weight.git
   cd smart-grind-by-weight
   ```

2. **Install development dependencies:**
   ```bash
   python3 tools/grinder.py install
   ```

This automatically creates a virtual environment and installs all required dependencies including PlatformIO.

---

## 🔧 Build Targets

The project has the following build targets:

### Production Target: `waveshare-esp32s3-touch-amoled-164`
- **Use case:** Real hardware with load cell and grinder connected
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:** All functionality enabled
- **Optimizations:** `-Ofast` optimization level for performance

### Debug Target: `waveshare-esp32s3-touch-amoled-164-debug`
- **Use case:** Development and debugging with real hardware
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:**
  - All functionality enabled
  - Debug symbols included
  - 2-second UI serial delay for easier debugging
  - Serial monitor filters to suppress harmless touch driver errors

### Mock/Development Target: `waveshare-esp32s3-touch-amoled-164-mock`
- **Use cases:**
  - Development without connected load cell or grinder
  - Testing with device installed in grinder without wasting beans or taxing the motor
- **Hardware:** Can run on just the ESP32-S3 Waveshare board (without HX711 or grinder) OR with full hardware installed
- **Features:**
  - Simulated load cell readings (green background indicates mock HX711 driver is active)
  - Mock grinder motor (visual indicator instead of relay activation)
  - Debug features enabled

**Mock mode benefits:**
- Develop UI changes without affecting the actual grinder
- Bring your waveshare board with you for coding and testing on the road :)
- Work on new features without hardware setup or bean waste
- Capture USB serial messages for debugging

### Native Touchscreen Preview: `lvgl-sdl-preview`
- **Use case:** Daily UI iteration without flashing hardware
- **Screen:** Runs the real LVGL UI at the Waveshare touchscreen size, `280 x 456`
- **Interactive mode:** Uses SDL for mouse/touch-style interaction on desktop
- **Screenshot mode:** Uses an offscreen LVGL display so captures work in headless/sandboxed environments
- **Mocked services:** Uses simulator stubs for Arduino timing, Preferences storage, profile/grinder state, connectivity callbacks, and bean data

The preview is not an HTML/browser mock. It builds the same LVGL screen code used by the firmware.

```bash
# Build the preview target and capture Ready, Bean List, and Feedback screenshots
python3 tools/grinder.py preview

# Capture specific scenes
python3 tools/grinder.py preview ready list feedback

# Open the interactive SDL preview window
python3 tools/grinder.py preview --interactive ready
```

Screenshots are written to `.pio/preview` by default. PlatformIO's package cache for this workflow is kept under `.pio-core` so the preview setup remains local to the repository.

### Rescue OTA Target: `waveshare-esp32s3-touch-amoled-164-rescue-ota`
- **Use case:** A minimal recovery firmware that only runs WiFi (AP + station) and the `/ota` upload page
- **When to use:** If a production build ever leaves the device unreachable, USB-flash this firmware and then upload a good production `.bin` from its web page
- **Build:** `python3 tools/grinder.py build-rescue`

**Build hygiene:** Only the production environment advances the build counter in `.build_number`; mock, debug, preview and rescue builds report the current number without incrementing it. The production build excludes the `rescue/`, `simulator/` and (legacy) `bluetooth/` sources via `build_src_filter`, so those development/recovery environments never leak into the shipped firmware.

---

## 🚀 Building & Flashing

### Development Platform

This project uses the **pioarduino ESP32 platform** (a community fork) instead of the standard Espressif ESP32 platform. This ensures proper support for the Waveshare ESP32-S3 AMOLED display.

**Platform Details:**
- **Platform**: [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) (stable release)
- **Framework**: Arduino ESP32 Core 3.x
- **Target**: ESP32-S3 with AMOLED touch display

The platform dependency is automatically handled by PlatformIO via the `platformio.ini` configuration.

### Build Commands

**Build production firmware:**
```bash
python3 tools/grinder.py build
```

**Build debug firmware:**
```bash
python3 tools/venv/bin/python -m platformio run -e waveshare-esp32s3-touch-amoled-164-debug
```

**Build mock/development firmware:**
```bash
python3 tools/venv/bin/python -m platformio run -e waveshare-esp32s3-touch-amoled-164-mock
```

**Clean build artifacts:**
```bash
python3 tools/grinder.py clean
```

### Initial USB Flashing

For first-time setup, recovery, or when WiFi is not configured yet:

```bash
# Build and upload via USB (production)
python3 tools/grinder.py build
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164

# Or for debug target
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164-debug

# Or for mock target
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164-mock
```

### WiFi OTA Updates (After Initial Setup)

Once the device is running and connected to WiFi:

```bash
# Required local safety gate before remote-only updates
python3 tools/grinder.py safety-check

# Build and upload wirelessly over WiFi (production)
python3 tools/grinder.py build-upload

# Upload specific firmware file
python3 tools/grinder.py upload path/to/smart-grind-by-weight-vX.X.X.bin

# Upload to a specific IP/host
python3 tools/grinder.py upload --host 192.168.4.1 path/to/firmware.bin

# Legacy BLE upload for older firmware only
python3 tools/grinder.py upload --transport ble path/to/firmware.bin

# Legacy BLE helpers for older firmware only
python3 tools/grinder.py scan

# Get device system info from older BLE firmware
python3 tools/grinder.py info
```

Remote-only devices must use `python3 tools/grinder.py build-upload` for normal updates. It runs the same safety gate automatically before building, then verifies `/ping`, `/api/status`, `/api/settings`, and `/api/beans` on the live device before OTA starts. Do not bypass `--skip-safety-checks` unless the device is physically reachable by USB.

The production firmware keeps setup AP recovery reachable at `http://192.168.4.1` when station WiFi cannot connect. Unknown GET routes in setup AP mode intentionally fall back to the recovery page so phone captive-portal probes do not strand the device.

**Connectivity behavior:** The device connects to station (home) WiFi without blocking — the web server stays responsive while it associates or reconnects, and it falls back to the setup AP only after the connect attempt times out. Once connected, reach it at `http://grindbyweight.local` or its DHCP IP.

**OTA mechanism:** Firmware uploads stream directly into the inactive OTA partition via the Arduino `Update` library, which erases flash incrementally as data arrives. There is no multi-second up-front partition erase, so the upload does not stall and the device only reboots after the image is fully written and verified.

---

## 📦 Release Process

For maintainers creating releases, see **[RELEASES.md](RELEASES.md)** for detailed release workflow documentation.

---

## 🐛 Debugging

### Serial Monitor

```bash
# Monitor serial output via PlatformIO
python3 tools/venv/bin/python -m platformio device monitor
```

### BLE Debug Monitoring (Legacy Firmware)

```bash
# Live debug monitoring via BLE on legacy builds
python3 tools/grinder.py debug
```

**⚠️ BLE Monitoring Limitations:**
- **Boot messages are missed** - BLE connection establishes after device boot
- **Kernel panics not captured** - System-level crashes bypass BLE and go directly to serial
- **Framework messages missing** - Low-level Arduino/ESP-IDF messages don't route through BLE
- **Best for application debug** - Primarily receives debug messages from the smart-grind-by-weight firmware itself

For complete debugging (including boot sequence and system messages), use USB serial monitoring.

---

## 📚 Additional Documentation

- **[DOC.md](DOC.md)** - Complete build instructions and parts list
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - Common issues and solutions
- **[GRINDER_COMPATIBILITY.md](GRINDER_COMPATIBILITY.md)** - Adapting to different grinder models
- **[RELEASES.md](RELEASES.md)** - Release process and versioning
