*This page is also available in [French](README.md).*

# Moonraker transcript recorder (task 4, milestone 3a)

`enregistrer.py` connects to a real Moonraker (see
`docs/dev/klipper-simule.en.md` for setting up the simulated-MCU Klipper
`virtual-klipper-printer` locally) and logs every JSON-RPC message
exchanged over `/websocket`, one JSON object per line, into a
`.jsonl` file that can be replayed by `host-test/tests/test_fixtures_moonraker.c`.

This script implements NO protocol rule of its own: the subscription
request it sends is copied byte for byte from
`firmware/main/apps/klipper/moonraker_rpc.c` (see the synchronisation
comment at the top of `enregistrer.py`) — were it to diverge, the
recorded fixtures would stop representing what the firmware actually
sends.

## Installation (local venv, isolated from `tools/requirements.txt`)

```bash
cd tools/moonraker-record
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt   # ou .venv\Scripts\pip sous Windows natif
```

## Prerequisite: Moonraker must answer

```bash
curl -s localhost:7125/server/info   # doit renvoyer "klippy_state":"ready"
```

See `docs/dev/klipper-simule.en.md` to start `virtual-klipper-printer`.

## Recording the four scenarios

```bash
source .venv/bin/activate

python3 enregistrer.py idle \
    --sortie ../../host-test/fixtures/moonraker/connexion-idle.jsonl

python3 enregistrer.py chauffe \
    --sortie ../../host-test/fixtures/moonraker/chauffe-buse.jsonl

python3 enregistrer.py macro-ok \
    --sortie ../../host-test/fixtures/moonraker/macro-ok.jsonl

python3 enregistrer.py macro-ko \
    --sortie ../../host-test/fixtures/moonraker/macro-inexistante.jsonl
```

- `idle`: identify + subscription, then about 30 s of passive listening.
- `chauffe`: identify + subscription, then
  `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=60`, capturing until the
  TARGET (`target=60`) pushed by a notify_status_update is seen. The
  temperature itself never rises: the simulated MCU of
  virtual-klipper-printer has no thermal model (static ADC, see
  docs/dev/klipper-simule.en.md) — it is the target that is checked, not a
  rise. **The file ends with
  target=60**: the script turns the heater off AFTER closing the
  recording (a separate, unlogged connection) so as not to leave the
  simulator heating, without polluting the fixture.
- `macro-ok`: runs `LOAD_FILAMENT`, a macro actually defined by
  `virtual-klipper-printer` (`example-configs/addons/basic_macros.cfg`) —
  chosen because all it does is an `M117` (no motion, replayable without
  side effects).
- `macro-ko`: runs `MACRO_QUI_NEXISTE_PAS`, capturing the REAL error
  returned by Klipper (not a guess at its shape).

## Before committing a fixture

**Check that no local hostname/IP appears in it** (the
`virtual-klipper-printer` setup is anonymous by construction — everything runs
on `localhost` — but checking remains the discipline):

```bash
grep -EnI '([0-9]{1,3}\.){3}[0-9]{1,3}' host-test/fixtures/moonraker/*.jsonl \
    | grep -v '127\.0\.0\.1'
grep -niE '[a-z0-9.-]+\.(lan|local|home)\b' host-test/fixtures/moonraker/*.jsonl
```

Neither of these two commands must output anything (`localhost`/`127.0.0.1`
are expected and harmless — that is the `Host:` of the initial HTTP
handshake, not captured here since we only record the already established
`/websocket` frames).

## Regenerating the fixtures after a protocol change

If `moonraker_rpc.c` changes the shape of the subscription (a new object
subscribed to, etc.), update `SUBSCRIBE_PARAMS` in `enregistrer.py`
FIRST (along with the synchronisation comment), then re-record the four
scenarios above, then run `host-test/run.sh` again to confirm that the
replay stays green.
