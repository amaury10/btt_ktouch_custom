# Klipper simulé (virtual-klipper-printer) pour les fixtures Moonraker

## Pourquoi

`host-test/tests/test_moonraker_rpc.c` (tâche 3, jalon 3a) encode NOTRE
lecture de la documentation Moonraker : entièrement composé de messages
JSON écrits à la main. `host-test/tests/test_fixtures_moonraker.c`
(tâche 4) rejoue dans les MÊMES fonctions pures des conversations
RÉELLEMENT enregistrées contre un vrai Moonraker adossé à un vrai Klipper
(MCU simulé) — c'est ce qui empêche le jalon de découvrir le protocole réel
seulement une fois sur l'appareil physique. `virtual-klipper-printer`
(mainsail-crew) fournit ce Klipper+Moonraker réels dans un conteneur
Docker, sans matériel.

## Installer et démarrer (WSL Debian)

Prérequis : Docker fonctionnel dans la distribution WSL utilisée pour la
chaîne de compilation (`docker --version`). Si absent, c'est une décision
d'installation à part entière (docker-ce dans WSL vs Docker Desktop côté
Windows) — voir le registre du jalon pour la trace de celle prise ici.
**L'installation elle-même n'est délibérément pas détaillée dans ce
document** (elle implique des commandes `sudo`).

```bash
git clone https://github.com/mainsail-crew/virtual-klipper-printer ~/virtual-klipper-printer
cd ~/virtual-klipper-printer
docker compose up -d
```

Cloné hors du dépôt K-Touch (dans `~/`, jamais sous `E:\Dev\BTT KTouch
Custom`) : ce n'est pas du code de ce projet, seulement un outil de
développement local.

Vérifier que Moonraker répond ET que Klippy est prêt (`klippy_connected`
ne suffit pas, il faut `klippy_state:"ready"`) :

```bash
curl -s localhost:7125/server/info
```

Le port exposé est `7125` (Moonraker HTTP+WebSocket) ; `8110` sert une
webcam factice, sans intérêt ici.

## Ré-enregistrer les fixtures

Voir `tools/moonraker-record/README.md` pour la procédure complète
(installation du venv local, une commande par scénario). En bref, une fois
vkp démarré et prêt :

```bash
cd tools/moonraker-record
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python3 enregistrer.py idle       --sortie ../../host-test/fixtures/moonraker/connexion-idle.jsonl
.venv/bin/python3 enregistrer.py chauffe    --sortie ../../host-test/fixtures/moonraker/chauffe-buse.jsonl
.venv/bin/python3 enregistrer.py macro-ok   --sortie ../../host-test/fixtures/moonraker/macro-ok.jsonl
.venv/bin/python3 enregistrer.py macro-ko   --sortie ../../host-test/fixtures/moonraker/macro-inexistante.jsonl
```

Puis vérifier l'absence de hostname/IP locale avant de committer (voir la
section correspondante du README de l'enregistreur), et relancer
`host-test/run.sh` pour confirmer que le rejeu reste vert.

## Ce que les fixtures couvrent

| Fichier | Scénario | Ce qu'il exerce dans moonraker_rpc.c |
|---|---|---|
| `connexion-idle.jsonl` | identify + abonnement + ~30 s d'écoute passive | `rpc_classifier` sur un flux idle réel (notify_status_update ET notify_proc_stat_update, jamais silencieux), `rpc_fusionner_instantane` sur l'instantané initial |
| `chauffe-buse.jsonl` | `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=60` | `rpc_fusionner_status` sur un notify_status_update poussant une nouvelle consigne (`target`) |
| `macro-ok.jsonl` | `LOAD_FILAMENT` (macro réelle de vkp) | `rpc_lire_reponse` sur un succès réel |
| `macro-inexistante.jsonl` | `MACRO_QUI_NEXISTE_PAS` | `rpc_lire_reponse` **et** la forme réelle de l'erreur Klipper — voir la trouvaille ci-dessous |

## Trouvaille : une macro inconnue ne produit PAS d'erreur JSON-RPC

