*This page is also available in [French](partitions.md).*

# K-Touch flash partitioning

## Overview

The K-Touch has a 16 MiB flash (0x1000000 bytes) partitioned according to the following scheme, identical to that of the official BIGTREETECH v1.1.0 firmware (`K-Touch_v1.1.0_partition.bin`).

## Partition table

| Partition | Type | Subtype | Offset | Size | Purpose |
|-----------|------|-----------|--------|--------|------|
| nvs | data | nvs | 0x9000 | 0x5000 (20 KiB) | NVS key-value storage |
| otadata | data | ota | 0xE000 | 0x2000 (8 KiB) | OTA metadata |
| app0 | app | ota_0 | 0x10000 | 0x480000 (4608 KiB) | OTA slot 0 (active or inactive firmware) |
| app1 | app | ota_1 | 0x490000 | 0x480000 (4608 KiB) | OTA slot 1 (active or inactive firmware) |
| spiffs | data | spiffs | 0x910000 | 0x6E0000 (7040 KiB) | File system |
| coredump | data | coredump | 0xFF0000 | 0x10000 (64 KiB) | Core dump (crash) |

## OTA (Over-The-Air) mechanism

The K-Touch supports two application slots (`app0` and `app1`), allowing an update without service interruption. The selection metadata is stored in the `otadata` partition.

### Structure of the otadata

The `otadata` partition (8 KiB at address 0xE000) contains **two independent copies** of 32 bytes each, one at the start of each 4 KiB sector — that is, at absolute addresses **0xE000** and **0xF000**, and not at bytes 0-31 and 32-63 of a single block. These two copies are normally **not** identical: it is precisely the difference in `ota_seq` between them that carries the whole selection mechanism.

Layout of an entry (32 bytes):

- **Bytes 0-3**: `ota_seq` — sequence number (incremented on each update)
- **Bytes 4-23**: `seq_label` — sequence label (20 bytes, usually 0xFF)
- **Bytes 24-27**: `ota_state` — slot state
  - `0x00000002` (VALID): slot is valid and can be booted
  - `0x00000003` (INVALID): slot is invalid, to be ignored
  - `0x00000004` (ABORTED): update aborted, to be ignored
  - `0xFFFFFFFF` (UNDEFINED): not initialized
  - note: `0x00000001` is PENDING_VERIFY, not VALID — do not confuse the two
- **Bytes 28-31**: `crc` — CRC-32-LE computed **only over the 4 bytes of `ota_seq`**, not over the whole entry

Among the entries whose CRC is correct, the bootloader keeps the one with the highest `ota_seq`, and boots slot **`(ota_seq - 1) % 2`**.

When an OTA update arrives:
1. The new firmware is written into the inactive slot, and the updater writes a new `otadata` entry with `ota_state = NEW` (0x0, if automatic rollback is enabled) or `UNDEFINED` (0xFFFFFFFF, if rollback is disabled), with the highest `ota_seq` of the two copies + 1.
2. On reboot, it is the **bootloader** that performs the NEW → PENDING_VERIFY transition before jumping into the new slot.
3. Once started, it is the **application** itself that must self-validate by calling `esp_ota_mark_app_valid_cancel_rollback()`; it is that call which moves the state to VALID.
4. If the application does not self-validate (crash, watchdog, call never reached) before the next reboot, the bootloader considers the attempt a failure, falls back to the previous slot and marks the new one as ABORTED.

**If no entry is valid, boot does not fail.** The bootloader falls back on the `factory` partition; since the K-Touch has none, it then boots the first OTA slot — that is, the original firmware in `app0`. This is the project's fallback path: **erasing the `otadata` brings back the stock firmware, it never bricks the device.**

## Critical safety rules

The following addresses **must never be written** without careful verification:

- **0x0** — main bootloader (ESP-IDF)
- **0x8000** — partition table
- **0x10000** — OTA slot 0 (may be the active application)

Any write error at these addresses renders the device unusable.

## Verifying a backup — if you have one

> **On this project's development device, no backup is possible.** Its USB-C
> port is unusable, so `esptool` is out of the question and the flash cannot
> be read. Reversibility rests entirely on mechanisms embedded in the
> firmware — see [`flashing.en.md`](flashing.en.md), to be read before any
> manipulation.
>
> This section therefore applies to whoever **does have** serial access. If
> that is your case, take the backup: it is a strictly better safety net than
> anything the firmware can offer, since it allows a byte-by-byte restore.

With serial access, the backup is taken with `esptool` and then verified as follows:

```bash
python ktouch-cli.py verify <sauvegarde.bin>
```

(to be run from the repository root — the `ktouch-cli.py` launcher makes the `tools/ktouch` package reachable without touching `PYTHONPATH`. The dash in its name is deliberate: it makes the file non-importable as a Python module, which structurally prevents it from shadowing the `tools/ktouch/` package.)

This command:
1. Checks that the size is exactly 16 MiB
2. Validates that the partition table matches the official scheme above exactly
3. Reports the contents of both OTA slots (firmware versions, dates, IDF versions)
4. Indicates which slot is currently active
5. Prints a verdict: **« Sauvegarde exploitable : OUI »** or **« NON — ne pas reprogrammer »**

The backup must only be used for a restore if the report indicates `safe_to_flash = True`.

## References

- Official partition: `K-Touch_v1.1.0_partition.bin` from the BIGTREETECH repository
- ESP-IDF documentation: [OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
