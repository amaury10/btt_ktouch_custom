*This page is also available in [French](README.md).*

# PC simulator (LVGL + SDL)

## What it is for

This directory runs the LVGL interface on a PC, without any K-Touch
hardware: either in an interactive SDL window, or offscreen to produce a PNG
screenshot. It is the core of milestone 2b (interface) — every later task
that builds screens relies on `afficheur_demarrer()`, `afficheur_pomper()`
and `afficheur_capturer()` (see `afficheur.h`) without knowing which of the
two outputs is active.

**Why the PNG screenshot exists.** The repository lives on a machine where
the real device is not always powered on, and a code review cannot plug in a
screen. Without a PNG file, a claim about how a screen looks is verifiable by
nobody — neither by the author of the change working remotely, nor by a
reviewer. The offscreen mode makes every screen provable by a file rather
than by a statement.

Both modes share the same pixel format as the real K-Touch panel (RGB565) and
the same resolution (800×480, `AFFICHEUR_LARGEUR` / `AFFICHEUR_HAUTEUR`): a
screenshot therefore shows the pixels the device would push to its panel, not
an approximation.

## Prerequisites (under WSL Debian)

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libsdl2-dev
```

Then, to fetch the LVGL submodule (see further down why it is a pinned
submodule):

```sh
git submodule update --init --recursive --depth 1
```

## Running both modes

From PowerShell (do not invoke `wsl.exe` from Git Bash: Git Bash mangles
`/mnt/...` paths):

```powershell
wsl -d Debian -- bash "<chemin-vers-le-depot>/simulateur/run.sh" --capture "<chemin-vers-le-depot>/simulateur/build/mire.png"
wsl -d Debian -- bash "<chemin-vers-le-depot>/simulateur/run.sh"
```

Or, from a WSL shell, at the root of the repository:

```sh
./simulateur/run.sh --capture simulateur/build/mire.png   # offscreen mode, writes a PNG then exits
./simulateur/run.sh                                       # SDL window mode, interactive
```

`run.sh` configures and builds into `simulateur/build/` (never `/tmp`: under
WSL, `/tmp` is wiped between two invocations launched from Windows), then
runs the executable with the arguments it received.

In screenshot mode, the expected output is:

```
capture ecrite : <chemin> (800x480)
```

with exit code 0. In window mode, an 800×480 SDL window opens and shows the
Klipper home screen (`ECRAN_ACCUEIL`, task 6) wired to the simulated loop;
the mouse pointer acts as the simulated touch input
(`lv_sdl_mouse_create()`), so the widgets react to clicks. (Before task 6,
both modes displayed a throwaway demo screen — no longer the case now that
`simulateur/main.c` pushes the real screen.)

### Command-line options

`run.sh` forwards every argument received after `--` to the executable; see
`main()` in `simulateur/main.c` for the exact list. Summary:

- `--capture <chemin>`: offscreen mode, writes a PNG to `<chemin>` then
  exits (exit code 0). Without this option: interactive SDL window mode.
- `--scenario <n>`: selects the fake backend's scenario (see
  `backend_factice_scenario()` in `firmware/main/core/backend_factice.h`, the
  only source of truth for this numbering — it has already changed once
  during this milestone). Current numbering:
  - `0` — idle (`etat = "standby"`, nothing heating, nothing printing) —
    **this is the default** when `--scenario` is not given;
  - `1` — print in progress, progress advances on every cycle;
  - `2` — **paused** (print in progress, `impression_en_pause = true`,
    progress frozen at 50 %) — NOT an idle state, whatever a variable name
    or a comment might suggest from a distance (a trap already hit once
    during the review of task 6: do not assume, re-read
    `backend_factice.c`);
  - `3` — extreme but **plausible** values (file name at the maximum of its
    capacity without a null byte, 350 °C) — used to check that a display
    does not overflow anywhere;
  - `4` — **out-of-range**, aberrant values (999 °C / -999 °C) — used to
    check that a display renders `"--"` rather than a wrong number
    (`ui_format_temperature()`, see `firmware/main/ui/widgets/tuile.h`).
  - `5`/`6` (task 7, screenshot mode only): they change nothing in the fake
    backend itself (passed through as-is to `backend_factice_scenario()`,
    which treats them as "any other number" — see just above) but
    additionally open, on top of the home screen already built, the modal
    keyboard (`5`) or the destructive confirmation dialog (`6`) — see
    `firmware/main/ui/widgets/clavier.h`/`confirmation.h`. Used solely to
    produce `clavier.png`/`confirmation.png` for review; no effect in window
    mode.
  - `7`/`8` (task 8): they start on `ECRAN_CONFIGURATION` (see
    `firmware/main/apps/klipper/ecrans/ecran_configuration.h`) instead of
    `ECRAN_ACCUEIL` — exactly what `app_main.c` would do on a device whose
    `reglages_configures()` returns false. `7` captures the screen alone
    ("idle"); `8` additionally opens the modal keyboard on top, pre-filled
    with an address as if it had just been typed — same technique as
    scenario 5, a capture-only. Like `5`/`6`, these two numbers correspond to
    no fake-backend scenario (falling back to the behaviour of scenario 3).
    Use `--cycles 0` with `7` for an "idle" screenshot without the "host
    connected" banner that a positive `--cycles` otherwise triggers (see
    below).
  - `9` (task 9, screenshot mode only): demonstrates the ASYNCHRONOUS failure
    of a command -- `ui_commander()` accepts it right away (`ESP_OK`), but
    its actual execution, later on by the simulated loop, fails deliberately
    (`backend_factice_commande_echoue(true)`, see
    `firmware/main/core/backend_factice.h`) even before the first `--cycles`
    cycle. Like `5`/`6`/`7`/`8`, it corresponds to no fake-backend scenario
    (falling back to the behaviour of scenario 3); unlike them, it also skips
    the "host connected" banner (see below) so that the failure banner
    ("Command failed: pause") stays visible on the screenshot instead of
    being replaced.
  - `10` (task 2, milestone 3a, "CR-10" tier): entry-level single-extruder
    printer, idle, heated bed, four simple macros (`BED_MESH_CALIBRATE`,
    `LOAD_FILAMENT`, `UNLOAD_FILAMENT`, `LIGHTS_TOGGLE`).
  - `11` (task 2, milestone 3a, "U1" tier): four-extruder tool changer (one
    hot among the four, the other three at room temperature), `outil_actif`
    switching head on every simulated cycle, as a real tool changer would;
    heated bed; eight macros including `_CACHEE` (hidden prefix — present in
    the state, it is up to the interface to filter it later, not up to the
    backend), `PURGE_PARAM` (which will demonstrate parameters, task 6) and
    `MACRO_ECHEC`, a sentinel whose command ALWAYS fails, whatever the active
    scenario.
  - `12` (task 2, milestone 3a, "8 heads" tier): eight synthetic extruders,
    all present, and 48 macros — exactly `KLIPPER_MACROS_MAX`, the real bound
    of `etat_klipper_t::macros` — with `macros_tronquees = true`: the only
    scenario in this file where that field is raised, to signal that a
    producer knows about more of them than the structure can carry (see the
    CRITICAL fixed during the review of task 1 about that exact OOB,
    `firmware/main/web_macros.h`).
  - `10`/`11`/`12` also respond to the `BACKEND_ACTION_MACRO` action
    (`commande()`, `arguments_json = {"nom":"<macro>"}`): known name →
    `ESP_OK`, `MACRO_ECHEC` → `ESP_FAIL`, unknown name or missing/unreadable
    `arguments_json` → `ESP_ERR_NOT_SUPPORTED`.
  - `13` (task 5, milestone 3b, screenshot mode only; background updated in
    task 7, removal of the old idle home screen): fake backend IDENTICAL to
    scenario 10 ("CR-10", idle, X/Y/Z axes all homed — see
    `backend_factice.c`), but ADDITIONALLY opens, on top of the already
    pushed `ECRAN_ACCUEIL_HUB`, the homing confirmation dialog
    (`confirmation_ouvrir_ex()`, inline literals since task 7 — neither
    `ECRAN_ACCUEIL_HUB` nor `ECRAN_DEPLACER` exposes a confirmed Home button
    any more, see `simulateur/main.c`) — same pattern as `5`/`6` on the print
    screen, but on the idle home screen: an axis MUST already be homed for
    the screenshot to show the confirmation rather than a direct send
    (spec §7). Used solely to produce `idle-home-confirm.png`.
  - `14` (task 6, milestone 3b, screenshot mode only; background updated in
    task 7, removal of the old idle home screen): fake backend IDENTICAL to
    scenario 10 ("CR-10", idle), but ADDITIONALLY opens, on top of the
    already pushed `ECRAN_ACCUEIL_HUB`, the numeric temperature keyboard
    (`clavier_ouvrir()`, inline literal since task 7 — `ECRAN_ACCUEIL_HUB` no
    longer has a clickable temperature cell, see `simulateur/main.c`) — same
    pattern as `13` for homing. Used solely to produce
    `idle-temp-clavier.png`.
  - any other number falls back to the behaviour of scenario 3 (see
    `backend_factice_rafraichir()`).
- `--cycles <n>`: before a screenshot, advances the simulated loop by `<n>`
  cycles (one cycle = one backend refresh + validation of the state store,
  what `boucle_tache()` would do once per second on target) before capturing.
  Without this option (or with `--cycles 0`), the screenshot is taken at time
  zero: everything is zero, the screen is greyed out (link still
  `LIAISON_CONNEXION`). No effect in window mode (which advances one cycle
  per elapsed second, continuously).
- `--ecran macros` (task 6, milestone 3a): pushes `ECRAN_MACROS` (see
  `firmware/main/apps/klipper/ecrans/ecran_macros.h`) on top of the home
  screen already built, instead of staying on the home screen alone. Any
  other value (or the absence of the option) falls back to the home screen
  alone, the same defensive policy as `--app`. Never combined with
  `--scenario 7`/`8` (configuration) in the planned screenshots — only one
  screen pushed on top of the home screen at a time.
- `--macro <nom>` (task 6, screenshot mode only): the counterpart, in the
  absence of simulated touch input, of a real tap on a button of the
  `ECRAN_MACROS` grid — it builds `{"nom":"<nom>"}` with the same pure
  function as that button and pushes `BACKEND_ACTION_MACRO` before the first
  `--cycles` cycle, with the same synchronous banner the real tap would put
  up ("Macro sent: `<nom>`" or "Command failed: `<nom>`"). Useful together
  with `--scenario 11` ("U1"): a known name (e.g. `LOAD_FILAMENT`)
  demonstrates success, `MACRO_ECHEC` (the sentinel that always fails)
  demonstrates the ASYNCHRONOUS failure reported through the existing generic
  seam ("Command failed: macro", without the name — see the comment on
  `bouton_macro_cb()` in `ecran_macros.c` for why that seam can only carry
  the action's name, never its arguments). Like `--scenario 9`, it skips the
  "host connected" banner (see below) so that the banner survives until the
  screenshot.
- `--echec`: replaces `backend_factice` with a toy backend local to
  `simulateur/main.c` that fails systematically (`ESP_FAIL` on every
  refresh). Used to drive `liaison_t` towards `DEGRADEE` (3 failures) then
  `HORS_LIGNE` (10 failures) as an unreachable host would on target, in order
  to capture the stale/greyed-out state of a screen (see §5.3 of the
  specification: the status bar alone displays that state, never an error box
  on the screen itself). In practice incompatible with `--scenario` (the
  failing backend ignores that choice).
- `--hote <adresse:port>` (task 7, milestone 3a): replaces `backend_factice`
  with `moonraker_pc.c` (`simulateur/moonraker_pc.c`), a real HTTP backend
  talking to a REAL Moonraker -- through a bare POSIX-socket GET (HTTP/1.0,
  no keep-alive), NOT `esp_http_client` (ESP-only) nor libcurl. It feeds
  `moonraker_parse_status()`/`rpc_lire_macros()`
  (`firmware/main/apps/klipper/moonraker_parse.c`/`moonraker_rpc.c`) -- the
  SAME pure functions as the ESP backend and as the `host-test/` fixtures,
  never a second reading of the protocol. The WebSocket path of
  `backend_moonraker.c` (`moonraker_ws.c`) stays ESP-only: it is NOT
  exercised here (the fixtures + the real device are enough to cover it, see
  `docs/dev/klipper-simule.en.md`). The address is parsed by `hote_parse()`
  (`firmware/main/core/hote_parse.c`) -- the same function as
  `ecran_configuration.c`. It takes precedence over `--scenario`/`--echec`
  (which belong to the fake backend); ignored for `--app jouet`. Example,
  against `virtual-klipper-printer` (see `docs/dev/klipper-simule.en.md`):
  `--hote localhost:7125`.

**Every image produced here must be opened and looked at, not merely
generated.** A PNG nobody has opened proves nothing more than an unverified
statement.

## LVGL submodule

`simulateur/lvgl` is a Git submodule pinned to the `v9.2.2` tag (shallow
clone, `shallow = true` in `.gitmodules`) — not a vendored copy, not
`FetchContent`.

**The version is an invariant, not a detail.** On the ESP-IDF firmware side,
LVGL comes from the component registry at 9.2.2 (see
`firmware/main/idf_component.yml`, locked in `firmware/dependencies.lock`,
upstream commit `7f07a129e...`). If this submodule drifts away from that
version without the firmware following (or the other way round), the
simulator stops telling the truth about what the device actually displays.
`afficheur.c` carries a `_Static_assert` on
`LVGL_VERSION_MAJOR/MINOR/PATCH` that turns any drift into a compile error
rather than a silent bug. Update both together, never one alone:

```sh
cd simulateur/lvgl
git fetch --tags
git checkout vX.Y.Z
cd ../..
# + update firmware/main/idf_component.yml and regenerate dependencies.lock
git add simulateur/lvgl firmware/main/idf_component.yml firmware/dependencies.lock
```

## `lv_conf.h`: why it exists twice

On the ESP side, the LVGL configuration is generated by Kconfig from
`firmware/sdkconfig.defaults`. Outside ESP-IDF there is no Kconfig:
`simulateur/lv_conf.h` is therefore written and maintained by hand, starting
from the `simulateur/lvgl/lv_conf_template.h` template. Both must stay
aligned on the values a screen can see: colour depth (RGB565), compiled
Montserrat fonts, widgets used, and `LV_COLOR_MIX_ROUND_OFS`.
`LV_USE_SNAPSHOT` deliberately stays disabled: `lv_snapshot_take()` turned
out to return `NULL` on the active screen in this version, including in
RGB565 and in ARGB8888 — the screenshot does not go through it, and enabling
it would make whoever reads this file believe otherwise.

## `simulateur/vendor/stb_image_write.h`

Taken as-is from
`https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h`
(single-header, `nothings/stb` domain). Chosen to avoid adding
`libsdl2-image-dev` to a contributor's prerequisites just to write a PNG, and
because a single header follows the convention already set by
`host-test/vendor/cJSON.c`.

License found at the end of the file (v1.16, checked when it was fetched):
the file explicitly offers a choice of two licenses —

> This software is available under 2 licenses -- choose whichever you
> prefer.
> ALTERNATIVE A - MIT License
> Copyright (c) 2017 Sean Barrett
> [standard MIT text]
> ALTERNATIVE B - Public Domain (www.unlicense.org)
> This is free and unencumbered software released into the public domain.
> [standard Unlicense text]

either MIT (Sean Barrett, 2017), or public domain in the sense of the
Unlicense, whichever the user prefers.

## Line endings

Like `host-test/`, this directory has Windows-side `core.autocrlf=true`
working against it: without precautions, `run.sh` would be rewritten in CRLF
on the next `git checkout` and would become non-executable under WSL.
`simulateur/.gitattributes` forces `eol=lf` on the whole directory to avoid
that trap (see `host-test/README.en.md` for the same problem already hit a
first time).
