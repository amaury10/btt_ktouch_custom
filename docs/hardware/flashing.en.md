*This page is also available in [French](flashing.md).*

# Flashing and recovering the K-Touch without a serial cable

## Context: what was lost, and why

The safety net originally planned for this project was simple: dump the 16 MiB
of flash before doing anything else, verify the backup
(`python ktouch-cli.py verify`, see `docs/hardware/partitions.en.md`), and be
able to restore it byte for byte should anything go wrong.

**That safety net does not exist on this setup.** The USB-C port of the K-Touch
used here only carries power — no serial port enumerates behind it — so
`esptool` is out of the picture and no flash backup is possible. In practice:

- **What remains recoverable**: the original BIGTREETECH firmware, intact in
  its OTA slot (`app0`, never rewritten by this project), and the official
  images published by BTT (`K-Touch_v1.1.0_partition.bin` and friends,
  available from their GitHub repository). The custom firmware in this
  repository can always be rebuilt and reinstalled.
- **What is NOT recoverable if lost**: the NVS (partition `nvs`,
  0x9000-0xDFFF). If it is erased or corrupted, the device settings and the
  WiFi credentials entered on screen by the end user are gone — this project
  holds no copy of them and cannot reconstruct them.

The replacement for that safety net is the firmware itself: see
`firmware/main/rescue.c`, `wifi.c`, `netlog.c` and `web.c`. This document
describes how to use it.

## Installing the firmware on the device

Installation goes through the OTA mechanism of the **original firmware**, which
writes to the inactive slot and switches the boot slot itself. This is the
safest route: no offset is handled by hand.

### 1. Check the starting state

```powershell
curl.exe -s http://<ip-de-la-k-touch>/update/identity
```

Expected: JSON along the lines of `{"id": "V1.0.0", "hardware": "ESP32"}`. Note
that version — it is what the device must be able to return to afterwards.

> If this request fails, **stop**. A device that is already unreachable before
> any write must not receive one.

### 2. Check that the binary is the right one

```powershell
python ktouch-cli.py image firmware/build/ktouch-custom.bin
```

Expected: `puce : ESP32-S3`, `projet : ktouch-custom`, and a size well below the
4,718,592 bytes of the slot.

> **Only install a firmware that is able to go back.** A firmware without a
> network stack — a bare test pattern, for instance — has no way of regaining
> control once booted, and without serial access that is a one-way trip.

### 3. Upload

Open `http://<ip-de-la-k-touch>/update` in a browser and select
`firmware/build/ktouch-custom.bin`. The page announces the end of the upload,
then the reboot.

> **A rejection is harmless.** If the original OTA refuses the image, nothing
> has been written and the device keeps running normally. That is not a
> failure, it is information — and the check must not be worked around.

### 4. Confirm

Wait a minute, then look at the screen. Since task 10 of sub-milestone 2b, the
panel shows the real Klipper screen on top of the milestone 1 diagnostic test
pattern, not the pattern itself:

- **Device never configured (no Moonraker host registered)**: the "Settings"
  screen (`ecran_configuration.c`) stacked on top of the home screen — fields
  "Printer address" and "Machine type", value "Not configured" as long as
  nothing has been entered, "Save" button. This is the normal state on a first
  boot, not a fault.
- **Device already configured**: the Klipper home screen straight away — the
  PRINT home (temperature tiles, progress, Pause/Cancel/E-STOP buttons) if a
  print is running at boot, otherwise the HUB HOME (temperature tiles grouped
  per tool, menu grid, including Déplacer for jogging/homing) when the machine
  is idle — the latter is what the user will meet at boot in the vast majority
  of cases (see `accueil_choix.h` for the criterion, and `ecran_accueil_hub.c`
  for the screen itself). Without the configuration screen on top in either
  case.
- **Panel or touch failure** (`pt_display_init()` failing, or a silent GT911):
  the device remains diagnosable remotely (WiFi, `/log`, `/state`, `/revert`)
  even with nothing displayed — see the comment at the top of `app_main()`.

