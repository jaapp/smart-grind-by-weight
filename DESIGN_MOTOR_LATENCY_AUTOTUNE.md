# Motor Response Latency Auto-Tuning Feature Design

## Overview

This document describes the design and implementation of the motor response latency auto-tuning feature for the ESP32-S3 intelligent coffee scale. This feature automatically calibrates the minimum pulse duration required for the grinder motor to reliably produce grounds, accounting for hardware variations across different grinder models, voltages (110V vs 220V), relay types, and burr inertia.

## Problem Statement

### Current Limitations

The existing pulse control system uses fixed compile-time constants for motor pulse duration constraints:

```cpp
#define GRIND_MOTOR_MIN_PULSE_DURATION_MS 75.0f   // Minimum motor pulse duration
#define GRIND_MOTOR_MAX_PULSE_DURATION_MS 300.0f  // Maximum motor pulse duration
```

These values do not account for:
- **Voltage differences**: 110V vs 220V motors have different response characteristics
- **Relay switching time**: Solid-state vs mechanical relays vary significantly
- **Burr inertia**: Different grinder models have varying mechanical resistance

### Why This Matters

The minimum pulse duration (`GRIND_MOTOR_MIN_PULSE_DURATION_MS`) represents the physical system lag — the time required for:
1. Relay to close circuit
2. Motor to overcome starting inertia
3. Burrs to begin rotating
4. First grounds to exit the chute

If this value is too high, the pulse correction algorithm wastes coffee on pulses longer than necessary. If too low, pulses may fail to produce grounds, requiring additional correction cycles.

## Design Goals

1. **Minimize coffee waste**: Use binary search to find minimum waste
2. **Hardware agnostic**: Support any grinder model without code changes
3. **Robust**: Handle chaotic grinding behavior with statistical verification
4. **User-friendly**: Simple one-button operation with clear UI feedback
5. **Safe**: Built-in safeguards prevent runaway calibration

## Architecture Changes

### Parameter Restructuring

**Before** (Compile-time fixed):
```cpp
#define GRIND_MOTOR_MIN_PULSE_DURATION_MS 75.0f
#define GRIND_MOTOR_MAX_PULSE_DURATION_MS 300.0f
```

**After** (Runtime configurable):
```cpp
// Default and bounds (compile-time)
#define GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS 75.0f  // Safe default
#define GRIND_AUTOTUNE_LATENCY_MIN_MS 30.0f            // Lower search bound
#define GRIND_AUTOTUNE_LATENCY_MAX_MS 200.0f           // Upper search bound
#define GRIND_MOTOR_MAX_PULSE_DURATION_MS 225.0f       // Maximum pulse duration above latency

// Runtime values (loaded from Preferences)
float motor_response_latency_ms;  // Stored in NVS: "motor_lat_ms"
float min_pulse_duration_ms;      // = motor_response_latency_ms
float max_pulse_duration_ms;      // = motor_response_latency_ms + GRIND_MOTOR_MAX_PULSE_DURATION_MS
```

### Storage

- **Preference key**: `motor_lat_ms`
- **Type**: Float (milliseconds)
- **Default**: 75.0ms
- **Valid range**: [30.0, 200.0]
- **Persistence**: Saved to NVS after successful auto-tune

## Binary Sweep Algorithm

### Algorithm Parameters

```cpp
#define GRIND_AUTOTUNE_PRIMING_PULSE_MS 500            // Initial chute priming pulse
#define GRIND_AUTOTUNE_TARGET_ACCURACY_MS 10.0f        // Target resolution (10ms steps)
#define GRIND_AUTOTUNE_SUCCESS_RATE 0.80f              // 80% success threshold (4/5 pulses)
#define GRIND_AUTOTUNE_VERIFICATION_PULSES 5           // Verification attempts per candidate
#define GRIND_AUTOTUNE_MAX_ITERATIONS 50               // Hard stop safety limit
#define GRIND_AUTOTUNE_SETTLING_TIMEOUT_MS 5000        // Max wait per pulse for scale settling
#define GRIND_AUTOTUNE_WEIGHT_THRESHOLD_G GRIND_SCALE_SETTLING_TOLERANCE_G  // 0.010g detection
```

