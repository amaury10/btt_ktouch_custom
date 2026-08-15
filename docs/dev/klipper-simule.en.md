*This page is also available in [French](klipper-simule.md).*

# Simulated Klipper (virtual-klipper-printer) for the Moonraker fixtures

## Why

`host-test/tests/test_moonraker_rpc.c` (task 3, milestone 3a) encodes OUR reading
of the Moonraker documentation: made up entirely of hand-written JSON messages.
`host-test/tests/test_fixtures_moonraker.c` (task 4) replays, through the SAME
pure functions, conversations ACTUALLY recorded against a real Moonraker backed
by a real Klipper (simulated MCU) — that is what keeps the milestone from
discovering the real protocol only once on the physical device.
`virtual-klipper-printer` (mainsail-crew) provides that real Klipper+Moonraker in
a Docker container, without hardware.

## Install and start (WSL Debian)

Prerequisite: a working Docker in the WSL distribution used for the toolchain
(`docker --version`). If it is missing, that is a full-fledged installation
decision of its own (docker-ce inside WSL vs Docker Desktop on the Windows side)
— see the milestone log for the record of the one made here. **The installation
itself is deliberately not detailed in this document** (it involves `sudo`
commands).

```bash
git clone https://github.com/mainsail-crew/virtual-klipper-printer ~/virtual-klipper-printer
cd ~/virtual-klipper-printer
docker compose up -d
```

Cloned outside the K-Touch repository (into `~/`, never inside the repository
root): it is not code belonging to this project, only a local development tool.

Check that Moonraker answers AND that Klippy is ready (`klippy_connected` is not
enough, you need `klippy_state:"ready"`):

```bash
curl -s localhost:7125/server/info
```

The exposed port is `7125` (Moonraker HTTP+WebSocket); `8110` serves a dummy
webcam, of no interest here.

## Re-recording the fixtures

See `tools/moonraker-record/README.en.md` for the full procedure (installing the
local venv, one command per scenario). In short, once vkp is started and ready:

```bash
cd tools/moonraker-record
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python3 enregistrer.py idle       --sortie ../../host-test/fixtures/moonraker/connexion-idle.jsonl
.venv/bin/python3 enregistrer.py chauffe    --sortie ../../host-test/fixtures/moonraker/chauffe-buse.jsonl
.venv/bin/python3 enregistrer.py macro-ok   --sortie ../../host-test/fixtures/moonraker/macro-ok.jsonl
.venv/bin/python3 enregistrer.py macro-ko   --sortie ../../host-test/fixtures/moonraker/macro-inexistante.jsonl
```

Then check that no local hostname/IP is present before committing (see the
corresponding section of the recorder's README), and run `host-test/run.sh` again
to confirm the replay stays green.

## What the fixtures cover

| File | Scenario | What it exercises in moonraker_rpc.c |
|---|---|---|
| `connexion-idle.jsonl` | identify + subscription + ~30 s of passive listening | `rpc_classifier` on a real idle stream (notify_status_update AND notify_proc_stat_update, never silent), `rpc_fusionner_instantane` on the initial snapshot |
| `chauffe-buse.jsonl` | `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=60` | `rpc_fusionner_status` on a notify_status_update pushing a new target (`target`) |
| `macro-ok.jsonl` | `LOAD_FILAMENT` (real vkp macro) | `rpc_lire_reponse` on a real success |
| `macro-inexistante.jsonl` | `MACRO_QUI_NEXISTE_PAS` | `rpc_lire_reponse` **and** the real shape of the Klipper error — see the finding below |

## Finding: an unknown macro does NOT produce a JSON-RPC error

The initial expectation of task 4 was that a non-existent macro would make
`rpc_lire_reponse()` return `succes=false` along with an error text, on the
response correlated to `printer.gcode.script`. **That is wrong against a real
Klipper.** A completely unknown extended command is not treated as a fatal error
by Klipper: it produces an INFO response. The correlated JSON-RPC response is
`{"result":"ok"}` — identical to a macro that succeeds. The real error text
(`// Unknown command:"MACRO_QUI_NEXISTE_PAS"`) only arrives asynchronously, via a
separate `notify_gcode_response` notification — which `rpc_classifier()` today
files under `RPC_MSG_AUTRE` (recognised but ignored: moonraker_rpc.c does not yet
know how to read the text of a gcode response).

**Consequence for the rest of the milestone** (macros screen, task 6):
`rpc_lire_reponse()` on the response correlated to `printer.gcode.script` can NOT
be used to detect the failure of an unknown macro. It would take a dedicated
function for interpreting `notify_gcode_response` (detecting a `// Unknown
command` prefix, `!!`, or another Klipper convention), which does not exist in
this module at this stage. `test_fixtures_moonraker.c` freezes that real
behaviour (see `section_macro_inexistante()`) rather than imposing a result that
no real Moonraker produces.

## Known limitation: no thermal model in this simulator

The simulated MCU of `virtual-klipper-printer` (`simulavr`, an emulated
ATmega644p) returns a **constant** thermistor ADC reading — verified in the raw
container logs (`docker logs printer` / `printer_data/logs/klippy.log`): the
`analog_in_state` frames carry EXACTLY the same bytes before and after a heating
command, across several recordings and several container restarts (~103 °C
observed, identical for `extruder` AND `heater_bed` — clearly a simulator
artefact, not a real ambient temperature). There is therefore NO thermal model
here: `temperature` structurally cannot "rise" no matter how long you wait.

The `chauffe-buse.jsonl` fixture therefore cannot demonstrate a temperature rise
— what it actually demonstrates, and what the replay checks, is that the
**target** (`target`) is indeed pushed by a real Moonraker following a real gcode
command (see `section_chauffe_buse()` in `test_fixtures_moonraker.c`).

## Known limitation: simulated MCU sensitive to host load ("Timer too close")

The simulated MCU (`simulavr`) emulates real hardware timing and has proven
sensitive to the scheduling jitter of this WSL2 host: several recording sessions
saw Klippy go into `shutdown` with `MCU 'mcu' shutdown: Timer too close` —
sometimes right at container start-up, sometimes after several tens of seconds of
activity. This is documented as a known defect of this simulator on a
virtualised/loaded host, not a configuration error of this repository.

Practical consequences for anyone re-recording the fixtures:

- Keep the recording windows **short** (a few seconds to ~30 s) rather than
  letting it run for a long time "just in case".
- If `curl -s localhost:7125/server/info` answers `klippy_state:"shutdown"`,
  `FIRMWARE_RESTART` is not always enough to recover (the failure observed here:
  `Can not update MCU 'mcu' config as it is shutdown`) — the most reliable way is
  to recreate the container:

  ```bash
  cd ~/virtual-klipper-printer
  docker compose down
  docker compose up -d
  # then wait for klippy_state:"ready" before restarting the recorder
  ```

## Reference

- Repository: <https://github.com/mainsail-crew/virtual-klipper-printer>
- Commit used to record the current fixtures:
  `e272bcd1060dad6cee1598d6debdaa5d5afae3c7` (10 January 2026).
