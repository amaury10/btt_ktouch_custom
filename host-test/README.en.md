*This page is also available in [French](README.md).*

# Host test harness

## What it is for

This directory runs, on a PC, the unit tests for the "non-visual" part of
the firmware: JSON parsers, state store, connection state machine
(`firmware/main/core/`), as well as the Klipper backend
(`firmware/main/apps/klipper/`, through `moonraker_parse.c` — see this
directory's `CMakeLists.txt` for the exact list of compiled files). None of
these modules touch the hardware (screen, Wi-Fi, GPIO) — they are pure
functions that take JSON or structures as input and produce structures as
output.

**Why `core/` must remain compilable outside ESP-IDF.** That is what makes
this harness possible. If `core/` ever came to depend on an ESP-IDF header
or on a hardware-specific function, these tests would no longer build on a
PC and we would have to go back to flashing the device, rebooting it and
reading the log remotely in order to check the smallest change — several
tens of seconds per iteration instead of a fraction of a second. The JSON
parsers are where most of the milestone 2a bugs will live, because that is
where we interpret the sometimes surprising JSON returned by Moonraker:
testing them fast is what makes it possible to test them often.

Concretely: nothing under `host-test/` may include `freertos/*.h`, nor any
header provided only by the ESP-IDF SDK. There is one deliberate exception
to that rule: `esp_err.h`, which `firmware/main/core/backend.h` needs for
the `esp_err_t` type and the `ESP_OK`/`ESP_ERR_*` constants. Rather than
making `core/` depend on the full SDK for those few definitions,
`shim/esp_err.h` (at the repository root, not under `host-test/` — see
below) provides a stand-in — a separate file, whose values were checked one
by one against the real ESP-IDF header and are guarded by `_Static_assert`
(see that file) — which `CMakeLists.txt` puts at the front of the include
paths for the `tests` target. `esp_*.h` in the broad sense (the full SDK)
remains out of bounds; `esp_err.h` alone has its stand-in explicitly
maintained here. If a file in `core/` needs another ESP-IDF or FreeRTOS
header, that is a sign it does too much and that its pure, testable part
must be extracted — see `firmware/main/core/boucle_cycle.c`, which was
extracted from `boucle.c` for exactly that reason.

**Why this shim lives at the repository root and not under `host-test/`.**
It is no longer just test tooling: `firmware/main/ui/navigation.c` also
needs it to build outside ESP-IDF, and the simulator
(`simulateur/CMakeLists.txt`) links against it. A test convenience file must
not make promises to a shipped artifact (the simulator) — the opposite is
what happened here before it was fixed: anyone moving `host-test/shim/`
without reading `simulateur/CMakeLists.txt` would have broken the simulator
without knowing it. `shim/`, at the root, explicitly says "target header
stand-in, used by every PC-side build" instead of suggesting that
`host-test/` is its sole owner.

## Packages to install under WSL

This harness is meant to run under WSL (Debian), not directly under
Windows. It needs:

```
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config
```

(`build-essential` provides `gcc`; the other packages are listed explicitly
even though `cmake` and `ninja-build` already pull in a toolchain, so that
the list can be copied as is.)

## Running the suite

From a Windows shell (PowerShell), with the repository visible under
`/mnt/<letter>/...` in WSL (replace `<chemin-vers-le-depot>` with the local
location, for example `/mnt/c/Users/vous/BTT-KTouch-Custom`):

```powershell
wsl -d Debian -- sh "<chemin-vers-le-depot>/host-test/run.sh"
```

Or, once inside a WSL shell, from the repository root:

```sh
./host-test/run.sh
```

The execute bit is tracked by Git (mode `100755` in the tree), so
`./run.sh` works directly after a `git clone`. `sh run.sh` remains a valid
alternative should the execute bit not survive a transfer (network share,
zip archive, etc.).

`run.sh` configures a `build/` directory with CMake + Ninja, builds, then
runs the `tests` executable. It propagates the exit code of `tests`:
non-zero as soon as a check fails.

The output lists one line per suite, then a final count. As of 16 August 2026:
**87 suites, 4352 checks, 0 failures** — these numbers grow with every
`tests/test_*.c` file added, so do not take them for a contract; only the
`0 echec(s)` is one. The first and last lines look like this:

```
suite : harnais
suite : contrat
suite : analyseur moonraker
suite : magasin d'etat
suite : liaison
...
suite : parc d'imprimantes (config + etats + parseur de sonde)
suite : bed mesh (store + parseur du sous-objet Moonraker)
suite : ecran spoolman (liste de bobines + selection de l'active)

4352 verification(s), 0 echec(s)
```

(A few `I backend_factice: ...` / `W backend_factice: ...` lines are
interleaved as well: those are normal application logs from the fake
backend during `suite : backend factice` and `suite : boucle_cycle`, not
failures — a failure is always announced by a line starting with `ECHEC`.)

## Sanitizers (ASan + UBSan)

The `tests` target builds by default with AddressSanitizer and
UndefinedBehaviorSanitizer (`-fsanitize=address,undefined,float-cast-overflow
-fno-omit-frame-pointer -fno-sanitize-recover=all`). cJSON (vendored) is
**not** instrumented: it is not code we maintain, and wiring its possible
warnings into our build would make it fail over a problem we cannot fix.

What this buys concretely: three defects already found in this milestone
belong to classes that these sanitizers detect mechanically — a
`float → uint32_t` conversion of a potentially infinite value
(`float-cast-overflow`), a `free()` on an indeterminate pointer left behind
by a failed initialization (heap-use-after-free / invalid-free), and a NaN
silently slipping into a comparison. Spotting them in human review is slower
and less reliable than a tool that fails the build.
`-fno-sanitize-recover=all` is what makes this impossible to work around:
without that option, UBSan merely prints a warning and carries on, and a
warning scrolling past in the output will not be seen.

**gcc note:** with gcc (unlike clang), `float-cast-overflow` is *not*
included in the `-fsanitize=undefined` group — which is why it is listed
explicitly above. Verified empirically with gcc 14.2: a `(uint32_t)` cast of
an infinite float goes unnoticed with `-fsanitize=undefined` alone.

Switch: the CMake option `KTOUCH_HOST_TEST_SANITIZERS` (default `ON`), for a
toolchain that would not support these sanitizers. To turn it off:

```sh
cmake -S host-test -B host-test/build -G Ninja -DKTOUCH_HOST_TEST_SANITIZERS=OFF
cmake --build host-test/build
./host-test/build/tests
```

Once the `OFF` value has been written into the CMake cache of
`host-test/build/`, it survives later reconfigurations (including through
`run.sh`, which calls `cmake` without restating the option) as long as that
`build/` directory is not deleted.

## The micro test framework

No external framework (no vendored Unity, no CTest beyond a minimal
`add_test`): three macros in `tests/petit_test.h`
(`VERIFIER`, `VERIFIER_FLOAT`, `VERIFIER_TEXTE`) are enough to test pure
functions. Each subsequent task adds a `tests/test_*.c` file and a
`void suite_xxx(void)` function called from `tests/main.c`.

## Line endings

This repository has `core.autocrlf=true` on the Windows side; without
precautions, a `git checkout` would reintroduce CRLF line endings into
`run.sh`, which `sh` under WSL refuses to execute. `host-test/.gitattributes`
forces `eol=lf` on this whole directory to avoid that pitfall.
