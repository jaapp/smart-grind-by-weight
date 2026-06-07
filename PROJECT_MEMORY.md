# Project Memory

Last updated: 2026-06-07, Europe/Berlin.

This file captures the critical local context for continuing work on this project without re-discovering the same device, OTA, UI, and build facts.

## Project

- Repo path: `/Users/work/.codex/worktrees/783e/smart-grind-by-weight`
- Active branch: `codex/coffee-bean-tracking`
- Hardware target: Waveshare ESP32-S3 Touch AMOLED 1.64, `280x456` portrait touchscreen.
- Firmware framework: PlatformIO, Arduino ESP32, LVGL 9, Arduino_GFX, WebServer, WiFi, Preferences/NVS.
- Main production PlatformIO env: `waveshare-esp32s3-touch-amoled-164`
- Native UI preview env: `lvgl-sdl-preview`
- Rescue OTA env: `waveshare-esp32s3-touch-amoled-164-rescue-ota`

## Device Situation

- The ESP32 is installed inside a coffee machine. USB access is not normally available.
- Treat the device as remote-only unless the user explicitly says it is connected by USB.
- Do not switch the Mac WiFi network automatically. The user previously lost access when WiFi was changed.
- The user may use a phone to connect to the grinder setup AP and upload firmware manually.
- The device setup AP SSID is `GrindByWeight-Setup`; default AP IP is intended to be `http://192.168.4.1/`.
- Historical router IP was `192.168.0.141`, hostname `grindbyweight.local`, MAC `10:20:BA:46:06:E0`.
- The Mac in the last OTA attempt was on `192.168.1.202/24` via gateway `192.168.1.1`; it could not reach `192.168.0.141:80`.

## Current Firmware Artifacts

- Latest full production diagnostics build:
  - `/Users/work/.codex/worktrees/783e/smart-grind-by-weight/firmware_cache/build_035.bin`
  - Size about `2.1 MB`
  - SHA256 `99fe32744055fa656c6082de59f948ece225e314ec73a0e69fd9098b3b42f39c`
- Lightweight rescue OTA build:
  - `/Users/work/.codex/worktrees/783e/smart-grind-by-weight/firmware_cache/rescue/rescue_build_036.bin`
  - Alias: `/Users/work/.codex/worktrees/783e/smart-grind-by-weight/firmware_cache/rescue/rescue_latest.bin`
  - Size about `969 KB`
  - SHA256 `f5328a2c72866287c9de3488f6b19c11a6673bc7d003b993b87341c2d5b1e6b7`
- Older full build:
  - `/Users/work/.codex/worktrees/783e/smart-grind-by-weight/firmware_cache/build_033.bin`
  - Does not include reset/heap diagnostics or heartbeat-off changes.

## OTA Status And Recovery

- The old/current device firmware was once confirmed as build `29`, version `1.4.0`.
- On build 29, `/ping` and `/api/status` once worked, but `GET /` returned headers with `Content-Length: 21698` and then zero body bytes before timing out. This matched the broken web UI.
- A full OTA upload of build 33 previously sent only about 137 KB, then failed with `curl: (55) Send failure: Broken pipe`.
- A later attempt to upload rescue build 36 via WiFi to `192.168.0.141` failed before sending bytes:
  - `curl: (28) Failed to connect to 192.168.0.141 port 80`
  - `grindbyweight.local` did not resolve from the Mac.
  - A scan of `192.168.1.x` found only the router.
  - A scan of `192.168.0.x` found many other devices but not the grinder at `.141`.
- If reachable, the manual curl OTA shape is:

```sh
curl --fail --show-error --connect-timeout 5 --max-time 300 \
  -H 'Expect:' \
  -F version=1.4.0 \
  -F firmware=@firmware_cache/rescue/rescue_build_036.bin \
  http://192.168.0.141/ota
```

- Preferred recovery flow if the current full firmware OTA path is flaky:
  1. Upload `firmware_cache/rescue/rescue_build_036.bin`.
  2. After reboot, the touchscreen may be blank/inactive; this is expected because rescue firmware is WiFi + OTA only.
  3. Connect phone to `GrindByWeight-Setup`.
  4. Open `http://192.168.4.1/`.
  5. Upload full production `firmware_cache/build_035.bin`.
  6. Device reboots into the normal app.

## Implemented Feature Work

- Bean tracking exists in source on this branch.
- V1 scope is double-shot only; per-profile bean settings are reserved for later.
- Data model:
  - Up to 8 beans.
  - Backed by Preferences/NVS in a `beans` namespace.
  - `mahlgrad_x2[3]` reserves Single, Double, Custom; V1 uses index `1` for Double.
  - Values `2..100` represent grind size `1.0..50.0` in `0.5` steps.
  - Higher value means coarser.
  - `dose_used_x10` and `purge_used_x10` track grams to 0.1 g.
  - Fields include id, name, roaster, bag size grams, active bean id, double grind size, dose used, purge used.
- Feedback behavior:
  - `Finer` decreases stored Double grind size by `0.5`.
  - `Coarser` increases stored Double grind size by `0.5`.
  - `OK` leaves setting unchanged.
  - `Skip` leaves setting unchanged and exits.
  - Clamp to `1.0..50.0`.
  - Feedback can be pressed multiple times on the feedback page.
