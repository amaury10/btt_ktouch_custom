*Cette page est également disponible en [anglais](README.en.md).*

# BTT K-Touch Custom

Firmware ouvert et outillage de rétro-ingénierie pour la **BIGTREETECH K-Touch**,
un écran tactile ESP32-S3 de 5 pouces dont le développement a été arrêté par son
fabricant (dernière version publiée : `v1.1.0`, novembre 2024, qui s'identifie
elle-même comme une beta).

Le projet poursuit deux buts sur une base technique commune : redonner un
firmware vivant et compilable aux possesseurs de l'appareil, et détourner
celui-ci pour piloter un tracker astrophotographique.

![Écran d'accueil](docs/captures/accueil.png)

## État — client Klipper complet, validé sur imprimantes réelles

L'appareil fait tourner un firmware maison depuis un slot OTA, sans jamais
toucher au firmware d'origine, et revient au stock sur commande. Au-dessus de ce
socle, l'interface est aujourd'hui un **client Moonraker complet**, utilisé pour
imprimer pour de vrai sur deux machines : une Creality CR-10 S5 et une Snapmaker
U1 multi-têtes.

Ce que l'écran sait faire :

- **Accueil** : températures de tous les chauffants, graphe d'historique à
  échelle verticale chiffrée, position, outil actif, progression d'impression et
  miniature du gcode en cours.
- **Impression** : liste des fichiers Moonraker, macros, actions (pause,
  reprise, annulation, arrêt d'urgence), console gcode, réglage fin en cours
  d'impression.
- **Réglages machine** : déplacement et prise d'origine, températures avec
  cibles cochables et préréglages PLA/PETG/ABS/TPU, extrudeur, ventilateurs,
  calibration Z, niveau du lit, limites, rétraction, **carte de niveau du lit**
  (heatmap, calibration, profils enregistrés) et **input shaper**.
- **Périphériques** : clé USB (navigation dossier par dossier, envoi vers
  Moonraker), prises pilotées, **Spoolman** (bobines et bobine chargée).
- **Exploitation** : parc de plusieurs imprimantes avec bascule séquentielle,
  réglages WiFi à l'écran, et **mise à jour du firmware par WiFi** avec slot A/B
  et retour arrière.

Le tout reste vérifiable sans matériel : **[`simulateur/`](simulateur/)**
(LVGL + SDL sur PC, captures PNG à l'appui) et [`host-test/`](host-test/) (suite
C, voir son README pour le compte à jour).

Le résultat qui a de la valeur au-delà de ce dépôt reste
**[`docs/hardware/pinout.md`](docs/hardware/pinout.md)** : la première
vérification publique du pinout de la K-Touch 5 pouces, affichage et tactile.
Le seul pinout disponible jusqu'ici était celui du Panda Touch 7 pouces, et le
projet `nomadsgalaxy/Prusa-Connect-Touch` notait que la K-Touch « may differ on
a few panel GPIOs or timings ». Cette réserve est levée : il fonctionne tel
quel, sans adaptation.

Vérifié sur matériel :

- panneau RGB 800×480 en mode DE — couleurs, géométrie et stabilité conformes,
  et 49 minutes de fonctionnement continu sans redémarrage ni fuite mémoire dès
  le premier essai, le 26 juillet 2026 ;
- les timings **tear-free sur une interface animée** (14,8 MHz, porches larges)
  sont documentés à part de ceux qui suffisaient à une mire fixe : la différence
  ne se voit qu'en mouvement, et c'est précisément ce que les autres portages
  n'avaient pas de quoi mesurer ;
- tactile GT911 à l'adresse I²C `0x5D`, correspondance directe avec l'affichage,
  sans rotation ni miroir ;
- PSRAM octale à 80 MHz opérationnelle.

## Aperçu de l'interface

Les 32 images ci-dessous couvrent **tous les écrans du firmware**. Ce ne sont
pas des maquettes : ce sont des captures 800×480 en RGB565 produites par
[`simulateur/`](simulateur/), qui compile le code d'écran réel — les mêmes
pixels que ceux poussés vers la dalle. Elles se régénèrent d'une commande,
[`tools/captures-readme.sh`](tools/captures-readme.sh), ce qui les empêche de
mentir après une évolution de l'interface.

### Accueil et impression

| | | |
|:-:|:-:|:-:|
| ![Accueil](docs/captures/accueil.png)<br>**Accueil** — chauffants, historique de température, position | ![Accueil multi-outils](docs/captures/accueil-multi-outils.png)<br>**Changeur d'outils** — quatre extrudeurs, une courbe par chauffant | ![Impression](docs/captures/impression.png)<br>**Impression en cours** — progression, temps restant, arrêt d'urgence |
| ![Fichiers](docs/captures/fichiers.png)<br>**Fichiers** — la liste gcode servie par Moonraker | | |

### Actions et mouvement

| | | |
|:-:|:-:|:-:|
| ![Actions](docs/captures/actions.png)<br>**Actions** — le sous-menu des commandes machine | ![Déplacer](docs/captures/deplacer.png)<br>**Déplacer** — jog X/Y/Z, pas et vitesse réglables | ![Prise d'origine](docs/captures/homing.png)<br>**Prise d'origine** — par axe ou globale |
| ![Macros](docs/captures/macros.png)<br>**Macros** — les macros Klipper, paginées | ![Températures](docs/captures/temperatures.png)<br>**Températures** — cibles cochables et préréglages matière | ![Extrudeur](docs/captures/extruder.png)<br>**Extrudeur** — extrusion et rétraction manuelles |
| ![Ventilateurs](docs/captures/ventilateurs.png)<br>**Ventilateurs** — curseur et paliers rapides | ![Réglage fin](docs/captures/reglage-fin.png)<br>**Réglage fin** — offset Z, vitesse et débit en cours d'impression | ![Console](docs/captures/console.png)<br>**Console** — gcode envoyé, réponses Klipper |

### Calibration et réglages

| | | |
|:-:|:-:|:-:|
| ![Configuration](docs/captures/menu-configuration.png)<br>**Configuration** — le sommaire des réglages | ![Calibration Z](docs/captures/zcalibrate.png)<br>**Calibration Z** — sonde ou butée, pas au centième | ![Niveau du lit](docs/captures/niveau-lit.png)<br>**Niveau du lit** — vis, Z-tilt, QGL |
| ![Carte du lit](docs/captures/bed-mesh.png)<br>**Carte du lit** — heatmap, bornes chiffrées, profils | ![Input shaper](docs/captures/input-shaper.png)<br>**Input shaper** — type et fréquence par axe | ![Limites](docs/captures/limites.png)<br>**Limites** — vitesse et accélérations machine |
| ![Rétraction](docs/captures/retraction.png)<br>**Rétraction** — rétraction firmware | ![Spoolman](docs/captures/spoolman.png)<br>**Spoolman** — bobines, matière, restant, bobine chargée | |

### Système

| | | |
|:-:|:-:|:-:|
| ![WiFi](docs/captures/wifi.png)<br>**WiFi** — scan et connexion depuis l'écran | ![Prises](docs/captures/power.png)<br>**Prises** — les sorties pilotées par Moonraker | ![Clé USB](docs/captures/usb.png)<br>**Clé USB** — navigation dossier par dossier |
| ![Parc](docs/captures/parc.png)<br>**Parc** — plusieurs imprimantes, état sondé, bascule | ![Mise à jour](docs/captures/updater.png)<br>**Mise à jour** — slot OTA actif et version | ![Premier démarrage](docs/captures/premier-demarrage.png)<br>**Premier démarrage** — appareil non configuré |

### Claviers et dialogues

| | | |
|:-:|:-:|:-:|
| ![Clavier texte](docs/captures/clavier-texte.png)<br>**Clavier texte** — saisie d'adresse | ![Pavé numérique](docs/captures/clavier-temperature.png)<br>**Pavé numérique** — consigne de température | ![Confirmation](docs/captures/confirmation.png)<br>**Confirmation destructive** — annulation d'impression |
| ![Confirmation de homing](docs/captures/homing-confirmation.png)<br>**Confirmation de homing** — axe déjà référencé | ![Saisie au premier démarrage](docs/captures/premier-demarrage-saisie.png)<br>**Saisie de l'hôte** — au premier démarrage | |

## Comment ce firmware peut être réversible sans câble

L'appareil de développement n'est atteignable **qu'en WiFi** : `esptool` est hors
jeu, et aucune sauvegarde des 16 Mo n'est possible. Le firmware porte donc son
propre chemin de retour, en trois mécanismes du plus automatique au plus manuel.

Un **sauvetage automatique** armé avant tout le reste rebascule sur le firmware
d'origine si le réseau ne répond pas dans le délai imparti ; il ne dépend ni de
l'écran, ni du tactile, ni du réseau. Un **compteur de démarrages** en mémoire
RTC ferme la classe des pannes trop rapides pour ce minuteur. Et une requête sur
**`/revert`** rebascule à la demande — sans rien téléverser, puisque le firmware
d'origine n'est jamais écrasé.

Les deux premiers ont été éprouvés en conditions réelles avant que le troisième
ne serve : lors des essais où le WiFi ne s'associait pas, l'appareil est revenu
au stock tout seul, deux fois.

## Contenu du dépôt

| Chemin | Rôle |
|---|---|
| [`firmware/`](firmware/) | Le firmware ESP-IDF et ses instructions de compilation — voir [`firmware/README.md`](firmware/README.md) |
| [`host-test/`](host-test/) | Tests unitaires (C, sur PC) du code « non visuel » ET des écrans, sans matériel ni ESP-IDF — voir [`host-test/README.md`](host-test/README.md) |
| [`simulateur/`](simulateur/) | Interface LVGL faisant tourner l'écran sur PC (fenêtre SDL ou capture PNG hors écran), sans matériel K-Touch — voir [`simulateur/README.md`](simulateur/README.md) |
| [`exemples/backend_jouet/`](exemples/backend_jouet/) | Backend et écran jouets minimalistes : la preuve, et le modèle, qu'une application tierce s'accroche au même socle que Klipper sans le modifier — voir [`exemples/backend_jouet/README.md`](exemples/backend_jouet/README.md) |
| [`shim/`](shim/) | En-têtes de substitution pour compiler `firmware/main/core/` côté PC (`host-test/`, `simulateur/`) sans ESP-IDF |
| [`tools/ktouch/`](tools/ktouch/) | Bibliothèque Python (standard uniquement) : images ESP32, table de partitions, `otadata` |
| [`tools/moonraker-record/`](tools/moonraker-record/) | Enregistreur de sessions Moonraker réelles, qui alimente les fixtures de rejeu des tests — voir [`tools/moonraker-record/README.md`](tools/moonraker-record/README.md) |
| `ktouch-cli.py` | Lanceur : `verify`, `otadata`, `make-otadata`, `image` |
| [`docs/captures/`](docs/captures/) | Les captures d'écran de ce README, régénérables par [`tools/captures-readme.sh`](tools/captures-readme.sh) |
| [`docs/hardware/`](docs/hardware/) | Pinout vérifié, partitionnement, procédure d'installation et de retour |
| [`docs/dev/`](docs/dev/) | Notes de développement, dont la mise en place d'un Klipper simulé pour les tests |
| [`CHANGELOG.md`](CHANGELOG.md) | Notes de version : ce que fait le firmware, ce qui est vérifié, et ses limites connues |

Pour compiler, il faut ESP-IDF v5.5.5 et renseigner son réseau WiFi ; tout est
dans [`firmware/README.md`](firmware/README.md). Pour l'outillage Python,
`python -m pip install -r tools/requirements.txt` puis `python -m pytest`.
Pour la suite de tests C (aucun besoin de matériel ni d'ESP-IDF, quelques
secondes sous WSL), voir [`host-test/README.md`](host-test/README.md).

## Avertissement

Reprogrammer l'appareil se fait à vos risques. La démarche est conçue pour être
réversible — le firmware d'origine reste intact dans son slot — mais **sans accès
série, aucune sauvegarde intégrale n'est possible** : si les mécanismes ci-dessus
échouaient tous, il ne resterait que la voie série pour reprendre la main.
Lisez [`docs/hardware/flashing.md`](docs/hardware/flashing.md) avant de vous
lancer.

## Licence

MIT pour le code de ce dépôt.

Le support matériel provient du composant
[`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF),
**dont la licence est un problème ouvert**. Ce dépôt affiche un badge
« License: MIT » pointant vers un fichier `LICENSE` qui n'existe pas, son README
dit lui-même « provided under the MIT License (*assumed*) », et l'API de licence
de GitHub n'y détecte aucune licence. Du code publié sans licence reste sous
droit d'auteur plein.

Le composant était initialement référencé en sous-module, précisément pour n'en
rien redistribuer. Depuis le 31 juillet 2026 il est **présent dans cet arbre**
(`firmware/components/PandaTouch_IDF/`, 1 826 lignes, avec deux correctifs
locaux) : ce dépôt redistribue donc du code sans licence. Un signalement a été
ouvert chez BIGTREETECH
([`PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1)) ;
la question reste ouverte et doit être tranchée avant toute publication — voir
[`docs/licence-du-composant-btt.md`](docs/licence-du-composant-btt.md), qui
détaille le constat, les options et le signalement rédigé pour BIGTREETECH.

Trois autres dépendances tierces, vendorisées pour compiler sans matériel :

- **LVGL** — MIT. Sous-module Git (`simulateur/lvgl`, épinglé `v9.2.2`, voir
  `.gitmodules`) pour `simulateur/` et `host-test/`, et composant du registre
  ESP-IDF (`firmware/managed_components/lvgl__lvgl`) pour la cible — même
  bibliothèque, deux mécanismes de récupération selon le contexte de
  compilation.
- `simulateur/vendor/stb_image_write.h` — MIT ou domaine public (double
  licence au choix), Sean Barrett. Sert uniquement à écrire les captures PNG
  du mode hors écran.
- `host-test/vendor/cJSON.c` — MIT. Analyseur JSON utilisé par la suite de
  tests hôte.
