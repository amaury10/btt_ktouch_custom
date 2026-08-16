*This page is also available in [French](CHANGELOG.md).*

# Release notes

## v0.9.0 — first publishable release

First complete firmware for the **BIGTREETECH K-Touch 5-inch**, an ESP32-S3
touchscreen whose manufacturer stopped development at the end of 2024. It
replaces the original interface with a **Moonraker client** and installs with no
cable, without opening the device, without overwriting the BigTreeTech firmware.

289 commits since 26 July 2026.

### Why 0.9 and not 1.0

The firmware prints for real, every day, on two machines. But two known defects
are unresolved (see "Known limitations") and one of them is a silent crash whose
cause is not yet established. Announcing 1.0 would promise a reliability that has
not been demonstrated.

### What the screen does

**Home.** Temperatures of every heater, history graph with a numbered vertical
scale (0-300 °C), position, active tool, progress and thumbnail of the gcode
being printed. Tapping a value edits its target, tapping a name shows or hides
its curve.

**Printing.** Files known to Moonraker, macros, pause/resume/cancel, emergency
stop, gcode console, fine tuning while printing (speed, flow, Z offset).

**Machine settings.** Movement and homing, temperatures with checkable targets
and PLA/PETG/ABS/TPU presets, extruder, fans, Z calibration, bed levelling,
limits, retraction, **bed mesh** (heatmap, calibration, saved profiles) and
**input shaper**.

**Peripherals.** USB stick with folder-by-folder browsing and upload to
Moonraker, power devices, **Spoolman** (spools and loaded spool).

**Operation.** A fleet of several printers with sequential switching, WiFi
settings on the screen, **over-the-air firmware update** with A/B slot and
rollback, backup and restore of the original firmware.

Capacities: 8 extruders, 48 macros, 32 Moonraker files, 64 entries per USB
folder, 6 printers in the fleet.

### Reversible installation without a serial port

The device can only be reached over WiFi: no full backup of the 16 MB is
possible, and `esptool` is out of the picture. The firmware therefore carries its
own way back, in three independent mechanisms — automatic rescue armed before
anything else, a boot counter in RTC memory, and a `/revert` request on demand.
The BigTreeTech firmware is **never overwritten**: it stays in its OTA slot and
takes back control on request.

The first two mechanisms brought the device back to stock on their own, twice,
during trials where WiFi would not associate.

Full procedure: [`docs/hardware/flashing.en.md`](docs/hardware/flashing.en.md).

### Verified on hardware

- Real printing driven from the screen on **Creality CR-10 S5** and
  **Snapmaker U1** (multi-head), through Moonraker over WiFi.
- 800×480 RGB panel, GT911 touch (I²C `0x5D`), octal PSRAM at 80 MHz.
- **Tear-free timings on an animated interface**: 14.8 MHz with wide porches.
  That is the contribution of [`docs/hardware/pinout.en.md`](docs/hardware/pinout.en.md),
  the first public verification of the K-Touch 5-inch pinout — the only one
  available until now was that of the Panda Touch 7-inch.

### Verifiable without hardware

- **87 test suites, 4352 checks** in a few seconds, with neither ESP-IDF nor a
  device ([`host-test/`](host-test/)), including the replay of recorded real
  Moonraker sessions.
- **LVGL/SDL simulator** on PC, in a window or as off-screen PNG capture
  ([`simulateur/`](simulateur/)).

### Known limitations

- **Silent crashes (WDT)**: five occurrences observed, cause not established.
  Both cores freeze with interrupts masked, which prevents a coredump from being
  written. A "black box" in RTC memory is in place to identify the area active at
  the moment of the freeze; it has not spoken yet.
- **The touch keyboard is sluggish** and drops characters when typing fast.
- The range of the temperature graph is fixed at compile time (0-300 °C).
- The bed mesh is truncated beyond 25×25 points, with an explicit notice on the
  screen.
- The Spoolman panel views and selects, but neither creates nor edits a spool:
  that inventory is entered by keyboard, in the Spoolman web interface.
- A USB folder with more than 64 entries has never been exercised on hardware.

### License — read before redistributing

The code in this repository is under MIT. **The hardware support is not.**

The [`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF)
component, which all the panel, touch and USB driving comes from, **has no
LICENSE file**: its "MIT" badge points to a file that does not exist, and its
README itself says "provided under the MIT License (*assumed*)". Code published
without a license remains under full copyright ("all rights reserved").

This repository currently contains 1,826 lines of it. A report is open with
BIGTREETECH ([`PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1)).
The full assessment and the options are in
[`docs/licence-du-composant-btt.en.md`](docs/licence-du-composant-btt.en.md).

### Disclaimer

Reprogramming the device is at your own risk. The approach is designed to be
reversible, but **without serial access no full backup is possible**: if all
three fallback mechanisms failed, only the serial route would allow taking back
control.