### Phase 0: Chute Priming

**Purpose**: Ensure beans are positioned at grinder exit before measurement begins.

```
1. Display: "Priming chute..."
2. Execute 500ms pulse
3. Wait for motor settling + scale settling
4. Tare scale to zero weight
5. Proceed to Phase 1
```

**Note**: This pulse is **not** part of the calibration measurement.

### Phase 1: Binary Search

**Goal**: Find the boundary where pulses stop producing grounds with 10ms accuracy.

**State Variables**:
```cpp
float current_pulse_ms = GRIND_AUTOTUNE_LATENCY_MAX_MS;  // Start at 200ms
float step_size = LATENCY_MAX - LATENCY_MIN;             // Initial: 200 - 30 = 170ms
float last_success_ms = 0.0f;                            // Last working pulse
enum { UP, DOWN } direction = DOWN;                      // Search direction
bool found_lower_bound = false;                          // Found no-grounds threshold?
int iteration = 0;
```

**Algorithm**:
```
WHILE (iteration < MAX_ITERATIONS):

    // Execute test pulse
    grinder.start_pulse(current_pulse_ms)
    wait_for_settling(timeout = SETTLING_TIMEOUT_MS)

    // Measure weight change
    weight_delta = settled_weight_new - settled_weight_old

    // Evaluate result
    IF (weight_delta > WEIGHT_THRESHOLD_G):
        // ✓ Grounds produced - pulse is above minimum
        last_success_ms = current_pulse_ms

        // Check termination condition
        IF (found_lower_bound AND step_size <= TARGET_ACCURACY_MS):
            BREAK to Phase 2  // Found boundary with sufficient accuracy

        // Check for direction reversal
        IF (direction == UP):
            step_size = step_size / 2.0f      // Halve step on reversal
            direction = DOWN

        // Move DOWN to find lower boundary
        current_pulse_ms = current_pulse_ms - step_size

    ELSE:
        // ✗ No grounds produced - pulse too short
        found_lower_bound = true

        // Check termination condition
        IF (step_size <= TARGET_ACCURACY_MS):
            BREAK to Phase 2  // Accuracy target reached

        // Check for direction reversal
        IF (direction == DOWN):
            step_size = step_size / 2.0f      // Halve step on reversal
            direction = UP

        // Move UP to find working pulse
        current_pulse_ms = current_pulse_ms + step_size

    // Bounds checking
    current_pulse_ms = CLAMP(current_pulse_ms, LATENCY_MIN, LATENCY_MAX)

    IF (current_pulse_ms <= LATENCY_MIN AND found_lower_bound):
        BREAK to Phase 2  // Hit lower bound

    iteration++

END WHILE

// Safety check
IF (last_success_ms == 0.0f):
    ABORT: "No successful pulse found - check grinder connection"

// Round up to nearest 10ms
candidate_ms = CEIL(last_success_ms / 10.0f) * (GRIND_AUTOTUNE_TARGET_ACCURACY_MS / 2)
```

**Result**: `last_success_ms = 32.5ms` → `candidate = 35ms`

### Phase 2: Statistical Verification

**Goal**: Confirm candidate value produces grounds reliably (≥80% success rate).

**Rationale**: The grinding system is chaotic — bean size, hopper pressure, and burr alignment vary between pulses. A single successful pulse doesn't guarantee reliability.

