# OLED pixel shifting

The display cycles through nine offsets on a 3 × 3 grid, at most ±2 physical pixels per axis, once per minute. Each shift invalidates the complete LVGL screen and paints exposed border strips black. Partial RGB565 transfers are clipped using the original source stride; touch coordinates are mapped back to logical screen coordinates and touches outside the shifted image are ignored.

Shifting is allowed on READY, menu, completion and timeout screens only when the grind phase is idle/completed/timed-out, the physical motor is stopped and OTA is inactive. No movement occurs while a touch is held or within one second of the last touch. Busy operation freezes the current offset, rather than recentering it. The next safe update can advance once after the interval has elapsed; missed intervals are not replayed.

This applies to the existing CO5300 AMOLED display path, independent of grinder model or board pin assignment. It does not alter screen brightness, the idle dimmer, profiles, motor control or firmware partitions. A two-pixel strip of content can be clipped at a screen edge. Pixel shifting distributes static content over nearby pixels; it is not a guarantee against burn-in.

## Host checks

Run `python3 tests/test_pixel_shift.py`, `python3 tests/test_pixel_shift_update.py` and `python3 tests/test_ui_idle_policy.py`. These compile actual display/UI function bodies with clang++ and AddressSanitizer/UndefinedBehaviorSanitizer. They cover full/partial transfers at all offsets, exposed edges, touch mapping, the update scheduler and every UI/grind phase with motor/OTA guards.

PlatformIO firmware compilation was checked with the default upstream environment and dependencies fixed to the versions already noted in platformio.ini: LVGL 9.3.0, Arduino GFX 1.6.1 and esp32-flashz 1.1.5. No device is flashed by these checks. Physical display appearance and long-term OLED wear are separate checks.