The status line of the milestone 1 test pattern (slot, boot counter, source of
the WiFi credentials, IP address) is no longer visible in the normal case: the
shell (44 px status bar) and the opaque background of each stacked screen cover
it entirely. It only resurfaces if stacking the initial screen has itself
failed (see the corresponding `JOURNAL_ERREUR` entries in `app_main.c`) — a
degraded but readable fallback, not a defect to fix.

Then, remotely:

```powershell
curl.exe -s http://<ip-de-la-k-touch>/status
curl.exe -s http://<ip-de-la-k-touch>/state
curl.exe -s http://<ip-de-la-k-touch>/log
```

`/state` (see the route table further down) remains available exactly as
before, IN PARALLEL with the graphical interface: the screen is a visual
presentation of the very state exposed as JSON, not a separate channel — both
can be consulted independently, including when the panel is dead. `/revert` is
unaffected by the arrival of the interface: it still switches immediately to the
original firmware regardless of which screen is displayed when it is called.

> If nothing answers after two minutes, **do nothing**: the automatic rescue
> described below brings the device back to the original firmware on its own.
> Wait, then check it with `curl.exe -s http://<ip>/update/identity`.

## The three mechanisms, from most manual to most automatic

| Mechanism | Trigger | Depends on |
|---|---|---|
| Manual revert (`/revert`) | deliberate HTTP request | working WiFi |
| Automatic rescue (timer) | no WiFi connection when it expires | nothing — no panel, no touch, no network |
| Automatic rescue (boot counter) | more than `RESCUE_DEMARRAGES_MAX` consecutive reboots | nothing — survives even a panic or a watchdog |

**This firmware never writes to an application partition, neither `app0` nor
`app1`.** The only flash write in the whole firmware is that of `otadata`
(8 KiB), which designates the boot slot — in `rescue.c`, as a last resort only,
if the bootloader refuses the normal switch.

## Why there is no `/update` route on this firmware

It is counter-intuitive, and it is the point the initial design of this document
got wrong: **iterating on the pinout does not go through our firmware**, but
always through the original one.

This custom firmware runs from `app1` — that is the slot the original
firmware's OTA picks for it. With only two OTA slots, the "inactive slot" as
seen from `app1` is therefore `app0`, the one holding the original firmware. An
`/update` on our firmware would have nowhere else to write, and
`esp_ota_begin(OTA_SIZE_UNKNOWN)` erases the target partition **before**
receiving a single byte: the first update would therefore erase the original
firmware itself, after which the rescue would have nothing left to switch to.
That is why this firmware exposes no update route, and why `web.c` contains no
`esp_ota_begin`/`esp_ota_write`.

## HTTP routes exposed

The server listens on port 80, at the IP address logged at boot (`adresse IP :
...` in the logs, and reported in `/status`).

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | minimal status page, with links to the other routes |
| `/status` | GET | JSON: current slot, version, IP address, uptime since boot, free memory, touch available or not, boot counter |
| `/state` | GET | JSON: state of the link with the Klipper host, state generation, and the last known state — `extrudeurs` (up to 8, array of the present ones only, each with its original `index`), `nb_extrudeurs`, `outil_actif`, `plateau`, `ventilateurs`, XYZ position and homed axes, speed/flow in %, Z offset (babystep), `macros` (array of names) and `macros_tronquees`, file, progress, remaining time, print running/paused |
| `/log` | GET | plain text, contents of the in-RAM network log (last log lines) |
| `/revert` | GET | HTML page with a "Redémarrer" button (fires the POST) — handy from a browser |
| `/revert` | POST | switches to the other OTA slot and reboots |

The **switch** (`/revert`) is a **POST** on purpose: as a GET, any request from
a browser (URL prefetching, tab restore), from a link crawler or from a network
scanner would reboot the device. **GET `/revert`** therefore reboots nothing by
itself: it serves a small page with a "Redémarrer" button which, in turn, sends
the POST — which makes it possible to trigger the switch from a plain browser
(Firefox: open `http://<ip>/revert` then click) without a tool capable of POST,
while keeping the protection described above against accidental reboots.

