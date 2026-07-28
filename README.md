# BTT K-Touch Custom

Firmware ouvert et outillage de rétro-ingénierie pour la **BIGTREETECH K-Touch**,
un écran tactile ESP32-S3 de 5 pouces dont le développement a été arrêté par son
fabricant (dernière version publiée : `v1.1.0`, novembre 2024, qui s'identifie
elle-même comme une beta).

Le projet poursuit deux buts sur une base technique commune : redonner un
firmware vivant et compilable aux possesseurs de l'appareil, et détourner
celui-ci pour piloter un tracker astrophotographique.

## État — jalon 2b atteint

Un firmware maison a tourné sur du matériel réel, depuis le slot OTA `app1`,
sans jamais toucher au firmware d'origine, et l'appareil est revenu au stock sur
commande (jalon 1). Depuis, le jalon 2b a construit une vraie interface tactile
(écran d'accueil, configuration, clavier, confirmations, notifications) au-dessus
d'un socle non visuel testé (analyseurs JSON, store d'état, machine à états de
connexion) : **vérifiée sous [`simulateur/`](simulateur/)** (LVGL + SDL sur PC,
captures PNG à l'appui) et sous [`host-test/`](host-test/) (suite C, voir son
README pour le compte à jour), **mais jamais encore lancée sur l'appareil
réel** — la validation matérielle de cette interface reste à faire, ne pas la
présumer acquise à la lecture de ce dépôt.

Le résultat qui a de la valeur au-delà de ce dépôt est
**[`docs/hardware/pinout.md`](docs/hardware/pinout.md)** : la première
vérification publique du pinout de la K-Touch 5 pouces, affichage et tactile.
Le seul pinout disponible jusqu'ici était celui du Panda Touch 7 pouces, et le
projet `nomadsgalaxy/Prusa-Connect-Touch` notait que la K-Touch « may differ on
a few panel GPIOs or timings ». Cette réserve est levée : il fonctionne tel
quel, sans adaptation.

Vérifié sur matériel le 26 juillet 2026 :

- panneau RGB 800×480 en mode DE, horloge pixel 23 MHz — couleurs, géométrie et
  stabilité conformes ;
- tactile GT911 à l'adresse I²C `0x5D`, correspondance directe avec l'affichage,
  sans rotation ni miroir ;
- PSRAM octale à 80 MHz opérationnelle ;
- 49 minutes de fonctionnement continu sans redémarrage ni fuite mémoire.

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
| [`host-test/`](host-test/) | Tests unitaires (C, sur PC) du code « non visuel » de `firmware/main/core/` et `firmware/main/apps/klipper/` — voir [`host-test/README.md`](host-test/README.md) |
| [`simulateur/`](simulateur/) | Interface LVGL faisant tourner l'écran sur PC (fenêtre SDL ou capture PNG hors écran), sans matériel K-Touch — voir [`simulateur/README.md`](simulateur/README.md) |
| [`exemples/backend_jouet/`](exemples/backend_jouet/) | Backend et écran jouets minimalistes : la preuve, et le modèle, qu'une application tierce s'accroche au même socle que Klipper sans le modifier — voir [`exemples/backend_jouet/README.md`](exemples/backend_jouet/README.md) |
| [`shim/`](shim/) | En-têtes de substitution pour compiler `firmware/main/core/` côté PC (`host-test/`, `simulateur/`) sans ESP-IDF |
| [`tools/ktouch/`](tools/ktouch/) | Bibliothèque Python (standard uniquement) : images ESP32, table de partitions, `otadata` |
| `ktouch-cli.py` | Lanceur : `verify`, `otadata`, `make-otadata`, `image` |
| [`docs/hardware/`](docs/hardware/) | Pinout vérifié, partitionnement, procédure d'installation et de retour |

Pour compiler, il faut ESP-IDF v5.5.5 et renseigner son réseau WiFi ; tout est
dans [`firmware/README.md`](firmware/README.md). Pour l'outillage Python,
`python -m pip install -r tools/requirements.txt` puis `python -m pytest`.
Pour la suite de tests C (analyseurs JSON, store d'état, machine à états de
connexion — aucun besoin de matériel ni d'ESP-IDF, quelques secondes sous
WSL), voir [`host-test/README.md`](host-test/README.md).

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
référencé en sous-module et **jamais redistribué ici**. Cette précaution n'est
pas de forme : ce dépôt affiche un badge « License: MIT » pointant vers un
fichier `LICENSE` qui n'existe pas, et l'API de licence de GitHub n'y détecte
aucune licence. Du code publié sans licence reste sous droit d'auteur plein.

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
