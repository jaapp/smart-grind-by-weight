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

This creates a Python virtual environment (`tools/venv`) with the BLE, analysis, and asset-conversion dependencies. Firmware itself is built with a standalone **ESP-IDF** install (see below) — not PlatformIO.

---

## 🔧 Build Variants

The firmware is a single ESP-IDF build. Behaviour is toggled with `DEBUG_*` macros in `src/config/` rather than separate build presets:

### Production (default)
- **Use case:** Real hardware with load cell and grinder connected
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:** All functionality enabled, `-Ofast` optimization

### Debug
- **Use case:** Development and debugging with real hardware
- **Enable via:** `DEBUG_*` macros in `src/config/` (e.g. UI serial delay, touch-driver error suppression)

### Mock/Development
- **Use cases:**
  - Development without a connected load cell or grinder
  - Testing in the grinder without wasting beans or taxing the motor
- **Hardware:** Runs on a bare ESP32-S3 Waveshare board, or with full hardware installed
- **Enable via:** `DEBUG_ENABLE_LOADCELL_MOCK` (simulated readings, green background) and the mock grinder macro in `src/config/`

**Mock mode benefits:**
- Develop UI changes without affecting the actual grinder
- Bring your waveshare board with you for coding and testing on the road :)
- Work on new features without hardware setup or bean waste
- Capture USB serial messages for debugging

---

## 🚀 Building & Flashing

### Build System

Firmware is built with native **ESP-IDF** (`idf.py`). `tools/grinder.py` wraps `idf.py` for `build`/`clean` and keeps the BLE-OTA `upload`/`export`/`report` flows.

**Prerequisites:**
- A standalone ESP-IDF install (v5.4.x). Either export it on `IDF_PATH` or install it at `~/esp/esp-idf`.
- Target chip: **ESP32-S3** with the Waveshare AMOLED touch display.

`sdkconfig` is generated from `sdkconfig.defaults` and git-ignored — edit `sdkconfig.defaults`, never `sdkconfig` directly. The custom partition table (`partitions.csv`) is wired via `sdkconfig.defaults`.

Mock/debug behaviour (simulated load cell, extra logging) is controlled by the `DEBUG_*` macros in `src/config/` rather than by separate build presets.

### Build Commands

**Build firmware:**
```bash
python3 tools/grinder.py build
# or, from an ESP-IDF prompt:
idf.py build
```

**Clean build artifacts:**
```bash
python3 tools/grinder.py clean
```

### Initial USB Flashing

For first-time setup or when BLE isn't working, flash directly over USB from an ESP-IDF prompt:

```bash
. ~/esp/esp-idf/export.sh
idf.py -p /dev/tty.usbmodemXXXX flash
```

### BLE OTA Updates (After Initial Setup)

Once the device is running and connected to Bluetooth:

```bash
# Build and upload wirelessly (production)
python3 tools/grinder.py build-upload

# Upload specific firmware file
python3 tools/grinder.py upload path/to/smart-grind-by-weight-vX.X.X.bin

# Force full firmware update (skip delta patching)
python3 tools/grinder.py build-upload --force-full

# Scan for BLE devices
python3 tools/grinder.py scan

# Get device system info
python3 tools/grinder.py info
```

---

## 📦 Release Process

For maintainers creating releases, see **[RELEASES.md](RELEASES.md)** for detailed release workflow documentation.

---

## 🐛 Debugging

### Serial Monitor

```bash
# Monitor USB serial output (from an ESP-IDF prompt)
idf.py -p /dev/tty.usbmodemXXXX monitor
```

### BLE Debug Monitoring

```bash
# Live debug monitoring via BLE
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