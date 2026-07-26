# Firmware K-Touch custom

Firmware ESP-IDF pour la BIGTREETECH K-Touch (ESP32-S3, PSRAM octale 8 Mio,
flash 16 Mio, panneau RGB 800x480, tactile GT911), basé sur le BSP
`PandaTouch_IDF` de BTT et LVGL 9.

Ce dossier ne contient que le firmware. L'installation sur l'appareil (mode
téléchargement, offsets de flash, précautions) est décrite dans
`docs/hardware/flashing.md`.

## Installer ESP-IDF

Ce projet cible ESP-IDF **v5.5.5**. Suivre le guide d'installation officiel
Espressif pour la cible `esp32s3` :
<https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/index.html>

Sous Windows, il n'existe pas nécessairement de raccourci « ESP-IDF
PowerShell » : l'environnement s'active en sourçant le script d'export dans
**chaque** session PowerShell, avant toute commande `idf.py` :

```powershell
& "<chemin vers esp-idf>\export.ps1"
```

Le script affiche `Done! You can now compile ESP-IDF projects.` une fois prêt.

Note : sur Windows, `idf.py --version` affiche la version du lanceur
`idf-exe` (par ex. `v1.0.3`), pas celle du framework. Pour vérifier la
version réelle d'ESP-IDF :

```powershell
git -C "<chemin vers esp-idf>" describe --tags
```

## Initialiser le sous-module BSP

Le composant `PandaTouch_IDF` est un sous-module git (jamais copié — le dépôt
amont n'a pas de fichier LICENSE, donc on ne redistribue pas son code) :

```bash
git submodule update --init --recursive
```

Vérifier ensuite que `firmware/components/PandaTouch_IDF/include/pandatouch_display.h`
existe.

## Compiler

```powershell
& "<chemin vers esp-idf>\export.ps1"
cd firmware
idf.py set-target esp32s3
idf.py build
```

Le binaire produit est `firmware/build/ktouch-custom.bin`, destiné aux
partitions `app0`/`app1` (0x480000 octets chacune, voir `partitions.csv`).

## Ne pas modifier

`firmware/components/PandaTouch_IDF/` est le code de BTT, importé en
sous-module : ne pas y toucher. Toute adaptation nécessaire se fait côté
`firmware/main/`.
