# Profile selection at boot and idle-screen wake

**Menu → Grind Settings → Automation → Auto select** offers Off, SINGLE, DOUBLE and CUSTOM. Off is the default for existing and new installations and retains the previous last-selected-profile behavior. With a profile configured, startup selects it, and a normal dim-to-awake transition selects it again and opens READY. Touch and weight activity both use the existing dimmer's wake detection.

The `auto_profile` preference is independent of the last selected tab and saved targets. SINGLE/DOUBLE/CUSTOM weight and time values, grind mode and calibration are retained. Manual profile selection still works between wake events. Changing the preference does not immediately navigate away from the menu. Invalid saved choices fall back to Off, and selecting an already-selected default avoids another NVS write.

Wake selection is skipped during an active grind/prime/tare, calibration, editing, purge confirmation, auto-tune, OTA or an active physical motor. A skipped wake is not queued and cannot unexpectedly change the profile when an operation later finishes. Idle menu/completion/timeout screens can return to READY after selection. The wake gesture, including its release, is consumed so it cannot press Grind on the newly shown screen; a second tap is needed. The same protection applies to a touch held across startup when Auto select is enabled.

Selection itself never issues a motor command. The separate existing Automation → Start feature retains its saved setting and behavior; selecting a default does not enable it or change it. This PR has no Decaid/BLE dose service, OLED pixel shifting or Atom-specific pin changes.

## Checks

Run `python3 tests/test_auto_profile.py`, `python3 tests/test_wake_touch.py` and `python3 tests/test_ui_idle_policy.py`. The tests compile actual preference, timeout, touch and UI selection code with clang++ and AddressSanitizer/UndefinedBehaviorSanitizer. They cover boot and invalid/Off preferences, saved targets/mode/calibration, bounded writes, touch/weight wake, whole-gesture suppression and all UI/grind phases with motor/OTA guards.

PlatformIO firmware compilation was checked with the default upstream environment and dependencies fixed to the versions already noted in platformio.ini: LVGL 9.3.0, Arduino GFX 1.6.1 and esp32-flashz 1.1.5. Hardware boot/wake interaction remains a separate physical check. No controller is flashed by this contribution.