- Usage accounting:
  - Successful Double grind adds final settled grind weight to active bean dose usage.
  - Cancelled, stopped, timeout, or non-Double sessions are not counted in V1.
  - Purge tracking is separate when configured/measured.

## Touchscreen UI State

- Advanced ready UI is the default on this feature branch.
- Advanced UI should show:
  - Active bean card at top.
  - `Grind Size` / compact grind size badge, not German `Mahlgrad`.
  - Usage strip.
  - Single, Double, Custom profile row with large touch targets.
  - Grind button.
  - WiFi/Bluetooth icons are small; gear icon was removed because settings are reached by gesture/menu.
- Advanced UI interactions:
  - Left/right swipe changes Single, Double, Custom selection in advanced UI.
  - Swipe up opens the settings/menu screen.
  - Bean card opens bean list.
  - Selecting a bean auto-returns to ready.
  - Profile buttons must work even with no bean selected.
- Normal UI mode is the previous classic profile-page swipe UI. User should be able to choose normal vs advanced UI from settings/web UI.

## Web UI / API State

- Web UI contains settings, firmware OTA, screensaver upload, and bean manager.
- Added API:
  - `GET /api/beans`
  - `POST /api/beans` with actions `create`, `update`, `delete`, `set_active`, `feedback`
- Important recovery routes:
  - `/ping`
  - `/api/status`
  - `/ota` GET lightweight upload page
  - Captive portal routes such as `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`
- Production web root uses chunked/small-content streaming via `send_html_response()` to avoid the old large HTML send failure.

## Diagnostics In Build 35

`/api/status` should include:

- `build`
- `version`
- `uptime_ms`
- `reset_reason`
- `reset_reason_code`
- `free_heap_bytes`
- `min_free_heap_bytes`
- `max_alloc_heap_bytes`
- `free_internal_heap_bytes`
- `largest_internal_block_bytes`
- `psram_size_bytes`
- `free_psram_bytes`
- `cpu_mhz`
- WiFi state, IP, hostname, OTA state, screensaver image state.

Reset interpretation:

- `BROWNOUT`: likely power sag or machine electrical instability.
- `TASK_WDT`, `WDT`, `INT_WDT`: firmware/task blocking.
- `PANIC`: crash.
- `SW`: intentional software restart such as OTA/restart.
- If screen blanks and `uptime_ms` resets near zero afterward, it rebooted.
- If `uptime_ms` keeps increasing, it was display/screensaver behavior.

The 3-4 second screen blanking is suspected to be reboot plus startup screensaver, because `USER_SCREENSAVER_STARTUP_DURATION_DEFAULT_MS` is `3000` and the device previously reported a stored screensaver image.

## Load Cell / HX711

- User said the load cell / HX711 may be disconnected during testing.
- Time grind mode must still work when HX711 is disconnected.
- Avoid assuming the hardware fault is fatal unless the requested flow requires weighing.

## Important Source Files

- Bean controller:
  - `src/controllers/bean_controller.h`
  - `src/controllers/bean_controller.cpp`
- Connectivity/Web/OTA:
  - `src/connectivity/manager.h`
  - `src/connectivity/manager.cpp`
  - `src/config/connectivity.h`
- Touchscreen ready UI:
  - `src/ui/screens/ready_screen.h`
  - `src/ui/screens/ready_screen.cpp`
  - `src/ui/controllers/ready_controller.h`
  - `src/ui/controllers/ready_controller.cpp`
- Bean screens:
  - `src/ui/screens/bean_list_screen.h`
  - `src/ui/screens/bean_list_screen.cpp`
  - `src/ui/screens/bean_feedback_screen.h`
  - `src/ui/screens/bean_feedback_screen.cpp`
- Simulator:
  - `src/simulator/`
  - `platformio.ini` env `lvgl-sdl-preview`
- Rescue OTA:
  - `src/rescue/rescue_ota.cpp`
  - `platformio.ini` env `waveshare-esp32s3-touch-amoled-164-rescue-ota`
- Build/archive scripts:
  - `tools/grinder.py`
  - `tools/build-scripts/pre_build.py`
  - `tools/build-scripts/post_build.py`

## Build And Test Commands

Run from repo root.

```sh
python3 tools/grinder.py preview --click-test
node tools/test-web-ui.mjs
python3 tools/grinder.py build
python3 tools/grinder.py build-rescue
```

Known passing results before this memory file:

- `node tools/test-web-ui.mjs` passed.
- `python3 tools/grinder.py preview --click-test` passed.
- Production build passed and archived `build_035.bin`.
- Rescue OTA build passed and archived `rescue_build_036.bin`.

## Safety Rules For Future Work

- Do not use destructive git commands unless explicitly requested.
- Do not revert unrelated user changes.
- Before OTA, verify the exact target is the grinder using `/ping` and `/api/status`.
- Never OTA a mock build to the real device.
- Prefer rescue firmware for recovery if the current firmware cannot upload full 2.1 MB binaries.
- Do not assume `192.168.4.1` from the Mac is the grinder; in the last check it returned an nginx router page because the Mac was not on the grinder AP.
- If the user can only reach the device by phone, give exact file paths and browser steps rather than trying to switch networks.