The boot counter reported by `/status` is a snapshot taken once at boot (just
after `rescue_count_boot()`), not a value read live: it does not change between
two requests within the same boot, even if a WiFi connection succeeds in the
meantime and resets it behind the scenes for the next boot.

In the `/state` response, `"generation":0` means precisely "no reading has been
validated yet" — host not configured, loop not started yet, started but with no
successful cycle since, or host configured but unreachable (Moonraker down:
`boucle_demarrer()` succeeds as soon as the backend's `demarrer()` succeeds —
for Moonraker, that only creates an HTTP client, without contacting the machine,
see `backend_moonraker.c`). It is the only signal that distinguishes this state
from "the machine exists and all its fields genuinely read zero": as long as
`generation` is 0, `"etat":null` in the same response and nothing under that key
should be interpreted as a real reading of the machine. Once a first cycle has
succeeded, `generation` advances on every newly validated reading (see
`boucle_generation()` in `firmware/main/core/boucle.h`) and `etat` stops being
`null`.

**Honest zeroes, independently of `generation`.** The rich fields of `etat`
(v2, milestone 3a — `position`, `vitesse_pct`, `flux_pct`, `babystep_z_um`, the
macro list) are filled by the **WebSocket subscription** when it is online
(`printer.objects.subscribe` for the state, `printer.objects.list` for the
macros — see `moonraker_ws.c` and `backend_moonraker.c`); on the HTTP fallback,
only the fields the GET knows about are updated and the others stay at
`0`/`[]`/`false`. A field at zero can therefore mean "never received" rather
than "measured at zero": a client reading `"vitesse_pct":0` must not display it
as "speed stopped" without another way of telling the two apart (see the comment
on each field in `firmware/main/core/etat_klipper.h`, which documents what a
zero value means for it). For heaters, `/state` only emits an extruder in the
`extrudeurs` array if it is present (presence is signalled by **inclusion in the
array**, each entry carrying `index`/`actuelle`/`consigne`); only `plateau`
carries an explicit `presente` flag. The `macros` array is a list of names,
accompanied by the single boolean `macros_tronquees` (`/state` emits no
`nb_macros` field).

**`generation` is NOT a liveness signal for the loop.** It only advances when
the contents of the state actually change from one cycle to the next (memory
comparison in `etat_store_valider()`) — an idle printer, whose every Klipper
reading is identical to the previous one, leaves `generation` frozen
indefinitely even though the loop is successfully polling Moonraker once a
second. To know whether the loop is running, the field to read is `liaison`:
`"en ligne"` means the last cycle succeeded (whether its contents changed or
not), `"degradee"` or `"hors ligne"` signal consecutive failures, and
`"connexion"` means no cycle has completed yet since boot. `generation` answers
"does the display need to be redrawn?", `liaison` answers "is the link working
right now?" — two different questions, not to be confused.

## Reverting to the original firmware (WiFi works, the display is broken)

This is the case covered by `/revert`: the custom firmware booted, WiFi joined
the network (so the automatic rescue disarmed itself), but the panel or the
touch do not behave as expected. Since the original firmware is never
overwritten in its slot, reverting to stock requires no upload at all:

```bash
curl.exe -X POST http://<ip-de-la-k-touch>/revert
```

The device switches immediately to the other slot (`app0`, the original
firmware) and reboots onto it.

## Automatic rescue (WiFi does not answer at all, or the device is boot-looping)

Two independent mechanisms, which depend neither on the network, nor on the
panel, nor on the touch:

**The timer** covers failures slower than its deadline
(`CONFIG_KTOUCH_RESCUE_TIMEOUT_MS`, 90 seconds by default). Armed at the very
beginning of `app_main`, before the panel, before WiFi, it depends only on
itself. If WiFi is not connected when it expires — wrong SSID, incorrect
password, network out of range — the firmware switches the boot partition to the
other slot on its own and reboots.

