# Fork Improvements

This fork builds on the excellent [jaapp/smart-grind-by-weight](https://github.com/jaapp/smart-grind-by-weight) project and adds a substantial round of usability, stability, and tooling improvements on top of it. Everything below ships in firmware **v2.2.1**.

The changes fall into six themes:

- [UI & Navigation](#ui--navigation)
- [Boot & Performance](#boot--performance)
- [Stability](#stability)
- [Grinding & Tuning](#grinding--tuning)
- [Screensaver & Display](#screensaver--display)
- [Build System & OTA Tooling](#build-system--ota-tooling)

---

## UI & Navigation

### Global navigation bar

Every non-immersive screen now carries a persistent top bar: a contextual **back arrow** and **screen title** on the left, with **Bluetooth and warning status icons** pinned to the right. You always know where you are, and you always have a consistent way back.

- The back arrow reuses each screen's existing cancel path, so behavior is predictable: calibration/autotune return to Settings, edit screens return home, confirmation screens return to their caller, and Settings sub-pages step back to their parent.
- Screens were unified around the bar: per-screen titles and CANCEL buttons are gone, replaced by a single primary action button at the bottom of each screen.
- LVGL's native `lv_menu` header is collapsed to zero height; its title mirrors into the nav bar and its back button drives sub-page navigation, so the menu system and the bar never fight each other.

### Dialog-style calibration

Calibration was restyled as a step-by-step dialog with a per-step primary button (**NEXT → CALIBRATE → DONE**) and a green theme, instead of a screen with scattered controls. Each step tells you exactly what to do and gives you exactly one button to press.

### In-tab sliding action buttons

The home screen tabs (Single / Double / Custom / MENU / Scale) each carry their own in-page action buttons — play, settings gear, or TARE + GRIND — that **slide with the swipe** as part of the tab content. The old floating bottom button now only appears during an actual grind cycle, so the home screen stays clean.

### Scale tab with manual grinding

Scale is a top-level swipe tab with a large live-weight readout and two round buttons:

- **TARE** — zero the scale explicitly (no surprise auto-tare when opening the tab).
- **GRIND** — hold-to-grind: the motor runs only while the button is pressed, with a 10-second safety cap, and stops immediately on release, on leaving the tab, or on any state change.

Because nothing tares around manual grinding, weight **accumulates** against the last tare — ideal for topping off a shot in small increments while watching the number climb.

iOS-style page-indicator dots along the bottom edge show which of the five tabs you're on.

---

## Boot & Performance

### Boot splash

The device boots into a proper splash screen: a centered logo fades in on an opaque black background while hardware and background initialization run, then fades out to the ready screen. The splash has a hard timeout so it can never get stuck.

The logo is a real asset pipeline, not a baked-in bitmap: `assets/boot_logo.png` is the source of truth, and the build **auto-regenerates** the LVGL C asset (`src/ui/assets/boot_logo.c`) via `tools/convert_logo.py` whenever the PNG changes. Swap the PNG, rebuild, done.

### Faster boot

Two changes cut boot time noticeably:

- The **weight sampling task starts immediately after hardware init**, so the >1-second HX711 bring-up overlaps BLE and UI initialization instead of running after them. The task manager adopts the already-running task instead of creating it later.
- The splash's artificial 1.2-second hold was removed — splash duration is now driven by actual hardware readiness plus fade time, nothing more.

### Boot tare

The sampling task issues a one-shot tare on the first valid sample after startup, so the scale reads near zero after every reboot without persisting an offset to flash. The display shows `0.0` until that first tare completes, so the raw load-cell reading never flashes on screen. (This replaced an NVS-persisted tare offset that never round-tripped reliably.)

### Green boot-flash fix

On cold boot the AMOLED panel used to flash green bands — uninitialized display GRAM shown before the first frame rendered. The panel is now cleared to black during display init and initializes with brightness at zero, restoring brightness synchronously once init completes, so garbage pixels are never visible.

---

## Stability

### UI-task stack overflow fix (the big one)

The device used to crash with a double-exception panic after roughly 1–3 minutes of uptime. The root cause was subtle:

`CircularBufferMath::get_min_raw()` / `get_max_raw()` used `alloca` to copy the **entire requested sample window onto the caller's stack**. The screensaver's weight-activity check calls these on the 8 KB UI task stack with a minutes-long window — so the allocation grew with the sample count until it silently overwrote return addresses with raw HX711 readings, and the task jumped into garbage.

The fix: min/max now walk the ring buffer **in place** with zero allocation, and every remaining `alloca` site in the buffer math is hard-capped at 256 samples. This class of failure can't recur.

### BLE reliability

- **OTA GATT registration**: GATT services are registered *before* the NimBLE host starts, fixing BLE OTA under the native ESP-IDF build.
- **Re-enable after idle timeout**: turning Bluetooth back on from Settings used to leave the device advertising but unable to accept connections until a reboot (the host task was never restarted after deinit). Fixed.
- **Advertising watchdog**: the Bluetooth manager reconciles its cached connection flag with the real GAP state and restarts advertising if BLE is enabled, idle, and silent — covering connections that drop without a clean disconnect callback.
- Fixed crashes on BLE toggle, during report generation, and on mid-report disconnect.

### Hardware fault detection

The HX711 load-cell amplifier is monitored at runtime: disconnection is detected and reported, and automatic fault recovery re-establishes communication instead of silently reading garbage.

### Memory architecture

LVGL's heap is routed to **PSRAM** through a custom allocator (`src/hardware/lv_mem_core_psram.cpp`). Without this, LVGL consumed ~127 KB of internal DRAM, leaving too little room for FreeRTOS task stacks — which broke task creation after OTA updates. With the allocator, internal DRAM stays comfortably clear of the task stack budget.

---

## Grinding & Tuning

### Pulse autotune actually works now

The Tune Pulses feature used to hang forever in PRIMING on this hardware. The bug: `Grinder::is_pulse_complete()` detected pulse completion by reading the RMT-driven motor pin back via `digitalRead()` — which returns a constant on the native ESP-IDF build, because there is no input/loop-back path on that pin. The pulse never read as complete. Completion is now **time-based on the requested RMT pulse duration**, which is correct by construction.

### A tune screen that tells you what's happening

While autotune runs you now get:

- A live **"Scale X.XXg +/-X.XXXg"** readout — the same 500 ms standard deviation the settling gate checks, colored green when the scale is settleable and orange when it's too noisy.
- A status headline tracking the phase: *Priming*, *Testing Xms*, *Verifying*.
- On failure, a result screen that shows the **actual failure reason** with a quantitative hint, plus the noise reading frozen at the moment of failure — so "it failed" becomes "the scale was reading ±0.04 g and the gate needs ±0.02 g; fix your vibration problem."

### Hardware adaptability

- **Active-low motor relay support** for relay boards that switch on a low signal.
- **180-degree screen rotation** for mounting the display upside down.

---

## Screensaver & Display

### Two-stage screensaver

Instead of a single sleep timeout, the screensaver now steps down in two independently configurable stages (Menu → Display):

1. **Dim after** (default 1 min) — either dims the current screen, or shows the boot logo centered on black at the dimmed brightness (a **Dim / Logo** style choice). Logo mode engages with a fade-to-dark followed by a slow logo fade-in.
2. **Off after** (default 5 min) — the backlight turns fully off.

Both timeouts use discrete steps from 15 seconds to 30 minutes, plus **Never** to disable a stage. Any touch or weight activity restores the screen instantly, and the screensaver never engages while a grind is in progress. Settings are cached in RAM and refreshed on change, so nothing polls flash storage every UI tick.

---

## Build System & OTA Tooling

### Native ESP-IDF

The firmware was migrated from PlatformIO/Arduino to **native ESP-IDF 5.4** (`idf.py`), and PlatformIO has been removed entirely:

- Entry point is `app_main()`; the IDF main component globs the `src/` tree and manages dependencies through the IDF component manager (`main/idf_component.yml`).
- The display driver moved to Espressif's `esp_lcd_sh8601` component with proper CO5300 panel init.
- `tools/grinder.py` wraps `idf.py` for `build`/`clean` and keeps the BLE-OTA `upload`, data `export`, and Streamlit `report` flows.
- Release CI builds with ESP-IDF and publishes correct web-installer artifacts/offsets.

See [DEVELOPMENT.md](DEVELOPMENT.md) for the full setup.

### BLE delta OTA workflow, preserved under ESP-IDF

Upstream's excellent delta OTA (the uploader asks the device which build it's running, computes a binary diff against the archived image for that build in `firmware_cache/build_NNN.bin`, and sends only the difference) survived the build-system migration intact: firmware discovery now points at the ESP-IDF `build/` output, and `python3 tools/grinder.py build-upload` remains the one-command development loop.

One base-matching rule to know: the delta's source check requires the device's flash to byte-match the cached archive. Flashing directly with `idf.py flash` does **not** increment the build number or archive the image, so after a direct USB flash the next delta OTA would fail its source check and roll back. Either build through `python3 tools/grinder.py build` (which increments and archives), or run the next update with `upload --force-full`.
