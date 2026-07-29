# Enregistreur de transcripts Moonraker (tâche 4, jalon 3a)

`enregistrer.py` se connecte à un vrai Moonraker (voir
`docs/dev/klipper-simule.md` pour monter le Klipper à MCU simulé
`virtual-klipper-printer` en local) et journalise chaque message
JSON-RPC échangé sur `/websocket`, un objet JSON par ligne, dans un
fichier `.jsonl` rejouable par `host-test/tests/test_fixtures_moonraker.c`.

Ce script n'implémente AUCUNE règle de protocole lui-même : la requête
d'abonnement qu'il envoie est recopiée à l'octet près depuis
`firmware/main/apps/klipper/moonraker_rpc.c` (voir le commentaire de
synchronisation en tête d'`enregistrer.py`) — s'il divergeait, les
fixtures enregistrées cesseraient de représenter ce que le firmware
envoie réellement.

## Installation (venv local, isolé de `tools/requirements.txt`)

```bash
cd tools/moonraker-record
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt   # ou .venv\Scripts\pip sous Windows natif
```

## Prérequis : Moonraker doit répondre

```bash
curl -s localhost:7125/server/info   # doit renvoyer "klippy_state":"ready"
```

Voir `docs/dev/klipper-simule.md` pour démarrer `virtual-klipper-printer`.

## Enregistrer les quatre scénarios

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

- `idle` : identify + abonnement, puis ~30 s d'écoute passive.
- `chauffe` : identify + abonnement, puis
  `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=60`, capture jusqu'à voir
  la CONSIGNE (`target=60`) poussée par un notify_status_update. La
  température, elle, ne monte jamais : le MCU simulé de
  virtual-klipper-printer n'a pas de modèle thermique (ADC statique, voir
  docs/dev/klipper-simule.md) — c'est la consigne qui est vérifiée, pas une
  montée. **Le fichier se termine
  consigne=60** : le script éteint le chauffage APRÈS avoir fermé
  l'enregistrement (connexion séparée, non journalisée) pour ne pas
  laisser le simulateur en chauffe sans polluer le fixture.
- `macro-ok` : exécute `LOAD_FILAMENT`, une macro réellement définie par
  `virtual-klipper-printer` (`example-configs/addons/basic_macros.cfg`) —
  choisie parce qu'elle ne fait qu'un `M117` (pas de mouvement, rejouable
  sans effet de bord).
- `macro-ko` : exécute `MACRO_QUI_NEXISTE_PAS`, capture l'erreur RÉELLE
  renvoyée par Klipper (pas une supposition sur sa forme).

## Avant de committer une fixture

**Vérifier qu'aucun hostname/IP locale n'y figure** (le montage
`virtual-klipper-printer` est anonyme par construction — tout tourne sur
`localhost` — mais vérifier reste la discipline) :

```bash
grep -EnI '([0-9]{1,3}\.){3}[0-9]{1,3}' host-test/fixtures/moonraker/*.jsonl \
    | grep -v '127\.0\.0\.1'
grep -niE '[a-z0-9.-]+\.(lan|local|home)\b' host-test/fixtures/moonraker/*.jsonl
```

Rien ne doit sortir de ces deux commandes (`localhost`/`127.0.0.1` sont
attendus et sans danger — c'est `Host:` de la poignée de main HTTP
initiale, pas capturée ici puisqu'on n'enregistre que les trames
`/websocket` déjà établies).

## Re-générer les fixtures après une évolution du protocole

Si `moonraker_rpc.c` change la forme de l'abonnement (nouvel objet
souscrit, etc.), mettre à jour `SUBSCRIBE_PARAMS` dans `enregistrer.py`
EN PREMIER (avec le commentaire de synchronisation), puis ré-enregistrer
les quatre scénarios ci-dessus, puis relancer `host-test/run.sh` pour
confirmer que le rejeu reste vert.
