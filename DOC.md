# Smart Grind-by-Weight Documentation

Complete build instructions, parts list, and usage guide for the Smart Grind-by-Weight system.

---

## 📋 Table of Contents

- [Parts List](#️-parts-list)
- [Assembly Video](#-assembly-video)
- [Installation & Wiring](#-installation--wiring)
- [Firmware Installation](#-firmware-installation)
- [Initial Calibration](#️-initial-calibration)
- [Usage Guide](#-usage-guide)
- [User Interface Navigation](#️-user-interface-navigation)
- [Bluetooth Connectivity](#-bluetooth-connectivity)
- [Analytics & Data Export](#-analytics--data-export)
- [Algorithm Details](#-algorithm-details)
- [Frequently Asked Questions](#-frequently-asked-questions)
- [Troubleshooting](#-troubleshooting)

---
## 🛠️ Parts List

> **Note:** All links are **for reference only** — sellers and items are **not personally verified** unless explicitly stated.

- **[Waveshare ESP32-S3 1.64" AMOLED Touch Display](https://www.waveshare.com/esp32-s3-touch-amoled-1.64.htm)** — Main controller
- **[HX711 ADC module](https://nl.aliexpress.com/item/1005006851380544.html)** — Load cell amplifier
- **MAVIN or T70 load cell** (0.3 – 1 kg range) — Weight sensor  
  ⚠️ Avoid cheap unshielded small load cells — accuracy will suffer
  - **1 kg:** Recommended. Suits portafilter use cases.
  - **0.3 kg:** Only suitable for dosing cups.
  - **Examples:**
    - [AliExpress T70](https://nl.aliexpress.com/item/1005009409460619.html)
    - [TinyTronics MAVIN](https://www.tinytronics.nl/en/sensors/weight-pressure-force/load-cells/mavin-load-cell-0.3kg)
    - [Mavin 0.3 / 1 kg](https://www.alibaba.com/product-detail/subject_1601564701384.html)
    - [T70 1 kg](https://nl.aliexpress.com/item/1005008658337192.html)
  - **Tested:** Only the 1 kg T70 and 0.3 kg Mavin load cells have been personally verified
- **6× M3 screws** (≈10 mm) — Mounting hardware
- **1000 µF capacitor** (10 V) — Power filtering [Example (untested)](https://nl.aliexpress.com/item/1005008995345289.html)
- **Wires & Dupont connectors** — General wiring  
  Example: [22 AWG silicone wire set](https://www.aliexpress.com/item/2255800441309579.html)
- **Dupont connector kit** — [Example (untested)](https://nl.aliexpress.com/item/1005008995345289.html)
- **Angled pin headers** — [Example (untested)](https://nl.aliexpress.com/item/1005006149080284.html)
- **[JST-PH 4-pin male connector (optional)](https://nl.aliexpress.com/item/1005009479983500.html)** — Optional solder-free connection to Eureka

[<img src="media/waveshare_board_wired_up_1.jpg" alt="Wired Waveshare Board" width="30%">](media/waveshare_board_wired_up_1.jpg)

### 3D Printed Parts

All parts designed to print **without supports**. Keep the orientation of the STL files. Some holes are covered with thin plastic layers that you can easily remove.

**Print Settings:**
- **Material**: PETG (preferred) - Flexible enough for snap fits to work properly
- **Layer Height**: 0.2mm
- **Alternative**: PLA might work but will offer a reduced experience due to brittleness

- **[Screen adapter](3d_files/Waveshare%20AMOLED%201_64%20adapter.stl)** - Mounts Waveshare screen to Eureka location
- **[Back plate](3d_files/Back%20plate.stl)** - Mounts to Eureka and holds HX711/load cell  
- **[Cover plate](3d_files/Cover.stl)** - Clean finishing cover
- **[Dosing cup holder](3d_files/Cup%20holder.stl)** - Connects to load cell for dosing cup
- **[Screw hole covers](3d_files/Cup%20holder%20hole%20cover.stl)** - Hides screws and protects against coffee grounds

### Fusion 360 Source Files
- **[All components](https://a360.co/3HYgubb)** - Customizable source files

Use these to adjust mounts for your specific grinder. Tuned for 54mm dosing cups. 
Compatible dosing cup: [AliExpress 54mm Cup](https://nl.aliexpress.com/item/1005006526852408.html)

---

## 📹 Assembly Video

Watch the complete Eureka Mignon Specialita assembly process: **[YouTube Assembly Guide](https://youtu.be/-kfKjiwJsGM)**

---

## 🔌 Installation & Wiring

[<img src="media/wiring_diagram.png" alt="Wiring Diagram" width="50%">](media/wiring_diagram.png)

### Pin Configuration

**HX711 Load Cell Amplifier Connections:**
```
ESP32-S3 GPIO 2    →    HX711 SCK
ESP32-S3 GPIO 3    →    HX711 DOUT
ESP32-S3 3.3V      →    HX711 VCC
ESP32-S3 GND       →    HX711 GND
```

**Load Cell to HX711 Wiring:**
```
Load Cell           HX711
Red (E+)         →  E+
Black (E-)       →  E-
White (A-)       →  A-
Green (A+)       →  A+
Yellow (Shield)  →  GND
```

- Connect the load cell shield wire (usually yellow) to the HX711 GND
- The HX711 only has 1 GND pin - solder the shield wire to the backside of the pin header
- **Tip**: Keep the load cell wire as short as possible to reduce noise

**Eureka Mignon Connections:**

⚠️ **CRITICAL WARNING:** Always verify your specific Eureka's wiring independently! Wire colors vary between units and cannot be trusted. Use the numbered pin positions shown in the reference image.

Using the 4-pin Eureka plug pinout (see `media/4-pin_Eureka_plug_pinout.png`), counting from left to right with the plug oriented with 'ribs' towards you:

[<img src="media/4-pin_Eureka_plug_pinout.png" alt="4-Pin Eureka Plug Pinout" width="50%">](media/4-pin_Eureka_plug_pinout.png)

```
ESP32-S3 5V        →    Pin 1 (5V power)
                        Pin 2 (Button signal - not used in this project)
ESP32-S3 GPIO 18   →    Pin 3 (Motor control signal)
ESP32-S3 GND       →    Pin 4 (Ground)
```

**4-Pin Eureka Plug Reference (Left to Right):**
- **Pin 1**: 5V power supply
- **Pin 2**: Button signal (unused in this project)  
- **Pin 3**: Motor control signal
- **Pin 4**: Ground

⚠️ **VERIFY 5V:** Use a multimeter to confirm 5V pin - wire colors vary between units! The Waveshare board has reverse polarity protection, and button/motor wires can be swapped without risk of damage.

### Installation Steps

1. **Flash the firmware** on the Waveshare board (see Firmware Instructions below)
2. **Add the 1000μF capacitor** between 5V and ground (protects against brownouts)
3. **Create HX711 to Waveshare connection:**
   - Add angled pin headers to HX711 (VCC, GND, DOUT, SCK pins)
   - Connect dupont cables to Waveshare board
   - Load cell can be directly soldered to HX711 (Make the wires as short as possible. Connect shield wire as well to GND)
4. **For Eureka Mignon assembly:**
   - Disassemble top plate and front plate
   - Remove the button and store it (not needed)
   - Use JST-PH plug to connect to Waveshare board
   - **WARNING:** Wire colors vary significantly between Eureka units - always verify pin functions with a multimeter before connecting!
   - Mount Waveshare screen using 3D printed adapter where original screen was (the Waveshare screen with adapter replaces the original screen and reuses the original mounting screws)
   - Fish HX711 wire through housing, exit via button hole
   - Mount load cell and HX711 to 3D printed back plate
   - Clip 3D printed back plate onto Eureka Mignon
   - Connect plug to HX711
   - Add 3D printed cover plate and screw down
   - Add 3D printed dosing cup holder on load cell and screw down
   - Hide screws with 3D printed screw covers
5. **Calibrate load cell** (see [Initial Calibration](#️-initial-calibration)) 

---

## 🚀 Firmware Installation

### 🌐 Web Flasher (Recommended)
**[🔗 Open Web Flasher Tool](https://jaapp.github.io/smart-grind-by-weight)**

**Browser Compatibility:**
- ✅ **Chrome** (Desktop & Android) - Full support
- ✅ **Microsoft Edge** (Desktop) - Full support
- ❌ **Safari/iOS** - Not supported (use command line method below)
- ❌ **Firefox** - Not supported (use command line method below)

**Two-Step Installation Process:**

1. **Initial Setup (USB - One Time Only)**
   - Connect ESP32 via USB cable
   - Use Chrome/Edge browser (desktop or Android)
   - Select firmware version from dropdown
   - Click "Flash via USB" - opens ESP Web Tools
   - After installation, device is ready for wireless updates

2. **Future Updates (BLE - After Installation)**
   - Power on the grinder and enable Bluetooth in device settings
   - Select firmware version from OTA dropdown
   - Click "Connect to Device" → "Flash Firmware"
   - Update happens wirelessly - **no USB cable needed**

**Key Benefits:**
- ✅ **No downloads needed** - firmware hosted automatically
- ✅ **No command line** - simple web interface
- ✅ **Automatic version listing** - all releases available in dropdown
- ✅ **Wireless updates** - once installed, never need USB again

*Initial USB flashing powered by [ESP Web Tools](https://esphome.github.io/esp-web-tools/)*

### Command Line (Fallback)
```bash
# First time (USB) - if web flasher unavailable
python3 tools/grinder.py upload smart-grind-by-weight-vX.X.X.bin

# Updates (BLE) - Enable device Bluetooth first  
python3 tools/grinder.py upload smart-grind-by-weight-vX.X.X.bin
```

**Manual firmware download:** [Releases page](https://github.com/jaapp/smart-grind-by-weight/releases)  
**Build from source:** See [DEVELOPMENT.md](DEVELOPMENT.md)

---

## ⚖️ Initial Calibration

After flashing firmware, calibrate the load cell for accurate measurements:

1. **Access calibration**: Settings → Tools → Press "Calibrate"
2. **Empty calibration**: Remove all weight from scale platform → Press OK
3. **Weight calibration**: 
   - Place known weight on scale (e.g., coffee mug with water)
   - Use +/- buttons to adjust displayed value to match actual weight
   - Press OK to complete

**Tip**: A coffee mug with water makes ideal calibration weight - weigh it on kitchen scale first.

### Auto-Tune Motor Response

The auto-tune feature models your grinder's motor response behavior by measuring the physical lag between relay activation and grounds production. This accounts for hardware variations like voltage differences (110V vs 220V), relay types (solid-state vs mechanical), and burr inertia across different grinder models. The default 50ms value works well for most setups, but if you experience unreliable pulse corrections or want to minimize coffee waste through hardware-specific optimization, run auto-tune via **Settings → Tools → Auto-Tune Motor Response**. The 1-2 minute calibration process finds the minimum reliable pulse duration for your specific hardware and saves it automatically.

### Diagnostics System

The system includes comprehensive load cell health monitoring accessible via **Settings → Diagnostics**. A warning icon (⚠) appears in the top-right corner when diagnostics are active - tap it to navigate directly to the diagnostics page.

**Diagnostic Types:**
1. **Load Cell Not Calibrated** - Appears until calibration is completed via Settings → Tools → Calibrate
2. **Sustained Noise** - Triggers after 60 seconds of excessive noise; clears after 120 seconds of acceptable levels
3. **Mechanical Instability** - Detects sudden weight drops during grinding (3+ events); auto-resets on next grind or via manual reset

**Displayed Values:**
- **Motor Latency** - Current motor response latency in milliseconds (default: 50ms, or calibrated value from auto-tune)
- **Calibration Factor** - Load cell calibration factor from Settings → Tools → Calibrate

**Noise Floor Diagnostics:**

Access via **Settings → Diagnostics → Noise Floor**.

**Three values displayed:**
1. **Standard Deviation (grams)** - Noise level in calibrated weight units
2. **Standard Deviation (ADC)** - Raw sensor noise values
3. **Noise Level Indicator** - Shows if noise will cause slow taring (>2s) or timeouts

**Important:** Noise diagnostics require prior calibration as they're based on calibrated gram values. High noise readings indicate wiring issues (check shield connection, use shorter wire leads). Read diagnostics in a stable, vibration-free environment for accurate assessment.

---

## 📱 Usage Guide

### Grinding Profiles
All profiles are fully customizable. Default grind-by-weight targets (fallback time values shown in parenthesis):
- **Single**: 9 g (5 s)
- **Double**: 18 g (10 s)  
- **Custom**: 21.5 g (12 s)

> 💡 **Tip** – the target label always shows the active unit (`g` or `s`). Long-press to edit in whichever mode you are currently using.

### Navigation
- **Swipe left/right** to navigate between menu tabs
- **Swipe up/down** on the ready screen to toggle between grind-by-weight and grind-by-time modes (when enabled in Settings → Grind Mode)
- **Tap** to select profiles or buttons
- **Long press** on profile targets to edit/customize them

> **Color cues:** The GRIND button background turns **red** in weight mode and **blue** in time mode, so you always know which behaviour is armed.

### Grind Mode Settings
Access **Settings → Grind Mode** to configure:
- **Swipe Gestures**: Enable/disable vertical swipe gestures for mode switching (default: disabled)
- **Time Mode**: Directly toggle between Weight and Time modes regardless of swipe setting

### Basic Operation
These steps describe the default grind-by-weight workflow:
1. Select profile by tapping on the main screen
2. Long press the profile target to edit/customize the weight if needed
3. Place the dosing cup on the scale platform
4. Press the GRIND button – the scale will tare automatically
5. The system grinds to the precise target weight using the predictive algorithm
6. GRIND COMPLETE shows the final settled weight in grams (with statistics)

Need the stock timed run? Enable swipe gestures in **Settings → Grind Mode**, then swipe up or down on the ready screen before you start; the GRIND button background turns blue to confirm time mode is active (red = weight). Alternatively, use the direct **Time Mode** toggle in settings.

> **Time mode pulse button:** In time mode completion, a "+" button appears next to OK for 100ms additional grinding pulses.

### Display Modes
- **Arc Layout**: Clean, minimal arc-based interface
- **Nerdy Layout**: Detailed charts showing flow rates and real-time grinding analytics
- **Switching**: Tap anywhere on grind screen to switch between layouts during grinding

---

## 🗺️ User Interface Navigation

```
Main Screen (swipe left/right between tabs, up/down to toggle weight/time mode if enabled)
|
+-- Single Profile 
|   |-- Weight display (long press to edit)
|   \-- GRIND button (red=weight, blue=time)
|
+-- Double Profile
|   |-- Weight display (long press to edit)
|   \-- GRIND button (red=weight, blue=time)
|
+-- Custom Profile
|   |-- Weight display (long press to edit)
|   \-- GRIND button (red=weight, blue=time)
|   \-- Time mode completion: OK + PULSE buttons
|
\-- Settings (hierarchical menu navigation)
    |
    +-- System Info
    |   |-- Firmware version & build number
    |   |-- Real-time weight sensor data (instant, samples, raw)
    |   |-- Uptime display
    |   \-- Memory usage
    |
    +-- Diagnostics
    |   |-- Load Cell Status (calibration flag, calibration factor)
    |   |-- Noise Floor (std dev g/ADC, noise level indicator)
    |   |-- Active diagnostic warnings
    |   \-- Reset diagnostics button
    |
    +-- Bluetooth
    |   |-- Bluetooth toggle (30m timer)
    |   |-- Bluetooth startup toggle (configurable auto-enable)
    |   |-- Connection status display
    |   \-- Auto-disable timer display
    |
    +-- Display
    |   |-- Normal brightness slider
    |   \-- Screensaver brightness slider
    |
    +-- Grind Mode
    |   |-- Swipe Gestures toggle (enable/disable vertical swipes)
    |   \-- Time Mode toggle (direct weight/time mode selection)
    |
    +-- Tools
    |   |-- Tare button
    |   |-- Calibrate button
    |   |-- Auto-Tune Motor Response button
    |   \-- Motor test button
    |
    \-- Data
        |-- Logging toggle (enable/disable session file writing)
        |-- Sessions count
        |-- Events count
        |-- Measurements count
        |-- Purge data button (clears logged grind sessions)
        \-- Factory reset button

During Grinding:
|-- Weight display & progress
|-- Tap anywhere: Arc ↔ Nerdy display modes
\-- STOP button
```

---

## 🔵 Bluetooth Connectivity

Bluetooth can be configured in **Settings → Bluetooth** with optional auto-startup (5-minute timer) or manual control (30-minute timer when manually enabled). The blue Bluetooth symbol in the top-right corner indicates when active. Bluetooth enables wireless firmware updates via BLE OTA, grind data export and analytics, and device management. Grind session logging is configurable in **Settings → Data → Logging** (disabled by default to prevent flash wear) and must be enabled before grinding to save session data for later analysis.

---

## 📊 Analytics & Data Export

⚠️ **Important**: Grind session logging is **disabled by default** to prevent unnecessary flash wear. To analyze grind data:

1. **Enable logging** in **Settings → Data → Logging** before grinding
2. Perform your grind sessions (data will be saved to flash storage)
3. **Export and analyze** the data using the tools below
4. **Disable logging** again when analysis is complete (recommended for daily use)

### Launch Interactive Dashboard
```bash
# Export data and launch Streamlit dashboard
python3 tools/grinder.py analyze

# Or view reports from existing data
python3 tools/grinder.py report
```

### Available Tools
```bash
python3 tools/grinder.py --help          # Show all available commands
python3 tools/grinder.py scan            # Scan for BLE devices
python3 tools/grinder.py connect         # Connect to grinder device  
python3 tools/grinder.py debug           # Stream live debug logs
python3 tools/grinder.py info            # Get device system information
python3 tools/grinder.py export          # Export grind data to database
```

### Tools Directory Structure
- **`grinder.py`**: Cross-platform Python tool for all operations (build, upload, analyze)
- **`ble/`**: BLE communication tools and OTA update system
- **`streamlit-reports/`**: Interactive data visualization and analytics
- **`database/`**: SQLite database management for grind session storage

---

## 🧠 Algorithm Details

### Grinding Algorithm

The system uses a **zero-shot learning algorithm** requiring no prior knowledge or manually tuned variables. It instantly adapts to changes in temperature, humidity, grinding coarseness, bean type, and hardware characteristics.

**Multi-Phase Approach:**

1. **Initialization & Taring Phase**
   - Automatic tare on grind button press
   - 30-second timeout from grind start to completion
   - Noise-adaptive settling detection

2. **Predictive Phase**
   - Learns flow rate and motor-to-cup latency (relay + motor inertia + burr spin-up)
   - Predicts when to stop motor based on measured flow and coast characteristics
   - Target: barely undershoot target weight (overshoot is unrecoverable)
   - Uses runtime-configurable motor response latency (30-200ms, default 50ms)

3. **Pulse Correction Phase**
   - Conservative pulse duration calculation using 95th percentile flow rate
   - Bounded pulses respect hardware-specific motor response latency
   - Pulses range from motor latency minimum to latency + 225ms maximum
   - Mechanical instability detection (3+ sudden weight drops triggers diagnostic)
   - Repeats until target ± tolerance reached

4. **Time Mode Additional Pulses**
   - Dedicated `TIME_ADDITIONAL_PULSE` phase for post-completion grinding
   - 100ms fixed pulse duration
   - Split-button UI: OK + PULSE buttons on completion screen

**Motor Response Latency Model:**

The motor response latency represents the physical system lag between relay activation and grounds production. This value is hardware-specific and accounts for:
- Relay closure time (solid-state vs mechanical relays)
- Motor inertia (110V vs 220V motors)
- Burr spin-up characteristics (different grinder models/designs)

The latency value is automatically calibrated via **Auto-Tune Motor Response** (Settings → Tools) using binary search with statistical verification, or uses a safe 50ms default. This enables universal grinder compatibility without firmware modifications.

**Key Features:**
- Noise-resistant through multi-modal load cell measurement (instant, smoothed, filtered)
- Hardware-adaptive pulse control via runtime motor latency
- Conservative approach: undershoots target, then corrects with bounded pulses
- Mechanical instability detection with hysteresis and persistence
- 30-second grind timeout protection with user acknowledgment requirement

---

## ❓ Frequently Asked Questions

**Will this modification work on grinders other than the Eureka Mignon Specialita?**

See the comprehensive **[Grinder Compatibility Matrix](GRINDER_COMPATIBILITY.md)** for detailed compatibility information across different grinder models, including confirmed compatible models, adaptation requirements, and installation methods.

**Can I use this to grind directly into a portafilter instead of a dosing cup?**

Yes, but requires modifications: use 1kg load cell (vs 0.3kg) for better accuracy with heavier portafilters. Design and 3D print custom portafilter holder mounting to load cell. The dosing cup holder design serves as reference for portafilter adapter.

---

## 🔧 Troubleshooting

For common build and setup issues, see **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)**.

---

For additional support, refer to the project repository issues section, but please note that support availability is limited as mentioned in the project status.
