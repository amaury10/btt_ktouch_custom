*Cette page est également disponible en [anglais](CHANGELOG.en.md).*

# Journal des versions

## v0.9.0 — première version publiable

Premier firmware complet pour la **BIGTREETECH K-Touch 5 pouces**, un écran
tactile ESP32-S3 dont le fabricant a arrêté le développement fin 2024. Il
remplace l'interface d'origine par un **client Moonraker** et s'installe sans
câble, sans ouvrir l'appareil, sans écraser le firmware BigTreeTech.

289 commits depuis le 26 juillet 2026.

### Pourquoi 0.9 et pas 1.0

Le firmware imprime pour de vrai, tous les jours, sur deux machines. Mais deux
défauts connus ne sont pas résolus (voir « Limites connues ») et l'un d'eux est
un plantage silencieux dont la cause n'est pas encore établie. Annoncer 1.0
promettrait une fiabilité qui n'est pas démontrée.

### Ce que fait l'écran

**Accueil.** Températures de tous les chauffants, graphe d'historique avec
échelle verticale chiffrée (0-300 °C), position, outil actif, progression et
miniature du gcode en cours d'impression. Taper une valeur édite sa consigne,
taper un nom affiche ou masque sa courbe.

**Impression.** Fichiers connus de Moonraker, macros, pause/reprise/annulation,
arrêt d'urgence, console gcode, réglage fin en cours d'impression (vitesse,
flux, décalage Z).

**Réglages machine.** Déplacement et prise d'origine, températures avec cibles
cochables et préréglages PLA/PETG/ABS/TPU, extrudeur, ventilateurs, calibration
Z, niveau du lit, limites, rétraction, **carte de niveau du lit** (carte de
chaleur, calibration, profils enregistrés) et **input shaper**.

**Périphériques.** Clé USB avec navigation dossier par dossier et envoi vers
Moonraker, prises pilotées, **Spoolman** (bobines et bobine chargée).

**Exploitation.** Parc de plusieurs imprimantes avec bascule séquentielle,
réglages WiFi à l'écran, **mise à jour du firmware par WiFi** avec slot A/B et
retour arrière, sauvegarde et restauration du firmware d'origine.

Capacités : 8 extrudeurs, 48 macros, 32 fichiers Moonraker, 64 entrées par
dossier USB, 6 imprimantes au parc.

### Installation réversible sans port série

L'appareil n'est atteignable qu'en WiFi : aucune sauvegarde intégrale des 16 Mo
n'est possible, et `esptool` est hors jeu. Le firmware porte donc son propre
chemin de retour, en trois mécanismes indépendants — sauvetage automatique armé
avant tout le reste, compteur de démarrages en mémoire RTC, et une requête
`/revert` à la demande. Le firmware BigTreeTech n'est **jamais écrasé** : il
reste dans son slot OTA et reprend la main sur simple demande.

Les deux premiers mécanismes ont ramené l'appareil au stock tout seuls, deux
fois, lors d'essais où le WiFi ne s'associait pas.

Procédure complète : [`docs/hardware/flashing.md`](docs/hardware/flashing.md).

### Vérifié sur matériel

- Impression réelle pilotée depuis l'écran sur **Creality CR-10 S5** et
  **Snapmaker U1** (multi-têtes), via Moonraker en WiFi.
- Panneau RGB 800×480, tactile GT911 (I²C `0x5D`), PSRAM octale à 80 MHz.
- Timings **tear-free sur interface animée** : 14,8 MHz avec porches larges.
  C'est l'apport de [`docs/hardware/pinout.md`](docs/hardware/pinout.md), la
  première vérification publique du pinout de la K-Touch 5 pouces — le seul
  disponible jusqu'ici était celui du Panda Touch 7 pouces.

### Vérifiable sans matériel

- **87 suites, 4352 vérifications** en quelques secondes, sans ESP-IDF ni
  appareil ([`host-test/`](host-test/)), dont le rejeu de sessions Moonraker
  réelles enregistrées.
- **Simulateur LVGL/SDL** sur PC, en fenêtre ou en capture PNG hors écran
  ([`simulateur/`](simulateur/)).

### Limites connues

- **Plantages silencieux (WDT)** : cinq occurrences observées, cause non
  établie. Les deux cœurs se figent interruptions masquées, ce qui empêche
  l'écriture d'un coredump. Une « boîte noire » en mémoire RTC est en place
  pour identifier la zone active au moment du gel ; elle n'a pas encore parlé.
- **Le clavier tactile rame** et perd des caractères à la saisie rapide.
- La plage du graphe de températures est fixée à la compilation (0-300 °C).
- La carte de niveau du lit est tronquée au-delà de 25×25 points, avec mention
  explicite à l'écran.
- Le panneau Spoolman consulte et sélectionne, mais ne crée ni ne modifie de
  bobine : cet inventaire se saisit au clavier, sur l'interface web Spoolman.
- Un dossier USB de plus de 64 entrées n'a jamais été exercé sur matériel.

### Licence — à lire avant de redistribuer

Le code de ce dépôt est sous MIT. **Le support matériel ne l'est pas.**

Le composant [`bigtreetech/PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF),
d'où vient tout le pilotage de la dalle, du tactile et de l'USB, **n'a aucun
fichier LICENSE** : son badge « MIT » pointe vers un fichier inexistant et son
README dit lui-même « provided under the MIT License (*assumed*) ». Du code
publié sans licence reste sous droit d'auteur plein.

Ce dépôt en contient aujourd'hui 1 826 lignes. Un signalement est ouvert chez
BIGTREETECH ([`PandaTouch_IDF#1`](https://github.com/bigtreetech/PandaTouch_IDF/issues/1)).
Le constat complet et les options sont dans
[`docs/licence-du-composant-btt.md`](docs/licence-du-composant-btt.md).

### Avertissement

Reprogrammer l'appareil se fait à vos risques. La démarche est conçue pour être
réversible, mais **sans accès série aucune sauvegarde intégrale n'est
possible** : si les trois mécanismes de retour échouaient tous, seule la voie
série permettrait de reprendre la main.
