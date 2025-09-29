# Troubleshooting Guide

## Unknown board ID 'esp32-s3-devkitc-1'

**Applies to:** Windows development environments using PlatformIO with ESP32-S3 boards.

### Symptoms
- `platformio run` fails with `UnknownBoard: Unknown board ID 'esp32-s3-devkitc-1'`.
- `pio boards espressif32 | findstr "esp32-s3"` shows the board, suggesting it exists in the registry.

### Root Cause
The local PlatformIO `espressif32` platform package was pinned to an older release (e.g. 3.1.0 from 2021) that predates ESP32-S3 board definitions. The registry lookup reports current boards, but the outdated local package lacks the corresponding JSON definition.

### Resolution
1. Update the global `espressif32` platform package so it includes ESP32-S3 board definitions:
   ```powershell
   C:\Users\<user>\.platformio\penv\Scripts\platformio.exe pkg install -g -p platformio/espressif32@^6.8.0
   ```
   *(Alternatively, run `pio platform update espressif32`.)*
2. Re-run the build:
   ```powershell
   C:\Users\<user>\.platformio\penv\Scripts\platformio.exe run
   ```
3. Optional: Pin the desired platform version in `platformio.ini` to ensure consistency across machines:
   ```ini
   [env:waveshare-esp32s3-touch-amoled-164]
   platform = platformio/espressif32@^6.8.0
   ```

### Notes
- The warning `Ignore unknown configuration option 'monitor_options'` also disappears once PlatformIO core and platform packages are current.
- If multiple PlatformIO installations are present, ensure you are updating the instance used for the project.

---

## PlatformIO Project Initialization Issues

**Applies to:** General PlatformIO project setup issues, especially when using PlatformIO (as this project uses the pioarduino fork as a platform).

### Symptoms
- `Error: Could not find one of 'package.json' manifest files in the package`
- PlatformIO fails to initialize the project properly
- Vague platform-related errors during project setup

### Root Cause
The use of the pioarduino platform fork can sometimes cause PlatformIO's cache or project state to become inconsistent, leading to initialization failures.

### Resolution
1. **Close VS Code completely**
2. **Delete the PlatformIO cache folder:**
   ```bash
   # On Windows
   rmdir /s "%USERPROFILE%\.platformio"
   
   # On macOS/Linux  
   rm -rf ~/.platformio
   ```
3. **Restart VS Code**
4. **Reopen the project** - PlatformIO will reinitialize and download the correct platform packages
5. **Perform a clean build** - Use PlatformIO's "Clean" then "Build" to ensure a fresh compilation

### Alternative (Less Nuclear)
If you want to try a less aggressive approach first:
1. Close VS Code
2. Delete only the platforms cache: `~/.platformio/platforms/` (or `%USERPROFILE%\.platformio\platforms\` on Windows)
3. Restart VS Code and reopen project
4. Perform a clean build in PlatformIO

**Note:** This issue is specific to the pioarduino platform fork usage and the way PlatformIO handles custom platform URLs.

---

## Grind Timeout Screen

**Applies to:** Grinder timing out during operation, showing timeout screen after 30 seconds.

### Symptoms
- Grinder reaches timeout screen during grinding cycle
- Long taring process (>10 seconds)
- Unstable weight readings

### Root Cause
Extended taring due to load cell noise prevents grinding from completing within 30-second limit.

### Resolution
1. **Check load cell wiring:**
   - Verify shielding wire connection per wiring diagram
   - Shorten load cell cables if possible
   - Ensure clean, solid connections

2. **If wiring issues persist:**
   - Increase `GRIND_SCALE_SETTLING_TOLERANCE_G` parameter for more noise tolerance