The only place in the firmware that disarms this timer is the
`IP_EVENT_STA_GOT_IP` handler in `wifi.c`, that is, an actually successful WiFi
connection (IP address obtained). Nothing else disarms it.

**The boot counter** covers almost everything the timer does not see: a failure
faster than 90 seconds — panic, watchdog, stack overflow — **provided it happens
after entering `app_main`**. It lives in RTC memory (`RTC_NOINIT_ATTR`), so it
survives a software reboot as well as a panic, but not a power cut. Incremented
at the very beginning of `app_main`, even before the timer, it switches the
device to the other slot immediately as soon as it exceeds three consecutive
boots without an IP address ever having been obtained — after first resetting
the counter itself (otherwise, retrying to install that same firmware on top
would start out already past the threshold and switch without ever attempting to
boot). A successful WiFi connection resets it, at the same place as the timer.

In both cases, no intervention is needed: just wait for the device to recover on
its own.

**What neither the timer nor the counter covers: a PSRAM failure.**
`esp_psram_chip_init()` runs from `cpu_start.c`, before the scheduler and before
`app_main`: a failure there triggers an `abort()` while not a single line of the
recovery code has had the slightest chance to run — no counter, no timer, no
HTTP server. Since `otadata` still designates that same slot, a power cut
changes nothing: the device would start over indefinitely. This is the only
class of failure that would make the device permanently unreachable, and it is
not handled in the code (impossible: nothing has run yet) but upstream, in
`firmware/sdkconfig.defaults`. `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` is deliberately
enabled there so that this milestone is exactly the ground on which that
question must be asked: a PSRAM that is absent or incompatible with the 5-inch
K-Touch (the initial settings come from the 7-inch Panda Touch BSP) no longer
kills the boot — it merely makes the LVGL buffer allocation in the BSP fail, so
`pt_display_init()` returns an error, and we fall back into the case already
handled above: dead panel, WiFi and `/revert` very much alive. Code and
constants are no longer placed in PSRAM either
(`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`/`CONFIG_SPIRAM_RODATA` removed), so that
marginal timing turns into a clean allocation failure rather than a crash on an
instruction fetch. This is not free: these are margins that a proof of life has
no need to exploit.

## Iterating on the pinout (the main use case of this milestone)

Fixing a badly guessed pinout takes several attempts. The correct loop goes back
through the original firmware every time, and never touches `app0`:

1. `/revert` on the custom firmware (if it is still running and WiFi answers) —
   back to the original firmware in `app0`:
   ```bash
   curl.exe -X POST http://<ip-de-la-k-touch>/revert
   ```
   If WiFi was not answering at all, the automatic rescue (timer or boot
   counter, see above) has already performed that same revert on its own — this
   step is then unnecessary.
2. Fix the code, rebuild (`idf.py build`).
3. `/update` **on the original firmware**, not ours — it is the one that exposes
   that route, and it is the one running at this point since we have just gone
   back to it. It writes the new version to the inactive slot (`app1`) and boots
   onto it.
4. Try it, observe. If the new code breaks something before WiFi connects, the
   automatic rescue brings the device back to the original firmware in `app0` by
   itself, without ever having touched it.
5. Back to step 1.

Once the right IP address of the custom firmware has been found again (it can
change from one reboot to the next depending on the DHCP lease — `/status` on
the original firmware or the network logs make it possible to find it), consult
`/log` to read the boot logs and confirm what has changed.

## Getting a serial path back, should the need arise

The K-Touch exposes its UART through a bridge on the USB-C port. A USB-C cable
that only carries power (which is the case on this setup) enumerates no port:
that is why this entire document exists. To get serial access back, you need a
USB-C cable that actually carries the data lines (not only the power lines),
plugged into a host that can enumerate the resulting serial device. Once a
serial port is available, the usual tools (`esptool`, `idf.py monitor`, the
byte-for-byte backup and restore described in `docs/hardware/partitions.en.md`)
become usable again.
