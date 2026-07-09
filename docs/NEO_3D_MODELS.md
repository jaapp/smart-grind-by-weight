# Eureka Mignon Neo 3D Models — Portafilter-Mounted Base

This fork adds a complete, redesigned 3D model set for the **Eureka Mignon Neo**, replacing the original cup-holder-style scale base with a **portafilter-mounted base**: the portafilter docks directly onto the scale platform under the chute, so grounds are weighed straight into the basket — no cup, no transfer, no mess.

All files live in [`3d_files/Eureka Mignon Neo/`](../3d_files/Eureka%20Mignon%20Neo/).

[<img src="../3d_files/Eureka%20Mignon%20Neo/Renders/neo-full-assembly.png" alt="Full assembly — portafilter docked on the scale base" width="70%">](../3d_files/Eureka%20Mignon%20Neo/Renders/neo-full-assembly.png)

## What changed vs. the original base

The original design weighed a cup (or dosing cup) sitting on a cup-holder platform. This redesign mounts the **portafilter itself** on the load cell:

- **Direct-to-basket grinding** — the target weight lands in the basket you brew from
- **Portafilter cradle** (top + bottom halves) that locates the portafilter head repeatably over the load cell
- **Cantilevered scale platform** keeps the load path clean while clearing the grinder body
- **15° tilt base** angles the whole grinder back for better chute-to-basket alignment and visibility of the screen

[<img src="../3d_files/Eureka%20Mignon%20Neo/Renders/portafilter-mount-top.png" alt="Portafilter mount top view with load cell amplifier" width="48%">](../3d_files/Eureka%20Mignon%20Neo/Renders/portafilter-mount-top.png) [<img src="../3d_files/Eureka%20Mignon%20Neo/Renders/scale-base-1.png" alt="Scale base render" width="48%">](../3d_files/Eureka%20Mignon%20Neo/Renders/scale-base-1.png)

## Part list

### Scale Base (`Scale Base/`)
| Part | Purpose |
|---|---|
| `Scale Base.stl` | Main base body housing the load cell |
| `Scale Base Cantelever Mount.stl` | Cantilever arm carrying the platform off the load cell |
| `Load Cell Mount Top.stl` | Upper load-cell clamp (the HX711 amplifier board seats here) |
| `Portafilter Mount Bottom.stl` | Lower half of the portafilter cradle |
| `Portafilter Mount Top.stl` | Upper half of the portafilter cradle |

### Screen Mount (`Scren Mount/`)
| Part | Purpose |
|---|---|
| `Frame.stl` | Display frame for the Waveshare 1.64" AMOLED |
| `Screen Mount (Minimal).stl` | Minimal mounting bracket |
| `Bottom Panel + Connector.stl` | Bottom closure with connector pass-through |

### Base
| Part | Purpose |
|---|---|
| `15 Degree Tilt Base.stl` | Tilts the grinder 15° back for chute alignment |

## Source files

- `Eureka Mignon Neo.step` — full parametric assembly (STEP) for editing in any CAD package
- The original Mignon (cup-holder) design remains available: `3d_files/smart-grind-by-weight. Eureka Mignon.f3z` and the top-level STLs

## Electronics

Wiring and firmware are unchanged from the main project — see the [main README](../README.md) and [DEVELOPMENT.md](DEVELOPMENT.md). The load cell and HX711 amplifier mount inside the scale base; route the load-cell wires away from mains/motor wiring and strain-relieve the connector (a noisy or intermittent load cell degrades settling-based features — the Tune Pulses screen's live noise readout will tell you how clean your install is).