L'attente initiale de la tâche 4 était qu'une macro inexistante ferait
rendre `rpc_lire_reponse()` avec `succes=false` et un texte d'erreur, sur
la réponse corrélée à `printer.gcode.script`. **C'est faux face à un vrai
Klipper.** Une commande étendue totalement inconnue n'est pas traitée
comme une erreur fatale par Klipper : elle produit une réponse INFO. La
réponse JSON-RPC corrélée est `{"result":"ok"}` — identique à une macro
qui réussit. Le texte d'erreur réel
(`// Unknown command:"MACRO_QUI_NEXISTE_PAS"`) n'arrive que de façon
asynchrone, via une notification `notify_gcode_response` séparée — que
`rpc_classifier()` range aujourd'hui dans `RPC_MSG_AUTRE` (reconnue mais
ignorée : moonraker_rpc.c ne sait pas encore lire le texte d'une réponse
gcode).

**Conséquence pour la suite du jalon** (écran macros, tâche 6) :
`rpc_lire_reponse()` sur la réponse corrélée à `printer.gcode.script` ne
peut PAS servir à détecter l'échec d'une macro inconnue. Il faudrait une
fonction dédiée à l'interprétation de `notify_gcode_response` (détection
d'un préfixe `// Unknown command`, `!!`, ou autre convention Klipper), qui
n'existe pas dans ce module à ce stade. `test_fixtures_moonraker.c` fige
ce comportement réel (voir `section_macro_inexistante()`) plutôt que
d'imposer un résultat qu'aucun vrai Moonraker ne produit.

## Limite connue : pas de modèle thermique dans ce simulateur

Le MCU simulé de `virtual-klipper-printer` (`simulavr`, un ATmega644p
émulé) renvoie une lecture ADC de thermistance **constante** — vérifié
dans les logs bruts du conteneur (`docker logs printer` /
`printer_data/logs/klippy.log`) : les trames `analog_in_state` portent
EXACTEMENT les mêmes octets avant et après une commande de chauffe, sur
plusieurs enregistrements et plusieurs redémarrages du conteneur (~103 °C
observés, identique pour `extruder` ET `heater_bed` — clairement un
artefact du simulateur, pas une température ambiante réelle). Il n'y a
donc AUCUN modèle thermique ici : `temperature` ne peut structurellement
pas « monter » quel que soit le temps d'attente.

Le fixture `chauffe-buse.jsonl` ne peut donc pas démontrer une montée de
température — ce qu'il démontre réellement, et que le rejeu vérifie, c'est
que la **consigne** (`target`) est bien poussée par un vrai Moonraker
suite à une vraie commande gcode (voir `section_chauffe_buse()` dans
`test_fixtures_moonraker.c`).

## Limite connue : MCU simulé sensible à la charge de l'hôte (« Timer too close »)

Le MCU simulé (`simulavr`) émule un timing matériel réel et s'est montré
sensible à la gigue d'ordonnancement de cet hôte WSL2 : plusieurs sessions
d'enregistrement ont vu Klippy passer en `shutdown` avec
`MCU 'mcu' shutdown: Timer too close` — parfois dès le démarrage du
conteneur, parfois après plusieurs dizaines de secondes d'activité. C'est
documenté comme un défaut connu de ce simulateur sur hôte virtualisé/chargé,
pas une erreur de configuration de ce dépôt.

Conséquences pratiques pour qui ré-enregistre les fixtures :

- Garder les fenêtres d'enregistrement **courtes** (quelques secondes à
  ~30 s) plutôt que de laisser tourner longtemps « au cas où ».
- Si `curl -s localhost:7125/server/info` répond `klippy_state:"shutdown"`,
  `FIRMWARE_RESTART` ne suffit pas toujours à récupérer (l'échec observé
  ici : `Can not update MCU 'mcu' config as it is shutdown`) — le plus
  fiable est de recréer le conteneur :

  ```bash
  cd ~/virtual-klipper-printer
  docker compose down
  docker compose up -d
  # puis attendre klippy_state:"ready" avant de relancer l'enregistreur
  ```

## Référence

- Dépôt : <https://github.com/mainsail-crew/virtual-klipper-printer>
- Commit utilisé pour l'enregistrement des fixtures actuelles :
  `e272bcd1060dad6cee1598d6debdaa5d5afae3c7` (10 janvier 2026).
