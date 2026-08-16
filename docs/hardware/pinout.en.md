*This page is also available in [French](pinout.md).*

# BIGTREETECH K-Touch pinout — verified on hardware

**Status: display and touch confirmed on hardware on 26 July 2026.**

## What this document contributes

The pinout of the 5-inch K-Touch panel was not documented anywhere publicly. The
only one available was that of the **7-inch Panda Touch**, published by
BIGTREETECH in its `PandaTouch_IDF` component, and the
`nomadsgalaxy/Prusa-Connect-Touch` project noted in its README that the K-Touch
"is the same family but **may differ on a few panel GPIOs or timings**".

**That caveat is now lifted: the 7-inch Panda Touch pinout works as-is on the
5-inch K-Touch**, for display as well as touch, at the exact values reproduced
below and without any adaptation.

## How the verification was carried out

A minimal firmware built on `bigtreetech/PandaTouch_IDF` (commit `396eaba`) was
installed into the `app1` OTA slot of a K-Touch running its factory firmware
`V1.0.0`, through the manufacturer's update mechanism. It displays a test pattern
chosen so that every wiring fault shows up.

| Observation | What it demonstrates |
|---|---|
| Red, green, blue, white bars **in that order** | The 16 data pins are in the right order; no channel swap |
| Four markers visible **at all four corners** | The full 800 × 480 is scanned; resolution and porches correct |
| Crisp text, no offset and no tearing | Correct synchronisation; DE mode operational |
| **Stable image, no flicker and no tearing** | Pixel clock and porches viable (see "Validated timings") |
| Backlight lit | `GPIO21` and the LEDC configuration correct |

Reversibility was demonstrated in two distinct ways, and it is worth not
confusing them. During earlier attempts where WiFi failed to associate, the
device **came back on its own** to the factory firmware, twice, through the
built-in automatic rescue mechanism. Then, once the successful attempt was over,
it went back **on command** via the `/revert` route. The first case proves the
rollback survives a failure; the second, that it is available on demand.

## LCD panel — 16-bit parallel RGB, DE mode

`HSYNC` and `VSYNC` are not routed: the panel runs in DE mode.

| Signal | GPIO |
|---|---|
| PCLK | `5` |
| DE | `38` |
| Reset | `46` |
| Backlight (LEDC PWM) | `21` |
| HSYNC | not routed |
| VSYNC | not routed |

Data pins, in `DATA0` to `DATA15` order:

```
17, 18, 48, 47, 39, 11, 12, 13, 14, 15, 16, 6, 7, 8, 9, 10
```

## Validated timings

| Parameter | Value chosen | First attempt (26 July) |
|---|---|---|
| Resolution | 800 × 480 | 800 × 480 |
| Pixel clock | **14.8 MHz** | 23 MHz |
| HSYNC: pulse / back porch / front porch | **4 / 16 / 16** | 4 / 8 / 8 |
| VSYNC: pulse / back porch / front porch | **4 / 32 / 32** | 4 / 16 / 16 |

**Both columns ran on the device; the first one is the one kept.** The first
attempt (23 MHz, short porches) gives a stable image, with no flicker and no
artefact — that is what was measured during the 49 minutes of continuous
operation described above. But as soon as a real UI redraws continuously, that
setting **tears**: the refresh band becomes visible on movement.

Moving to 14.8 MHz with porches twice as wide removes that tearing, by giving
the RGB controller enough headroom to absorb PSRAM bus contention. It is the
`PT_LCD_PCLK_HZ` value of the upstream component; the firmware does not touch
it.

> The component also defines `PT_LCD_PCLK_HZ_MIN` as 14 MHz: 14.8 MHz therefore
> sits just above the floor BTT intends. To place the other copies in
> circulation, the one shipped with Prusa-Connect-Touch runs at 17 MHz, lowered
> by its authors for the same PSRAM contention reason. Three values do work,
> then, but only the one kept here is confirmed **tear-free on an animated UI**,
> which is the only criterion that counts beyond a static test pattern.

## Backlight

Driven by PWM from the LEDC peripheral: `LEDC_TIMER_1`, channel
`LEDC_CHANNEL_0`, low-speed mode, 11-bit resolution, 30 kHz frequency.

## GT911 touch — confirmed

| Signal | GPIO |
|---|---|
| I²C SCL | `1` |
| I²C SDA | `2` |
| Reset | `41` |
| Interrupt | `40` |

Registers: status `0x814E`, first point `0x814F`, up to 5 simultaneous points.
The controller answers at I²C address `0x5D`.

Initialisation traces obtained on the device:

```
PandaTouch::Touch: ACK 0x5D (no reset)
PandaTouch::Touch: STATUS=0x00
PandaTouch::Touch: PT_GT911 ready @ 0x5D
PandaTouch::LVGL_Touch: PT GT911 LVGL indev registered (800x480 touch -> 800x480 disp)
```

### Orientation and scale — verified

The four markers of the test pattern have their centres at `(12,12)`, `(788,12)`,
`(788,468)` and `(12,468)`. Pressed clockwise starting from the top-left corner,
they reported back:

| Press order | Physical corner | Coordinates read |
|---|---|---|
| 1 | top-left | `(27, 23)` |
| 2 | top-right | `(784, 21)` |
| 3 | bottom-right | `(777, 459)` |
| 4 | bottom-left | `(27, 460)` |

The mapping is direct: **no rotation, no mirroring, no axis inversion**, and the
scale is correct on both axes. The deviations of about ten pixels from the
theoretical centres correspond to the contact area of a finger.

This is a point that deserved verification rather than deduction: touch
orientation is set inside the GT911, independently of the panel. A correct
display pinout in no way implies a correctly oriented touch surface.

## Stability

The device ran for **49 minutes** under this firmware without a single restart —
boot counter still at 1, free heap unchanged down to the byte (7,399,519) between
two readings half an hour apart. No watchdog, no panic, no observable memory
leak.

## Sources

The pin values come from `bigtreetech/PandaTouch_IDF`, published by BIGTREETECH.
That repository contains no licence file, and the component is currently present
in this tree — an open situation, detailed in
[`../licence-du-composant-btt.md`](../licence-du-composant-btt.md) (French
only). Pin numbers and timings are, for their part, hardware facts and not
protectable: it is their **verification on the 5-inch K-Touch** that constitutes
the contribution of this document, and it depends on no licence.