```
candidate_ms = rounded_candidate from Phase 1
verification_round = 0

WHILE (verification_round < 5):  // Max 5 rounds

    success_count = 0

    FOR i = 0 TO 4:  // 5 test pulses
        grinder.start_pulse(candidate_ms)
        wait_for_settling()
        weight_delta = settled_weight_new - settled_weight_old

        IF (weight_delta > WEIGHT_THRESHOLD_G):
            success_count++

    success_rate = success_count / 5

    IF (success_rate >= 0.80):  // 4/5 or 5/5 successes
        // SUCCESS - Save to NVS
        preferences.putFloat("grind.motor_latency_ms", candidate_ms)
        RETURN SUCCESS

    ELSE:
        // Insufficient reliability - increase by 10ms
        candidate_ms = candidate_ms + TARGET_ACCURACY_MS
        verification_round++

END WHILE

// Verification failed after 5 rounds
RETURN FAILURE: "Auto-tune failed - using default 75ms"
```

### Safeguards

1. **Iteration limit**: Hard stop at 50 iterations in Phase 1
2. **Settling timeout**: Abandon pulse if scale doesn't settle within 5 seconds
3. **Bounds enforcement**: Never test pulses outside [30ms, 200ms] range
4. **Verification limit**: Max 5 verification rounds (prevents infinite climbing)
5. **Success validation**: Require ≥80% success rate (accounts for chaotic behavior)
6. **Fallback**: On any failure, revert to default 75ms and display warning

## User Interface

### Settings Integration

**Location**: Settings → Tools → "Auto-Tune Motor Response"

**Entry Requirements**:
- Cup must be on scale
- Scale must be tared
- Hopper must contain beans
- No grind operation in progress

**UI Flow**:
```
[Tools Screen]
├─ Calibrate Scale
├─ Tare Scale
├─ Reset Settings
└─ [NEW] Auto-Tune Motor Response  <-- New button
```

### Auto-Tune Screen

**Layout**:
```
╔═══════════════════════════════════╗
║   Motor Response Auto-Tune        ║
╠═══════════════════════════════════╣
║                                   ║
║  Phase: Priming Chute             ║  <-- Current phase
║  Step Size: 60.0 ms               ║  <-- Current step size
║                                   ║
║  Iteration: 7 / 50                ║  <-- Progress counter
║  Pulse: 45.0 ms                   ║  <-- Current test pulse
║  Result: ✓ Grounds Detected       ║  <-- Last result
║                                   ║
║  Verification: 4 / 5 Success      ║  <-- Phase 2 progress
║                                   ║
║  ┌─────────────────────────────┐  ║
║  │█████████░░░░░░░░░░░░░░░░░░░│  ║  <-- Overall progress bar
║  └─────────────────────────────┘  ║
║                                   ║
║  [────── CANCEL ──────]           ║  <-- Abort button
║                                   ║
╚═══════════════════════════════════╝
```

**Phase Labels**:
- "Priming Chute"
- "Binary Search" (with iteration count)
- "Verifying Result"
- "Complete!" or "Failed"

**Success Screen**:
```
╔═══════════════════════════════════╗
║      Auto-Tune Complete!          ║
╠═══════════════════════════════════╣
║                                   ║
║   New Motor Latency:              ║
║                                   ║
║        40 ms                      ║  <-- Large, centered
║                                   ║
║   Previous Value: 75 ms           ║
║                                   ║
║                                   ║
║                                   ║
║  [────────  OK  ────────]         ║
║                                   ║
╚═══════════════════════════════════╝
```

**Failure Screen**:
```
╔═══════════════════════════════════╗
║      Auto-Tune Failed             ║
╠═══════════════════════════════════╣
║                                   ║
║   Could not find reliable         ║
║   minimum pulse duration.         ║
║                                   ║
║   Using default: 75 ms            ║
║                                   ║
║   Check:                          ║
║   • Grinder power connection      ║
║   • Beans in hopper               ║
║   • Cup on scale                  ║
║                                   ║
║  [────────  OK  ────────]         ║
║                                   ║
╚═══════════════════════════════════╝
```

