*This page is also available in [French](README.md).*

# K-Touch custom firmware

ESP-IDF firmware for the BIGTREETECH K-Touch (ESP32-S3, 8 MiB octal PSRAM,
16 MiB flash, 800x480 RGB panel, GT911 touch), based on BTT's `PandaTouch_IDF`
BSP and LVGL 9.

This folder contains only the firmware. Installing it on the device over WiFi,
and going back to the stock firmware, are described in
`docs/hardware/flashing.en.md` — **read it before flashing anything**.

## Installing ESP-IDF

This project targets ESP-IDF **v5.5.5**. Follow Espressif's official
installation guide for the `esp32s3` target:
<https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/index.html>

On Windows there is not necessarily an "ESP-IDF PowerShell" shortcut: the
environment is activated by sourcing the export script in **every** PowerShell
session, before any `idf.py` command:

```powershell
& "<path to esp-idf>\export.ps1"
```

The script prints `Done! You can now compile ESP-IDF projects.` once it is
ready.

Note: on Windows, `idf.py --version` prints the version of the `idf-exe`
launcher (for example `v1.0.3`), not the version of the framework. To check the
actual ESP-IDF version:

```powershell
git -C "<path to esp-idf>" describe --tags
```

## Initialising the BSP submodule

The `PandaTouch_IDF` component is a git submodule (never copied — the upstream
repository has no LICENSE file, so we do not redistribute its code):

```bash
git submodule update --init --recursive
```

Then check that `firmware/components/PandaTouch_IDF/include/pandatouch_display.h`
exists.

## Setting the WiFi network

**Do this before the first build**, otherwise the firmware will boot without
credentials: it will not connect, and the automatic rescue will bring it back
to the stock firmware after 90 seconds.

```powershell
& "<path to esp-idf>\export.ps1"
cd firmware
idf.py menuconfig
```

Menu **"K-Touch custom"**, options `KTOUCH_WIFI_SSID` and
`KTOUCH_WIFI_PASSWORD`. They end up in `firmware/sdkconfig`, which is excluded
from the repository: **no credentials are ever committed**.

These values in fact only serve this firmware. They are never written back into
the device's NVS, which stays the one from the stock firmware — see the header
of `main/wifi.c` for the details of that guarantee.

## Building

```powershell
& "<path to esp-idf>\export.ps1"
cd firmware
idf.py build
```

> **Do not run `idf.py set-target esp32s3` routinely.** That command deletes and
> regenerates `sdkconfig`, and therefore **erases the WiFi credentials entered
> above**, without warning and with a build that then succeeds as if nothing
> had happened. It is useless here anyway: the target is already fixed by
> `CONFIG_IDF_TARGET` in `sdkconfig.defaults`. If you really need to start from
> a fresh configuration, plan on re-entering the WiFi settings right afterwards.

The binary produced is `firmware/build/ktouch-custom.bin`, meant for the
`app0`/`app1` partitions (0x480000 bytes each, see `partitions.csv`).

Quick check of the binary, from the root of the repository:

```powershell
python ktouch-cli.py image firmware/build/ktouch-custom.bin
```

## French identifiers in the C code

The code in `main/` uses French identifiers (`minuteur`, `cible`, `erreur`…)
whereas the rest of the project asks for English identifiers. This is a
deliberate deviation: it was raised in review and left as is rather than
massively renaming safety-critical code. The exported API, however, is in
English.

## Do not modify

`firmware/components/PandaTouch_IDF/` is BTT's code, imported as a submodule: do
not touch it. Any necessary adaptation is done on the `firmware/main/` side.
