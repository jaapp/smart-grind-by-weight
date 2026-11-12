# Eureka Atom Speciality 65 community conversion

**Owner-confirmed working conversion:** 220 V Eureka Atom Speciality 65, purchased in 2022, with a Waveshare ESP32-S3-Touch-AMOLED-1.64 V1.1, HX711 and T70 1 kg load cell. Reported by [Sabotage1](https://github.com/Sabotage1) on 12 November 2026. Used for grinding into Decent portafilters with a 73 mm cradle opening.

This report applies to this Atom 65 build, not every Atom, Atom W or factory GBW variant. The owner confirmed the conversion works; no measured dose-accuracy or long-term durability dataset is supplied. The files include the latest requested CAD refinements (panel v0.43); the working-build report is not a separate physical validation of every newly exported revision.

## Print these three parts

Print **one of each STL at 100% scale, in millimetres**:

| Qty | Part | STL | Editable solid |
|---:|---|---|---|
| 1 | Full front panel v0.43 | [front_panel_v043.stl](front_panel_v043.stl) | [STEP](front_panel_v043.step) |
| 1 | Fixed short-leg scale adapter v0.9 | [scale_adapter_v09.stl](scale_adapter_v09.stl) | [STEP](scale_adapter_v09.step) |
| 1 | Moving 73 mm portafilter cradle v0.8 | [portafilter_cradle_73mm_v08.stl](portafilter_cradle_73mm_v08.stl) | [STEP](portafilter_cradle_73mm_v08.step) |

[Download the complete package](ATOM65_PRINT_AND_ASSEMBLY_v0_43.zip). Each STL is one printable body; STEP files retain their individual design coordinates; the panel and scale use different design frames, not one aligned assembly. Do not print electronics or an assembly as a fused object.

PETG, 0.2 mm layers, 6 walls and 40–50% infill are starting settings, not a validated printer profile. Use removable supports where needed, especially at the panel mounts, adapter L-legs and cradle overhangs. Do not follow the Mignon's support-free instruction for these Atom parts.

### Optional two-piece panel

Instead of `front_panel_v043.stl`, print both [base_v043.stl](optional_two_piece_front_panel/base_v043.stl) and [collar_v043.stl](optional_two_piece_front_panel/collar_v043.stl). This is an alternative, not two extra parts for the one-piece panel. The collar has a 4 mm inner underlap and nominal 0.3 mm interface allowance; its retention needs a trial fit. Both base versions include the revised HX711 holder.

## Hardware and fasteners

| Qty | Item | Use / status |
|---:|---|---|
| 2 | **M3 × 18 socket-head screws** | Adapter to the grinder's existing metal bracket; this is the length the owner actually used. Replaces the earlier provisional M3 × 16 suggestion. |
| 4 | M3 × 10 socket-head screws | Two fixed-end and two moving-end T70 load-cell fasteners; no nuts or washers. |
| 4 | M2 × 5 screws, nominal | Waveshare module's existing metal bosses. The model retains 3 mm bearing plastic, giving nominal 2 mm engagement; confirm seating against the actual module. This length comes from the design, not a new owner measurement. |
| 2 | Original grinder front-panel screws | Reuse at the two upper screw tabs. Their replacement thread/length has not been measured. |
| 1 | T70 1 kg load cell | Nominal 70 × 22 × 15 mm; four M3 holes, end-row centres 58 mm apart and paired holes 7 mm apart. Use the actual cell's drawing. |
| 1 | Waveshare ESP32-S3-Touch-AMOLED-1.64 **V1.1** | Display/controller |
| 1 | HX711 module | Fits the open-top integral panel holder |
| 1 | 1000 µF capacitor, at least 10 V | Across the build's 5 V/G rail; holder sized for approximately Ø10.2 × 16.5 mm body, leads upward. |
| As needed | Wire, right-angle HX711 header, Dupont housings, insulation, slack cable restraints and foam | Verify actual connector size and keep the weighed assembly free of cable tension. |

**Fastener total: 12 screws — 10 new and 2 reused.** No load-cell nuts, washers, heat-set inserts or separate handle-saddle screws are needed. The optional collar adds no designed screw joint.

The adapter has a 13.2 mm solid plastic mounting stack. M3 × 18 therefore gives about 4.8 mm nominal engagement before any other stack; the owner used this length successfully on their bracket. Check the actual threaded depth rather than forcing a screw that bottoms out. Socket heads and the portafilter pass close together.

## Mechanical assembly

1. Remove power and fit the panel to the original mounting interfaces. Upper screw centres are 88 mm apart; lower narrow hook tips span 90 mm outside-to-outside. The screen, HX711 holder, capacitor cradle and latches are integral to the panel.
2. Fit the unplugged Waveshare module into its seat and use four M2 screws into the module bosses. Connect it from behind after seating.
3. Slide the HX711 into the holder from above. The centre rail is 15.5 mm wide, side rails 5 mm wide, clear gaps 4 mm and rail length 18 mm. The linking bar is at the bottom; no upper bar obstructs insertion.
4. Fit the capacitor in the **left side when looking at the inside/back** of the panel, leads upward. Its entry notch follows the holder. The local relief clears the fixed HX711 socket; keep the capacitor leads insulated.
5. Clamp the fixed adapter to the existing metal bracket using **two M3 × 18** screws through the narrow mounting slot. The two fixed L-legs project forward 12 mm and extend down 18 mm below the adapter plate. They belong to the fixed side and must never touch the moving cradle.
6. Support the moving parts while fastening the load cell. Use **two M3 × 10 upward through the fixed shelf** into one cell end. Attach the opposite end to the moving cradle with **two M3 × 10 downward through the moving pad**. Each seat leaves 6.5 mm plastic, giving nominal 3.5 mm engagement. The centre sensing span stays clear; do not bridge the fixed and moving ends.
7. The load path is: **portafilter → moving cradle → free end of load cell → fixed end → adapter → metal bracket**. The cradle includes its handle support; there is no separate adjustable saddle.
8. Check the 73 mm portafilter opening, screw-head access and free movement. Nominal moving/fixed clearance is 2 mm. Keep wires slack and confirm that the portafilter, funnel, cradle and handle touch no fixed part. Count all moving mass toward the 1 kg cell rating; tare does not remove physical load. Do not tamp or knock on the weighing cradle.
9. Tare and calibrate with a known mass, then check return to zero and dispensing alignment with the actual portafilter before grinding.

## Electronics and firmware notes for this build

The Atom power/control board is marked **MAME Rev. 1.2**. The original transformer is marked 230 V primary / 9 V AC secondary; the separate J1 header is labelled GND / RL / GRL / +5V. These labels are not permission to apply the transformer's AC output to the controller. Work on the grinder's mains side is outside this mechanical contribution.

The build's pin assignment differs from the original V1 display configuration:

| Net | Connection in this build |
|---|---|
| Display CS | GPIO46 for Waveshare V1.1 |
| Touch interrupt | GPIO18; keep reserved for touch |
| HX711 DT / SCK | GPIO3 / GPIO2 |
| HX711 VCC / GND | 3V3 / common GND |
| Load cell | Red → E+, black → E−, green → A+, white → A−; shield → GND, per this T70 certificate |
| Atom +5V / GND | Controller 5V / common GND |
| Motor-control output | GPIO1, active high, to the build's verified RL interface; GRL unused |

The owner's local firmware is derived from upstream v1.4.0 with V1.1/GPIO1 support. **This contribution does not change upstream firmware or provide a universal ready-to-flash Atom binary.** Match the display revision and motor pin before using an upstream image. Do not use GPIO18 for motor control on V1.1. The original Atom display is replaced by the new front-panel controller in this conversion; it is not a required second display in the finished setup.

Decaid dose/profile syncing and burr-gap measurement are not part of this compatibility report.

## Files and validation

The [manifest](MANIFEST.json) records file hashes, quantities and screw counts. [CAD/mesh checks](validation.json) record one watertight positive-volume body per STL, retained component clearances and the dimensions inherited from the CAD revisions. They are separate from the owner's working-build confirmation. Print tolerances, other grinder revisions and numerical weighing performance are not established by mesh checks.

Adaptation and build report: Sabotage1. Based on jaapp's Smart Grind-by-Weight project; contributed under the repository's [GPL-3.0-or-later license](LICENSE.txt).
