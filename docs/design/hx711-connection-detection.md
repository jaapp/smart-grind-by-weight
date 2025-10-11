# HX711 Connection Detection Design

## Goal

Keep the ESP32-S3 stable when the HX711 board or load-cell wiring is missing during an initial build. Instead of rebooting, surface a clear diagnostic while leaving the UI, BLE, and tooling responsive.

## Crash Analysis (current firmware)

- With the HX711 absent the DOUT pin floats HIGH. `HX711Driver::begin()` waits ~400 ms for a sample, times out, and returns `false`.
- `WeightSamplingTask::initialize_hx711_hardware()` ignores that return value. It marks the hardware ready, starts the task loop, and keeps calling `sample_and_feed_weight_sensor()`.
- The sampling loop pushes into `CircularBufferMath`, which uses multiple `alloca()` calls. The task has a 4 KB stack (`SYS_TASK_WEIGHT_SAMPLING_STACK_SIZE`), so we eventually trip GCC’s stack guard and reboot with “Stack smashing protect failure!”.
- The existing waits are already bounded—there is no infinite loop—so the reboot happens after the failure, not instead of it.

## Revised Detection & Recovery Strategy

1. **Harden `HX711Driver::begin()` as the “not connected” gate.** Configure `dout_pin` with a pulldown so a missing amplifier resolves to a deterministic HIGH, keep the existing timeout, and propagate the `false` return.
2. **Reuse `HX711Driver::validate_hardware()` for the “no data” diagnostic.** If fewer than three samples arrive inside its timeout window, flag the load cell as providing unusable data (floating inputs, swapped wires, etc.).
3. **Publish diagnostics from `WeightSamplingTask::initialize_hx711_hardware()`.** After each boot attempt it sets or clears diagnostics through `DiagnosticsController`, then decides whether to start the task loop.
4. **Guard the task loop.** If either diagnostic is active, exit `WeightSamplingTask::task_impl()` gracefully (or never set `hardware_validation_passed`). The sampling loop never runs without a healthy HX711, so the stack smash path disappears.
5. **Runtime disconnects stay out of scope.** Once boot succeeds we accept the current YOLO behaviour; this change only targets first-boot wiring mistakes.

## Diagnostic States

Add two new entries to `DiagnosticCode`, message text in `DiagnosticsController::get_diagnostic_message()`, and any UI/BLE surfaces that enumerate diagnostics.

| Code | Message | Trigger | Clear condition |
|------|---------|---------|-----------------|
| `HX711_NOT_CONNECTED` | “HX711 sensor not connected. Check wiring and restart.” | `HX711Driver::begin()` returns `false`. | Next boot where `begin()` succeeds. |
| `HX711_NO_DATA` | “HX711 not providing valid data. Check load cell wiring and restart.” | `HX711Driver::validate_hardware()` returns `false`. | Next boot where validation collects three samples. |

The mock driver bypasses both checks so development builds stay noise-free.

## Boot Flow Changes

1. `WeightSamplingTask::initialize_hx711_hardware()` performs the existing power cycle, then calls `weight_sensor->begin()`.
   - Failure:
     - Set `DiagnosticCode::HX711_NOT_CONNECTED`.
     - Skip calibration loading and sampling setup.
     - Leave `hardware_initialized` and `hardware_validation_passed` false so the task loop exits.
2. On success, run the 2 s stabilization window and call `weight_sensor->validate_hardware()`.
   - Failure:
     - Set `DiagnosticCode::HX711_NO_DATA`.
     - Do not start sampling; leave state flags false.
3. Only when both calls succeed:
   - Clear the two diagnostics.
   - Mark the sensor hardware-initialized and validation-passed.
   - Start the sampling loop (unchanged behaviour).
4. `task_impl()` checks `hardware_validation_passed` (or a new helper) before entering the sampling loop. If unchecked, it logs the diagnostic and exits cleanly.

## GPIO Configuration

- Inside `HX711Driver::begin()` (or its helpers) set `pinMode(dout_pin, INPUT_PULLDOWN)` before the wait loop so a missing sensor yields a clean HIGH.
- Document an optional board-level pulldown so users wiring their own HX711 boards achieve the same behaviour even if ESP32 pin configuration changes elsewhere.

## System Behaviour With Diagnostics Active

- Weight sampling, grinding, and calibration remain disabled.
- UI navigation, settings, diagnostics viewer, BLE, and OTA continue to run.
- Displayed weight is pinned to `0.00 g` while the diagnostic is active.
- BLE logs should emit the diagnostic message once per boot so remote testers understand the failure.

## Out of Scope / Future Work

- No automatic retries or runtime reconnection.
- No refactor of filtering code; the guard simply prevents it from running when unsafe.
- We continue to rely on a manual reboot after the user fixes wiring.