### Diagnostics Display

**Location**: Settings → Diagnostics

**Add new row**:
```
╔═══════════════════════════════════╗
║        Diagnostics                ║
╠═══════════════════════════════════╣
║                                   ║
║  Scale Calibrated:    Yes         ║
║  Load Cell Noise:     OK          ║
║  Mechanical Inst.:    No          ║
║  [NEW] Motor Latency: 40 ms       ║  <-- Add this line
║                                   ║
╚═══════════════════════════════════╝
```

**Display Logic**:
- Show current value from Preferences: `motor_lat_ms`
- If not set, show default: "75 ms (default)"
- Update immediately after successful auto-tune

## Implementation Requirements

### Existing Methods to Reuse

The following existing methods should be used to avoid code duplication:

#### WeightSensor Methods
- `bool check_settling_complete(uint32_t window_ms, float* settled_weight_out = nullptr)` - Returns true when settled and outputs final weight
- `void tare()` - Blocking tare operation (used in blocking overlay)
- `bool is_settled(uint32_t window_ms)` - Check if scale is settled without waiting
- `float get_display_weight()` - Get current weight for display

#### Grinder Methods
- `void start_pulse_rmt(uint32_t duration_ms)` - Start precise RMT pulse
- `bool is_pulse_complete()` - Check if RMT pulse has finished

#### UI Operations
- `UIOperations::execute_custom_operation()` - Execute blocking operation with custom message
- `BlockingOperationOverlay` - Unified blocking overlay system

### Code Changes

1. **Configuration** (`src/config/grind_control.h`):
   - Remove fixed `GRIND_MOTOR_MIN_PULSE_DURATION_MS` define
   - Add runtime parameter constants
   - Add auto-tune algorithm constants

2. **GrindController** (`src/controllers/grind_controller.h/cpp`):
   - Add `float motor_response_latency_ms` member (load from Preferences in init)
   - Add `load_motor_latency()` and `save_motor_latency()` methods
   - Add `get_min_pulse_duration()` and `get_max_pulse_duration()` runtime helpers
   - Add `get_motor_response_latency()` and `set_motor_response_latency()` accessors
   - Update `calculate_pulse_duration_ms()` in WeightGrindStrategy to use runtime values

3. **New AutoTuneController** (`src/controllers/autotune_controller.h/cpp`):
   - Implement binary search algorithm (Phase 1)
   - Implement verification algorithm (Phase 2)
   - Use existing `check_settling_complete()` for settling detection
   - Use existing `start_pulse_rmt()` and `is_pulse_complete()` for pulse execution
   - Use existing `tare()` for priming phase tare
   - Interface with GrindController for latency read/write

4. **New AutoTuneUIController** (`src/ui/controllers/autotune_controller.h/cpp`):
   - Handle UI state transitions for auto-tune screen
   - Update progress display (phase, iteration, pulse duration, etc.)
   - Handle user cancel action
   - Show success/failure completion screens

5. **New AutoTuneScreen** (`src/ui/screens/autotune_screen.h/cpp`):
   - Progress display screen (phase, step size, iteration, result indicators)
   - Success screen (new latency value, previous value)
   - Failure screen (error message, troubleshooting hints)

6. **UIManager** (`src/ui/ui_manager.h/cpp`):
   - Add `AutoTuneScreen autotune_screen` member
   - Add `AutoTuneUIController autotune_controller` member
   - Add auto-tune button to Tools screen
   - Add motor latency display to Diagnostics screen (read from Preferences)

7. **StateMachine** (`src/system/state_machine.h/cpp`):
   - Add `SystemState::AUTOTUNING` state
   - Add state transitions: READY → AUTOTUNING → READY
   - Prevent grind operations while in AUTOTUNING state

### Documentation Requirements

The following documentation must be updated after implementation:

1. **DOC.md**
2. **TROUBLESHOOTING.md**
4. **README.md**