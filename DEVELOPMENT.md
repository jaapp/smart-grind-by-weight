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

The project has two main build targets:

### Production Target: `waveshare-esp32s3-touch-amoled-164`
- **Use case:** Real hardware with load cell and grinder connected
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:** All functionality enabled

### Mock/Development Target: `waveshare-esp32s3-touch-amoled-164-mock`
- **Use cases:** 
  - Development without connected load cell or grinder
  - Or; Testing with device installed in grinder without wasting beans or taxing the motor
- **Hardware:** Can run on just the ESP32-S3 Waveshare board (without HX711 or grinder) OR with full hardware installed
- **Features:** 
  - Simulated load cell readings (green background indicates mock HX711 driver is active)
  - Mock grinder motor (visual indicator instead of relay activation)
**Mock mode benefits:**
- Develop UI changes without affecting the actual grinder
- Bring your waveshare board with you for coding on and testing the road :)
- Work on new features without hardware setup or bean waste
- Capture USB serial messages for debugging

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

**Build mock/development firmware:**
```bash
# Using PlatformIO directly for specific target
python3 tools/venv/bin/python -m platformio run -e waveshare-esp32s3-touch-amoled-164-mock
```

**Clean build artifacts:**
```bash
python3 tools/grinder.py clean
```

### Initial USB Flashing

For the first-time setup or when BLE isn't working:

```bash
# Build and upload via USB (production)
python3 tools/grinder.py build
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164

# Or for mock target
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164-mock
```

### BLE OTA Updates (After Initial Setup)

Once the device is running and connected to Bluetooth:

```bash
# Build and upload wirelessly (production)
python3 tools/grinder.py build-upload

# Upload specific firmware file
python3 tools/grinder.py upload path/to/firmware.bin

# Force full firmware update (skip delta patching)
python3 tools/grinder.py build-upload --force-full

# Scan for BLE devices
python3 tools/grinder.py scan

# Get device system info
python3 tools/grinder.py info
```

---

## 📦 Release Process

For maintainers creating releases:

```bash
# Interactive release creation with custom release notes
python3 tools/grinder.py release
```

The release process:
1. **Interactive prompts** for version increment and custom release notes
2. **Local version update** - commits updated firmware version to repo
3. **Annotated git tag** - created with your custom release notes
4. **Automatic GitHub Actions** - builds and creates release with both custom notes and automatic changelog


See **[RELEASES.md](RELEASES.md)** for detailed release workflow documentation and release note examples.

---

## 🐛 Debugging

### Serial Monitor

```bash
# Monitor serial output via PlatformIO
python3 tools/venv/bin/python -m platformio device monitor
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