# C++ to Python Struct Alignment Guide

This document provides essential guidance for maintaining data structure alignment between the ESP32 C++ firmware and Python BLE data processing tools. **Following this guide prevents parsing failures and data corruption during binary data exchange.**

## Critical Warning: Data Structure Changes Require Purge

⚠️ **MANDATORY**: Every time you modify any struct in `grind_logging.h`, you MUST purge all flash data using the "PURGE HISTORY" button on the ESP32 device before testing. Old corrupted data will cause alignment issues and false debugging results.

**Session ID Reset**: The purge function automatically resets all cached session tracking variables, ensuring session IDs restart from 1 after a purge.

## Struct Alignment Process

### Step 1: ESP32 Struct Layout Debugging

The ESP32 firmware includes debug functions to output actual memory layout:

```cpp
// In grind_logging.h
void print_struct_layout_debug();      // Shows sizes and field offsets for alignment debugging
```

**When debugging alignment issues:**
1. Flash updated firmware to ESP32
2. **PURGE HISTORY** to clear all old data
3. Trigger BLE export - this automatically prints struct layout debug info
4. Use the output to verify/update Python parsing

### Step 2: Python Parsing Update

Update `tools/grinder-ble` parsing based on ESP32 debug output:

```python
# Key structs to maintain alignment for:
LOG_SCHEMA_VERSION = 2    # Update when schema changes
GRIND_SESSION_SIZE = 84  # Update based on sizeof(GrindSession)
GRIND_EVENT_SIZE = 44     # Update based on sizeof(GrindEvent) 
GRIND_MEASUREMENT_SIZE = 24  # Update based on sizeof(GrindMeasurement)

# Binary parsing format strings
GRIND_SESSION_FORMAT = '<IIffffff...'  # Update field by field
```

### Step 3: Verification

Essential debug output from ESP32 shows:
```
=== STRUCT LAYOUT DEBUG ===
TimeSeriesSessionHeader: 24 bytes
GrindSession: 108 bytes (current size)
GrindEvent: 44 bytes  
GrindMeasurement: 24 bytes
```

## Current Struct Definitions (Reference)

### TimeSeriesSessionHeader (24 bytes)
```cpp
struct TimeSeriesSessionHeader {
    uint32_t session_id;        // offset 0
    uint32_t session_timestamp; // offset 4
    uint32_t session_size;      // offset 8 (bytes of GrindSession + events + measurements)
    uint32_t checksum;          // offset 12
    uint16_t event_count;       // offset 16
    uint16_t measurement_count; // offset 18
    uint16_t schema_version;    // offset 20 (current LOG_SCHEMA_VERSION)
    uint16_t reserved;          // offset 22 (future flags)
};
```

### GrindSession (84 bytes)
```cpp
struct GrindSession {
    uint32_t session_id;              // 0
    uint32_t session_timestamp;       // 4
    uint32_t target_time_ms;          // 8 (time-mode target)
    uint32_t total_time_ms;           // 12 (session runtime)
    uint32_t total_motor_on_time_ms;  // 16
    int32_t  time_error_ms;           // 20 (motor_on_time - target_time)

    float target_weight;              // 24
    float tolerance;                  // 28
    float final_weight;               // 32
    float error_grams;                // 36 (zeroed for time mode)
    float start_weight;               // 40 (pre-tare snapshot)

    float initial_motor_stop_offset;  // 44
    float latency_to_coast_ratio;     // 48
    float flow_rate_threshold;        // 52

    uint8_t profile_id;               // 60
    uint8_t grind_mode;               // 61 (enum GrindMode)
    uint8_t max_pulse_attempts;       // 62
    uint8_t pulse_count;              // 63
    uint8_t termination_reason;       // 64 (enum GrindTerminationReason)
    uint8_t reserved[3];              // 65-67

    char    result_status[16];        // 68-83 (null-terminated)
};
```

### GrindEvent (44 bytes)
```cpp
struct GrindEvent {
    uint32_t timestamp_ms;            // 0
    uint32_t duration_ms;             // 4
    uint32_t grind_latency_ms;        // 8
    uint32_t settling_duration_ms;    // 12
    float    start_weight;            // 16
    float    end_weight;              // 20
    float    motor_stop_target_weight;// 24
    float    pulse_duration_ms;       // 28
    float    pulse_flow_rate;         // 32
    uint16_t event_sequence_id;       // 36
    uint16_t loop_count;              // 38
    uint8_t  phase_id;                // 40
    uint8_t  pulse_attempt_number;    // 41
    uint8_t  event_flags;             // 42 (bitmask metadata)
    uint8_t  reserved;                // 43
};
```

### GrindMeasurement (24 bytes)
```cpp
struct GrindMeasurement {
    uint32_t timestamp_ms;            // 0
    float    weight_grams;            // 4
    float    weight_delta;            // 8
    float    flow_rate_g_per_s;       // 12
    float    motor_stop_target_weight;// 16
    uint16_t sequence_id;             // 20 (monotonic counter)
    uint8_t  motor_is_on;             // 22
    uint8_t  phase_id;                // 23
};
```

## Common Alignment Issues

### Issue: Python Parsing Failures
**Symptoms**: 
- `struct.error: unpack_from requires a buffer of X bytes`
- Incorrect field values in parsed data

**Solution**:
1. Check ESP32 debug output for actual struct sizes
2. Update Python `*_SIZE` constants
3. Verify format strings match field order and types

### Issue: Field Offset Misalignment  
**Symptoms**:
- Fields contain garbage data
- String fields show binary data

**Solution**:
1. Use ESP32 `print_struct_layout_debug()` to see struct sizes and field offsets
2. Manually verify each field offset in Python parsing
3. Account for compiler padding between fields

### Issue: Old Corrupted Data
**Symptoms**: 
- Parsing works sometimes but fails other times
- Debug output shows mixed clean/corrupted data

**Solution**:
1. **Always purge flash data after struct changes**
2. Test only with fresh grind sessions
3. Verify purge worked by checking session count

## Development Workflow

1. **Modify structs** in `grind_logging.h`
2. **Build and flash** firmware: `pio run --target upload`
3. **PURGE HISTORY** via ESP32 developer screen  
4. **Perform test grind** to generate fresh data
5. **Trigger BLE export** - review struct debug output
6. **Update Python parsing** in `tools/grinder-ble`
7. **Test full export pipeline** to verify alignment

## Key Files

- **ESP32 Structs**: `src/logging/grind_logging.h`
- **ESP32 Debug Functions**: `src/logging/grind_logging.cpp`
- **Python Parser**: `tools/grinder-ble` 
- **SQL Schema**: Updated automatically by Python parser

## Debug Commands

```bash
# ESP32 serial monitor (shows struct debug during export)
pio device monitor --baud 115200

# Test BLE export and parsing  
cd tools && python3 grinder-ble --connect --auto
```

Remember: **Every struct change = Mandatory flash purge + Python parsing update**
