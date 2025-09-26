# Grinder Measurement Definitions

This document provides comprehensive definitions for all measurements collected by the ESP32-S3 coffee grinder system. Each measurement includes its description, calculation method, and timestamp semantics.

**Source Reference**: Based on analysis of `src/controllers/grind_controller.cpp` and `src/logging/grind_logging.h`

---

## 1. GrindEvent Structure

**Purpose**: Discrete, low-frequency events summarising each major phase transition.  
**Frequency**: One event per phase entry.  
**Timestamp Semantics**: `timestamp_ms` marks the **start** of the phase relative to the session start.  
**Source**: `src/logging/grind_logging.h` / `src/controllers/grind_controller.cpp`

### Fields (LOG_SCHEMA_VERSION = 2)

#### `timestamp_ms` (uint32_t)
- Milliseconds since session start when this phase began.
- Captured as `millis() - session_start_time` at phase entry.

#### `duration_ms` (uint32_t)
- How long the phase remained active.
- `phase_end_time - phase_start_time`.

#### `grind_latency_ms` (uint32_t)
- Time from motor start until the controller detected flow.
- Populated for predictive phases to quantify burr spin-up latency.

#### `settling_duration_ms` (uint32_t)
- Mirrors `duration_ms` for settling-oriented phases to make postprocessing easier.
- Zero for phases where settling is not relevant.

#### `start_weight` / `end_weight` (float)
- Raw weight snapshot (grams) at entry/exit of the phase.
- Provides yield context per phase.

#### `motor_stop_target_weight` (float)
- Predicted motor cut-off weight (grams) calculated by the predictive algorithm.
- Snapshotted for every phase to trace adjustments over time.

#### `pulse_duration_ms` (float)
- Commanded pulse duration (milliseconds) during `PULSE_EXECUTE`.
- Zero for non-pulse phases.

#### `pulse_flow_rate` (float)
- Flow rate (g/s) carried forward from predictive phase for pulse calculations.
- Logged so downstream analytics can recompute expected yields.

#### `event_sequence_id` (uint16_t)
- Monotonic counter assigned by the logger (0-based).
- Guarantees deterministic ordering when exporting data.

#### `loop_count` (uint16_t)
- Number of control loop iterations executed while in this phase.
- Useful for verifying loop timing/performance.

#### `phase_id` (uint8_t)
- Encoded `GrindPhase` enum value at phase entry.
- See `src/controllers/grind_controller.h` for mapping.

#### `pulse_attempt_number` (uint8_t)
- 1-based pulse index (1..`USER_GRIND_MAX_PULSE_ATTEMPTS`).
- Zero for non-pulse phases.

#### `event_flags` (uint8_t)
- Bitmask metadata for quick filtering:  
  - `0x01` – Event recorded while running in grind-by-time mode.  
  - `0x02` – Phase kept the motor running (motor-active).  
  - `0x04` – Phase relates to the pulse subsystem (`PULSE_EXECUTE` / `PULSE_SETTLING`).
- Additional bits reserved for future analytics.

## 2. GrindMeasurement Structure

**Purpose**: High-frequency telemetry captured during grinding.  
**Frequency**: Logged every `SYS_LOG_EVERY_N_GRIND_LOOPS` iterations (default ≈100 ms).  
**Timestamp Semantics**: Actual time of the reading relative to session start.  
**Source**: `src/logging/grind_logging.h`

### Fields

#### `timestamp_ms` (uint32_t)
- Milliseconds since session start for this sample.
- Matches the controller loop timestamp.

#### `weight_grams` (float)
- Instantaneous cup weight (grams) read directly from the load cell fast-path.

#### `weight_delta` (float)
- Difference in grams compared to the previous logged measurement.
- Used to derive flow rate without recomputing on the host.

#### `flow_rate_g_per_s` (float)
- Instantaneous flow rate in grams/second calculated by the controller.
- Already filtered according to the active smoothing strategy.

#### `motor_stop_target_weight` (float)
- Current predictive motor stop target weight at the time of logging.
- Enables plotting how the target converges over time.

#### `sequence_id` (uint16_t)
- Monotonic measurement counter maintained by the logger (wraps at 65,535).
- Useful for gap detection when packets are dropped.

#### `motor_is_on` (uint8_t)
- Motor state snapshot (1 = running, 0 = stopped).
- Aligns with `GrindEvent` motor-active flags.

#### `phase_id` (uint8_t)
- Current controller phase when the sample was taken (`GrindPhase` enum).

## 3. GrindSession Structure

**Purpose**: Captures session-wide metadata, controller configuration, and results for both grind-by-weight and grind-by-time modes.  
**Frequency**: One record per session (stored alongside every event/measurement block).  
**Source**: `src/logging/grind_logging.h`

