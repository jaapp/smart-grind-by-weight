# Desktop simulator

The desktop simulator runs the Smart Grind-by-Weight LVGL interface in a native
Windows window. It is intended for fast UI and grind-flow development without
flashing or connecting a development board.

The simulated panel has the same 280 x 456 logical resolution as the Waveshare
display. It currently runs the production Ready screen, both production
Grinding screen layouts, and the production Play/Stop/Complete control. A
deterministic grinder/load-cell model supplies rising weight, changing flow,
motor run-on, settling, and completion data.

## Requirements

- Windows 10 or later
- Visual Studio 2022 with the Desktop development with C++ workload
- Git and internet access for the first build (CMake fetches LVGL 9.3.0)

No ESP32, display, load cell, grinder, PlatformIO, or SDL installation is
required.

## Run

From PowerShell at the repository root:

```powershell
.\sim\run.ps1
```

The first run downloads LVGL and builds the executable. Later runs reuse the
local build.

Use the on-screen circular button to start, stop, acknowledge, and restart the
grind flow. The desktop-only keyboard shortcuts are kept outside the simulated
panel UI:

- `V`: switch between the production arc and chart grinding layouts
- `T`: tare the simulated load cell

## Automated smoke test

```powershell
.\sim\build.ps1 -Test
```

The smoke scenario creates the production UI, starts a grind, verifies that the
screen transitions to Grinding, and confirms that simulated load-cell weight
advances.

The simulator does not execute built ESP32 `.bin` files; those contain Xtensa
machine code and cannot run in a native Windows process. The complete
compatibility gate is therefore:

1. Build the V1 firmware target.
2. Build the V2 firmware target.
3. Build and run this simulator smoke test against the shared production UI
   source.

## Scope and hardware boundary

The simulator is suitable for UI layout, interaction flows, deterministic grind
scenarios, and future web/BLE integration work. Physical hardware remains the
authority for AMOLED initialization, QSPI/DMA timing, real touch-controller
behaviour, ESP32 memory pressure, BLE/Wi-Fi coexistence, relay wiring, HX711
electrical noise, and final motor safety checks.
