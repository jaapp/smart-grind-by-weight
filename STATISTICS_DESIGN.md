# Statistics System Design

## Statistics to Track

**Usage Statistics**
1. `stat_total_grinds` (uint32) - Total completed grinds
2. `stat_single_shots` (uint32) - Grinds ≤10g
3. `stat_double_shots` (uint32) - Grinds >10g and ≤22g
4. `stat_custom_shots` (uint32) - Grinds >22g
5. `stat_motor_runtime_sec` (uint32) - Total motor time (grind sessions + motor tests + time mode pulses)
6. `stat_device_uptime_hrs` (uint32) - Device runtime across power cycles
7. `stat_total_weight_kg` (float) - Total coffee mass ground

**Mode-Specific**
8. `stat_weight_mode_grinds` (uint32) - Weight mode count
9. `stat_time_mode_grinds` (uint32) - Time mode count
10. `stat_time_pulses` (uint32) - Additional pulses in time mode

**Quality/Performance**
11. `stat_avg_accuracy_g` (float) - Running average of abs(error_grams) in weight mode
12. `stat_total_pulses` (uint32) - Total precision pulse corrections
13. `stat_avg_pulses` (float) - Running average pulses per grind

## Storage
- **Backend:** Preferences (NVS)
- **Namespace:** `"stats"`
- **Persistence:** Survives reboots

## Update Triggers
- **Primary:** End of grind session in `GrindLogger::end_grind_session()`
- **Motor test:** Settings controller motor test
- **Time pulses:** `TIME_ADDITIONAL_PULSE` phase
- **Uptime:** Hourly periodic update

## Reset & UI
- **Factory Reset** (Settings → Logs & Data) clears lifetime statistics, calibration, profiles, and log files.
- **Purge Logs** (Settings → Logs & Data) removes time-series grind logs only; lifetime statistics persist.
- **Lifetime Stats Screen** shows read-only totals stored in NVS so users know these survive log purges but not factory reset.