### Identity & Timing

- `session_id` (uint32_t) – Monotonic identifier persisted in preferences.  
- `session_timestamp` (uint32_t) – Start time in Unix seconds.  
- `target_time_ms` (uint32_t) – Requested grind duration for time-mode sessions (0 for weight mode).  
- `total_time_ms` (uint32_t) – Wall-clock duration from session start to completion.  
- `total_motor_on_time_ms` (uint32_t) – Sum of all motor active periods.  
- `time_error_ms` (int32_t) – `total_motor_on_time_ms - target_time_ms`; zeroed for weight mode.

### Weight Targets & Outcomes

- `target_weight` (float) – Desired cup weight (grams).  
- `tolerance` (float) – ± tolerance in grams captured at session start.  
- `start_weight` (float) – Pre-tare snapshot of cup weight (useful for scale drift analysis).  
- `final_weight` (float) – Settled weight recorded at session end.  
- `error_grams` (float) – `target_weight - final_weight`; explicitly set to `0.0f` for time-mode sessions to avoid misinterpretation.

### Mode, Status & Pulse Summary

- `profile_id` (uint8_t) – Active profile index (Single / Double / Custom).  
- `grind_mode` (uint8_t) – Encoded `GrindMode` enum (`0 = WEIGHT`, `1 = TIME`).  
- `max_pulse_attempts` (uint8_t) – Copy of `USER_GRIND_MAX_PULSE_ATTEMPTS`.  
- `pulse_count` (uint8_t) – Actual number of corrective pulses executed.  
- `termination_reason` (uint8_t) – Encoded `GrindTerminationReason` (COMPLETE / TIMEOUT / OVERSHOOT / MAX_PULSES / UNKNOWN).  
- `result_status` (char[16]) – Human-readable status string mirrored from the controller (e.g., `"COMPLETE"`, `"TIMEOUT"`, `"STOPPED_BY_USER"`).

### Predictive & Pulse Configuration Snapshots

- `initial_motor_stop_offset` (float) – Undershoot target used at session start.  
- `latency_to_coast_ratio` (float) – Ratio controlling coast compensation.  
- `flow_rate_threshold` (float) – Flow detection threshold (g/s).  
- `pulse_duration_large` / `medium` / `small` / `fine` (float) – Pulse timing lookup table (milliseconds).  
- `large_error_threshold` / `medium_error_threshold` / `small_error_threshold` (float) – Weight-error thresholds that classify undershoot severity.

## Key Calculation Details

### Dynamic Motor Stop Target Weight
```pseudocode
if (current_flow_rate >= USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS && !flow_start_confirmed):
    grind_latency_ms = current_time - phase_start_time
    flow_start_confirmed = true
    
if (flow_start_confirmed && current_flow_rate > USER_GRIND_FLOW_DETECTION_THRESHOLD_GPS):
    motor_stop_target_weight = ((grind_latency_ms * USER_LATENCY_TO_COAST_RATIO) / 1000.0) * current_flow_rate
```

### Flow Rate (FlowRateData rolling window)
```pseudocode
// Used by GrindController for dynamic motor stop calculation
time_span_ms = newest_sample_time - oldest_sample_time
weight_span = newest_weight - oldest_weight
flow_rate = (weight_span / time_span_ms) * 1000.0
```

### Flow Rate (GrindLogger simple delta)
```pseudocode  
// Used by GrindMeasurement logging - pre-calculated by GrindController
flow_rate = weight_sensor->get_flow_rate()  // Passed as parameter
```

### Motor Time Tracking
```pseudocode
on_motor_start():
    motor_start_time = current_time
    
on_motor_stop():
    if motor_start_time > 0:
        total_motor_time += (current_time - motor_start_time)
```

---

## Important Notes

1. **Timestamp Semantics Differ**: 
   - GrindEvent timestamps = **phase start time**
   - GrindMeasurement timestamps = **measurement time**

2. **Error Sign Convention**: 
   - Positive error = undershoot (need more coffee)
   - Negative error = overshoot (too much coffee)
   - For grind-by-time sessions `error_grams` is explicitely zeroed; rely on `time_error_ms` instead.

3. **Two Flow Rate Methods**:
   - GrindController uses rolling window over 10 samples
   - GrindLogger uses simple delta between measurements

4. **Phase Duration vs Total Time**:
   - GrindEvent duration_ms = individual phase duration
   - GrindSession total_time_ms = entire session duration

5. **Motor Time vs Session Time**:
   - total_motor_on_time_ms = actual grinding time
   - total_time_ms = session duration (includes settling, etc.)
