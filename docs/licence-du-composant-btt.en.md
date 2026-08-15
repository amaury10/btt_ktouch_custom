*This page is also available in [French](licence-du-composant-btt.md).*

# The missing license of `PandaTouch_IDF`

This document records the licensing finding that determined the repository's
architecture — an architecture since abandoned without a decision, see below —
and reproduces the report **filed with BIGTREETECH**
([`PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1)).

## The finding

The [`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF)
component, the source of all the hardware support used here, displays a
"License: MIT" badge in its README pointing to a `LICENSE` file **that does not
exist**. GitHub's license API detects no license on that repository.

Code published without a license remains under full copyright: GitHub's Terms of
Service allow viewing and forking it on the platform, not redistributing it or
including it in a derivative work.

## The consequence, applied then abandoned without a decision

Originally, the component was referenced as a **Git submodule** and was never
copied here: none of its code was redistributed. That was the only reason for
this architectural choice, otherwise needlessly constraining.

**This is no longer the case, and it was never settled.** Commit `64c8d55`
(2026-07-31, "stable tear-free display + BSP integration") moved the component
into the tree: 28 files tracked by git, including 1,826 lines of source under
`firmware/components/PandaTouch_IDF/`. Two local modifications have been added
since — `b9fda5f` (resuming the VFS mount on a SCSI unit-attention, +28 lines in
`pandatouch_msc.c`) and `32f8c9a` (+5 lines in `pandatouch_board.h`) — which
makes it a fork, not a copy. `.gitmodules` now declares only `simulateur/lvgl`.

The repository therefore redistributes unlicensed code today, which is exactly
what this architecture existed to avoid. As long as it stays private, nothing is
distributed; **the question must be decided before any publication.** The volume
to be rewritten is not uniform: display and touch amount to only 511 lines of
glue code on top of `esp_lcd_rgb_panel` and `esp_lcd_touch_gt911` (Apache-2.0,
Espressif), whereas `pandatouch_msc.c` accounts for 801 lines on its own, that
is 44% of the component.

Note that other projects vendor the component relying on the badge — the
`NOTICE` file of `nomadsgalaxy/Prusa-Connect-Touch` lists it as "BigTreeTech,
MIT". That claim rests on the same decorative image.

## Fallback should the situation become a problem

Rewrite the hardware support cleanly: pin numbers and timings are facts, not
protectable — and `docs/hardware/pinout.en.md` now documents them independently,
verified on hardware. The rest is merely a glue layer on top of
`esp_lcd_rgb_panel` and `esp_lcd_touch_gt911`, both published by Espressif under
Apache-2.0.

## Report sent

The text below has been **posted to BIGTREETECH**:
[`bigtreetech/PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1).
It asks for a `LICENSE` file to be added and offers a PR with the standard MIT
text. It had been written much earlier and then set aside; it was **corrected
just before being sent** — it still claimed that this repository redistributed
none of the component, which is no longer true (see the previous section), and
carried a link left as a placeholder.

**As long as there is no answer, nothing changes**: the repository redistributes
unlicensed code, and the question remains to be decided before publication.

---

**Title:** `README shows an MIT badge but the repository contains no LICENSE file`

**Body:**

```markdown
Hi, and thanks for publishing this component — the LCD, GT911 and USB MSC glue
saved us a lot of bring-up work.

There is a licensing gap that makes the component hard to build on, and I think
it is unintentional:

- `README.md` displays a `License: MIT` badge, and the badge links to `LICENSE`.
- There is no `LICENSE` file in the repository.
- GitHub's license API returns 404 for this repository, so no license is
  detected in the repository metadata either.
- The README text itself reads "provided under the MIT License (assumed)".

Under default copyright law, code published without a license is "all rights
reserved": GitHub's Terms of Service let people view and fork it on the
platform, but grant no right to redistribute it or to ship it inside a
derivative work. So despite the badge, downstream projects cannot safely vendor
this component — and several already do, on the strength of the badge alone
(for instance `nomadsgalaxy/Prusa-Connect-Touch`, whose NOTICE file lists this
component as "BigTreeTech, MIT").

Would you consider adding the MIT license text as a `LICENSE` file at the
repository root, with the appropriate copyright line? That single file would
make the badge accurate, and let the component be used the way the badge
already suggests it can be.

Happy to open a PR with the standard MIT text if that is easier — you would
just need to confirm the copyright holder and year.

For context on what the component enabled: we used it to bring up a BIGTREETECH
K-Touch (the 5-inch model), and were able to confirm that the Panda Touch
7-inch pinout and timings work on it unchanged, display and GT911 touch alike.
That did not seem to be documented publicly anywhere. Happy to share the
details if they are useful to you or to others.
```

---
