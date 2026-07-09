# Troubleshooting Guide

## Table of Contents

- [Motor Does Not Start](#motor-does-not-start)
- [HX711 Not Detected / Wrong Sample Rate](#hx711-not-detected--wrong-sample-rate)
- [Build Fails or `idf.py` Not Found](#build-fails-or-idfpy-not-found)
- [Grind Timeout Screen](#grind-timeout-screen)
- [Unreliable Pulse Corrections](#unreliable-pulse-corrections)
- [Getting Diagnostic Reports](#getting-diagnostic-reports)

---

## Motor Does Not Start

**Applies to:** Eureka Mignon installations where the grinder motor does not activate when commanded.

### Symptoms
- Motor test function (Menu → Motor Test) does not start the motor
- GRIND button does not start the motor
- All other functionality works normally (screen, weight readings, etc.)

**Note:** If you hear a click and humming sound but no coffee grounds come out, the motor **is** starting correctly - your grinder is clogged, not a wiring issue.

### Root Cause
The motor control wire (Pin 3) and button signal wire (Pin 2) from the Eureka 4-pin plug may be reversed on the Waveshare board connection. The button signal wire does not control the motor.

### Diagnosis

**Option 1: Use Motor Test Function (Recommended)**

Access **Menu → Motor Test** (Tools section) to verify motor connectivity. This button activates the motor for a short test pulse. If the motor does not run during the test, the motor wire is incorrectly connected.

**Option 2: Manual Wire Identification (Advanced - Use with Extreme Caution)**

⚠️ **DANGER:** This test involves live wires and can cause the grinder to start unexpectedly, potentially causing injury or damage. Only attempt if comfortable working with electrical systems.

Using the 4-pin Eureka plug pinout (see `../media/4-pin_Eureka_plug_pinout.png` in documentation):
- **Pin 1**: 5V power supply
- **Pin 2**: Button signal (unused in this project)
- **Pin 3**: Motor control signal *(active-high — motor engages when driven to 3.3-5V)*
- **Pin 4**: Ground

The motor control wire (Pin 3) is the wire that **starts the motor when briefly driven HIGH (touch it to Pin 1 / 5V)**. Test carefully:
1. Ensure grinder is powered and hopper has minimal beans
2. Disconnect the suspect wire from the Waveshare board
3. Briefly touch the wire to 5V (Pin 1) - motor should start
4. **Be prepared for the powerful motor to cause the grinder to twist/jump**
5. **Avoid shorts that could damage the board or cause injury**

### Resolution

**Swap the wire connections:**
- The motor control wire (Pin 3) should connect to Waveshare **GPIO 18**
- The button signal wire (Pin 2) can remain disconnected (unused in this project)

If wires were reversed, swap them so Pin 3 connects to GPIO 18. The Waveshare board has reverse polarity protection for power connections, but the motor/button wires should be correctly identified for proper operation.

**Important:** Wire colors vary significantly between Eureka units - always refer to pin positions rather than wire colors when troubleshooting.

---

## HX711 Not Detected / Wrong Sample Rate

**Applies to:** First-boot issues when the load cell board is missing, miswired, or strapped for 80 SPS.

### Symptoms
- Startup log shows `HX711_NOT_CONNECTED` or `HX711_SAMPLE_RATE_INVALID`.
- Weight display stays at `0.00 g`; grinding and calibration remain disabled.

### Quick Fixes
- **NOT_CONNECTED:** Verify VCC/GND/SCK/DOUT wiring and that the HX711 board is powered.
- **SAMPLE_RATE_INVALID:** Ensure the HX711 `RATE` pin is tied to GND for 10 SPS; a floating/high pin forces 80 SPS and will now block startup.
- After correcting hardware, reboot the scale. The diagnostic clears automatically when healthy samples are detected.

## Build Fails or `idf.py` Not Found

**Applies to:** Building the firmware locally.

### Symptoms
- `idf.py: command not found`, or `tools/grinder.py build` reports the IDF environment is missing.
- CMake errors about missing components on a fresh checkout.

### Root Cause
Firmware is built with a standalone **ESP-IDF** install (v5.4.x). The IDF environment must be sourced, and managed components must be resolved on the first build.

### Resolution
1. Install ESP-IDF v5.4.x (at `~/esp/esp-idf` or on `IDF_PATH`) and source it:
   ```bash
   . ~/esp/esp-idf/export.sh
   ```
2. Build (the IDF component manager fetches managed components on the first run):
   ```bash
   idf.py build            # or: python3 tools/grinder.py build
   ```
3. If the build state is inconsistent, clean and rebuild:
   ```bash
   idf.py fullclean && idf.py build   # or: python3 tools/grinder.py clean
   ```

### Notes
- `sdkconfig` is generated and git-ignored — edit `sdkconfig.defaults`, not `sdkconfig`.
- If OTA leaves the screen broken, recover over USB: `idf.py -p /dev/tty.usbmodemXXXX flash`.

---

## Grind Timeout Screen

**Applies to:** Grinder timing out during operation, showing timeout screen after 30 seconds.

### Symptoms
- Grinder reaches timeout screen during grinding cycle
- Long taring process (>10 seconds)
- Unstable weight readings

### Root Cause
Extended taring due to load cell noise prevents grinding from completing within 30-second limit.

### Diagnosis
Check **Menu → Diagnostics → "Noise Floor"** section (see [Diagnostics System](DOC.md#diagnostics-system) for details). If "Noise level: Too High" (red text) appears persistently, your load cell has sustained noise issues that will cause slow taring (>2 seconds). A warning icon (⚠) will appear in the top-right corner when sustained noise is detected.

### Resolution
1. **Check load cell wiring:**
   - Verify shielding wire connection per wiring diagram
   - Shorten load cell cables if possible
   - Ensure clean, solid connections

2. **Check calibration factor (reference examples only):**

   Calibration factors vary between individual load cells, but extreme deviations may indicate hardware issues. Check your calibration factor in **Menu → Diagnostics → Load Cell Status**. Example values from tested units:
   - **1KG T70 load cell**: ±4400 (example)
   - **0.3KG Mavin Load Cell**: ±6580 (example)

   **Note:** These are examples only and can differ significantly between load cells. Use them as rough reference points, not expected values.

3. **If wiring issues persist:**
   - Increase `GRIND_SCALE_SETTLING_TOLERANCE_G` parameter for more noise tolerance

---

## Unreliable Pulse Corrections

**Applies to:** Pulse corrections failing to produce grounds consistently, requiring multiple correction cycles.

### Symptoms
- Multiple pulse attempts needed to reach target weight
- Inconsistent grounds production during pulse phase
- Overshooting or undershooting target weight frequently

### Root Cause
Motor response latency mismatch between firmware settings and actual hardware characteristics. Default 50ms may not match your specific grinder's relay type (solid-state vs mechanical), voltage (110V vs 220V), or burr inertia.

### Resolution

**Recommended:** Use the Auto-Tune Motor Response feature to automatically calibrate optimal pulse duration for your hardware:

1. **Access auto-tune**: Menu → Tune Pulses (Tools section)
2. **Prepare system**:
   - Ensure beans are in hopper
   - Place dosing cup on scale
   - System will automatically tare
3. **Run calibration**: Process takes 1-2 minutes
   - Priming phase: 500ms pulse to position beans
   - Binary search: Finds minimum reliable pulse duration
   - Verification: Confirms 80%+ success rate across 5 test pulses
4. **Check result**: New motor latency value displayed on completion
   - Typical range: 30-200ms depending on hardware
   - Value saved automatically to device preferences
   - Displayed in **Menu → Diagnostics**

**Verify calibration**: Check **Menu → Diagnostics → Motor Latency** to see current value. Re-run auto-tune if you change grinders, modify relay hardware, or continue experiencing unreliable pulse corrections.

**Manual fallback:** If auto-tune fails, system reverts to safe 50ms default. Check grinder power connection, hopper bean level, and scale setup.

---

## Getting Diagnostic Reports

**Applies to:** Reporting issues on GitHub or troubleshooting system behavior.

### What is a Diagnostic Report?

The diagnostic report is a comprehensive text dump of your device's current state, including:
- Firmware version, build number, and git commit
- System information (uptime, CPU frequency, RAM, flash storage)
- All compile-time constants (grind settings, thresholds, timeouts)
- Runtime statistics (lifetime grinds, total weight)
- Load cell calibration status and noise diagnostics
- Motor latency settings
- Autotune results (if available)

This information helps identify configuration issues, hardware problems, or firmware bugs.

### How to Get a Diagnostic Report

#### Option 1: Web Flasher (Recommended)

1. Visit the **[Web Flasher Diagnostics Tool](https://jaapp.github.io/smart-grind-by-weight)**
2. Click the **"Diagnostics"** tab
3. Click **"Connect & Get Diagnostics"**
4. Select your device from the Bluetooth pairing dialog
5. Wait for the report to complete (~5-10 seconds)
6. Click **"Copy to Clipboard"** or **"Download Report"**
7. Paste the report into your GitHub issue

**Browser Requirements:** Chrome, Edge, or Opera on desktop/Android (Web Bluetooth API required)

#### Option 2: Command Line Tool

If you have the development environment set up:

```bash
# Get diagnostics and display in terminal
python3 tools/grinder.py diagnostics

# Save diagnostics to a file
python3 tools/grinder.py diagnostics --save diagnostics.txt
```

### When to Include a Diagnostic Report

**Always include a diagnostic report when reporting:**
- Grinding accuracy issues (overshooting/undershooting)
- Timeout errors or slow taring
- Load cell calibration problems
- Motor response issues
- Any unexpected behavior or crashes
- Feature requests that depend on your hardware configuration

**The diagnostic report is anonymous** and contains no personal information. It only includes technical device settings and statistics.

### Including Grind Results in Your Report

**For grind-related issues, follow these steps before generating the diagnostic report:**

1. **Enable logging:** Menu → Data → Enable Logging
2. **Reproduce the issue:** Perform at least one grind where the issue occurs
3. **Generate diagnostics:** Use one of the methods above (Web Flasher or Command Line)

The diagnostic report will automatically include your recent grind session data, which is essential for troubleshooting accuracy issues, pulse corrections, timeout problems, and other grind behavior issues. Without recent grind data, it's much harder to diagnose what's going wrong.
