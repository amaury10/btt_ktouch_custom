# Firmware K-Touch custom

Firmware ESP-IDF pour la BIGTREETECH K-Touch (ESP32-S3, PSRAM octale 8 Mio,
flash 16 Mio, panneau RGB 800x480, tactile GT911), basé sur le BSP
`PandaTouch_IDF` de BTT et LVGL 9.

Ce dossier ne contient que le firmware. Son installation sur l'appareil par
WiFi, et le retour au firmware d'origine, sont décrits dans
`docs/hardware/flashing.md` — **à lire avant de flasher quoi que ce soit**.

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

## Renseigner le réseau WiFi

**À faire avant la première compilation**, sans quoi le firmware démarrera sans
identifiants : il ne se connectera pas, et le sauvetage automatique le
ramènera au firmware d'origine au bout de 90 secondes.

```powershell
& "<chemin vers esp-idf>\export.ps1"
cd firmware
idf.py menuconfig
```

Menu **« K-Touch custom »**, options `KTOUCH_WIFI_SSID` et
`KTOUCH_WIFI_PASSWORD`. Elles atterrissent dans `firmware/sdkconfig`, exclu du
dépôt : **aucun identifiant n'est jamais commité**.

Ces valeurs ne servent en réalité qu'à ce firmware. Elles ne sont jamais
réécrites dans la NVS de l'appareil, qui reste celle du firmware d'origine —
voir l'en-tête de `main/wifi.c` pour le détail de cette garantie.

## Compiler

```powershell
& "<chemin vers esp-idf>\export.ps1"
cd firmware
idf.py build
```

> **Ne pas lancer `idf.py set-target esp32s3` en routine.** Cette commande
> supprime et régénère `sdkconfig`, donc **efface les identifiants WiFi saisis
> ci-dessus**, sans avertissement et avec une compilation qui réussit ensuite
> comme si de rien n'était. Elle est de toute façon inutile ici : la cible est
> déjà fixée par `CONFIG_IDF_TARGET` dans `sdkconfig.defaults`. Si vous devez
> vraiment repartir d'une configuration neuve, prévoyez de ressaisir le WiFi
> juste après.

Le binaire produit est `firmware/build/ktouch-custom.bin`, destiné aux
partitions `app0`/`app1` (0x480000 octets chacune, voir `partitions.csv`).

Contrôle rapide du binaire, depuis la racine du dépôt :

```powershell
python ktouch-cli.py image firmware/build/ktouch-custom.bin
```

## Identifiants en français dans le code C

Le code de `main/` utilise des identifiants en français (`minuteur`, `cible`,
`erreur`…) alors que le reste du projet demande des identifiants en anglais.
C'est un écart assumé : il a été relevé en revue et laissé en l'état plutôt que
de renommer massivement du code critique pour la sûreté. L'API exportée, elle,
est en anglais.

## Ne pas modifier

`firmware/components/PandaTouch_IDF/` est le code de BTT, importé en
sous-module : ne pas y toucher. Toute adaptation nécessaire se fait côté
`firmware/main/`.
