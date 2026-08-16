*This page is also available in [French](CHANGELOG.md).*

# Release notes

## v0.9.1 — a manageable printer fleet, and an interface that stops lying

A consolidation release: no new subsystem, but the printer fleet becomes
genuinely manageable and three display defects disappear. 8 commits since
`v0.9.0`.

### Managing the fleet from the screen

Until now, a printer added to the fleet stayed there forever: the only write
paths ever **increased** the entry count. Once the 6 slots were taken, "Add
printer" answered "Printer list is full" with no way whatsoever to free one.
The address could only be changed over HTTP, and the name not at all.

A **long press on a tile** now opens a menu: **Rename**, **Edit address**,
**Remove**. A short tap still switches printers.

Removing the **active** printer is refused while others exist — the screen asks
you to switch first. This is not a technical limitation: removing it would mean
rewriting the boot host **and** restarting the panel, a side effect a delete
gesture must not trigger. The one exception is the active printer being the last
one: removing it simply empties the list.

Editing the active printer's address also rewrites the boot host, which is the
source of truth — otherwise the change would only take effect after switching
away and back, and would appear to do nothing.

Two HTTP endpoints accompany the screen, modelled on `/parc-hote`:
`POST /parc-nom?i=N` and `POST /parc-supprimer?i=N`.

### A temperature graph that actually fills

On a printer whose state stopped changing, the home screen drew **no** curve at
all, permanently. Propagation to the screen is triggered only by a counter
changing, and four independent stores — temperature history, Spoolman, console,
power outlets — were not wired into it. As long as the Klipper state did not
move, the screen was never refreshed.

On a real printer, whose temperatures fluctuate, the defect stayed hidden. It
bit with the printer powered off and Moonraker still up. This is the third
occurrence of that same class of omission; the code comment now names it
explicitly.

### Three display defects

- **Macros** and the **configuration screen**: buttons ran past the right edge
  of the panel. Two screens out of twenty-five computed their width from the
  panel's 800 px instead of the 742 actually available to the right of the rail.
- **Numeric keypad**: the last row was off by 6 px. LVGL's stock map gives 4
  keys to the first three rows and 5 to the last, for the same total width — the
  drift between columns is structural. Halved; removing it entirely would make
  the keys touch.

### Documentation

The README, in French and English, now shows **all 33 screens of the firmware**.
These are not mock-ups: they are 800×480 RGB565 captures produced by the
simulator, which compiles the real screen code. A single command regenerates
them (`tools/captures-readme.sh`).

Along the way, the simulator was not registering the configuration screen: its
home captures showed a status bar **without** the gear button the device does
display. That is what made the printer-address screen impossible to find for
anyone discovering the project through the README.

### Verified on hardware

Removing a printer, refusing to remove the active one, editing an address and
renaming were exercised on the real panel on 16 August 2026, against a CR-10 S5.

Automated suites: 4405 host-side C assertions, 66 Python tests, ESP-IDF build
with no warnings.

### Known limitations (in addition to those of v0.9.0)

- A USB key already plugged in at power-on takes about 5 s to mount; on a hot
  re-plug, an intermittent incident can stretch that to ~15 s, during which the
  screen reads "Insert a USB key" while the key is in fact there. The BSP
  exposes no intermediate state that would let it tell the truth.
- The fleet HTTP endpoints are only compiled when coredump-to-flash is enabled —
  the surrounding `#if` sweeps them in for no evident reason.
- The numeric keypad keeps ~3 px of misalignment on its last row.

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
