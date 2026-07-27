# Simulateur PC (LVGL + SDL)

## À quoi ça sert

Ce répertoire fait tourner l'interface LVGL sur PC, sans matériel K-Touch :
soit dans une fenêtre SDL interactive, soit hors écran pour produire une
capture PNG. C'est le socle du jalon 2b (interface) — toutes les tâches
suivantes qui construisent des écrans s'appuient sur `afficheur_demarrer()`,
`afficheur_pomper()` et `afficheur_capturer()` (voir `afficheur.h`) sans
savoir laquelle des deux sorties est active.

**Pourquoi la capture PNG existe.** Le dépôt vit sur une machine où
l'appareil réel n'est pas toujours allumé, et une revue de code ne peut pas
brancher un écran. Sans fichier PNG, une affirmation sur l'aspect d'un écran
n'est vérifiable par personne — ni par l'auteur du changement à distance, ni
par un relecteur. Le mode hors écran rend chaque écran prouvable par un
fichier plutôt que par une déclaration.

Les deux modes partagent le même format de pixel que le panneau réel de la
K-Touch (RGB565) et la même résolution (800×480, `AFFICHEUR_LARGEUR` /
`AFFICHEUR_HAUTEUR`) : une capture montre donc les pixels que l'appareil
pousserait vers sa dalle, pas une approximation.

## Prérequis (sous WSL Debian)

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libsdl2-dev
```

Puis, pour récupérer le sous-module LVGL (voir plus bas pourquoi c'est un
sous-module épinglé) :

```sh
git submodule update --init --recursive --depth 1
```

## Lancer les deux modes

Depuis PowerShell (ne pas invoquer `wsl.exe` depuis Git Bash : Git Bash
dénature les chemins `/mnt/...`) :

```powershell
wsl -d Debian -- bash "<chemin-vers-le-depot>/simulateur/run.sh" --capture "<chemin-vers-le-depot>/simulateur/build/mire.png"
wsl -d Debian -- bash "<chemin-vers-le-depot>/simulateur/run.sh"
```

Ou, depuis un shell WSL, à la racine du dépôt :

```sh
./simulateur/run.sh --capture simulateur/build/mire.png   # mode hors écran, écrit un PNG puis quitte
./simulateur/run.sh                                       # mode fenêtre SDL, interactif
```

`run.sh` configure et compile dans `simulateur/build/` (jamais `/tmp` : sous
WSL, `/tmp` est effacé entre deux invocations lancées depuis Windows), puis
lance l'exécutable avec les arguments reçus.

En mode capture, la sortie attendue est :

```
capture ecrite : <chemin> (800x480)
```

avec un code de sortie 0. En mode fenêtre, une fenêtre SDL 800×480 s'ouvre et
affiche la même mire ; le pointeur de souris agit comme le tactile simulé
(`lv_sdl_mouse_create()`), donc les widgets réagissent au clic.

**Toute image produite ici doit être ouverte et regardée, pas seulement
générée.** Un PNG que personne n'a ouvert ne prouve rien de plus qu'une
déclaration non vérifiée.

## Sous-module LVGL

`simulateur/lvgl` est un sous-module Git épinglé sur le tag `v9.2.2`
(clone superficiel, `shallow = true` dans `.gitmodules`) — pas une copie
vendorisée, pas `FetchContent`.

**La version est un invariant, pas un détail.** Côté firmware ESP-IDF, LVGL
vient du registre de composants en 9.2.2 (voir
`firmware/main/idf_component.yml`, verrouillé dans
`firmware/dependencies.lock`, commit amont `7f07a129e...`). Si ce sous-module
dérive de cette version sans que le firmware suive (ou l'inverse), le
simulateur cesse de dire la vérité sur ce que l'appareil affiche réellement.
`afficheur.c` porte un `_Static_assert` sur `LVGL_VERSION_MAJOR/MINOR/PATCH`
qui transforme toute dérive en erreur de compilation plutôt qu'en bug
silencieux. Mettre à jour les deux ensemble, jamais l'un seul :

```sh
cd simulateur/lvgl
git fetch --tags
git checkout vX.Y.Z
cd ../..
# + mettre à jour firmware/main/idf_component.yml et régénérer dependencies.lock
git add simulateur/lvgl firmware/main/idf_component.yml firmware/dependencies.lock
```

## `lv_conf.h` : pourquoi il existe en double

Côté ESP, la configuration LVGL est générée par Kconfig à partir de
`firmware/sdkconfig.defaults`. Hors ESP-IDF il n'y a pas de Kconfig :
`simulateur/lv_conf.h` est donc écrit et maintenu à la main, à partir du
gabarit `simulateur/lvgl/lv_conf_template.h`. Les deux doivent rester
alignés sur les valeurs qu'un écran peut voir : profondeur de couleur
(RGB565), polices Montserrat compilées, widgets utilisés, et
`LV_COLOR_MIX_ROUND_OFS`. `LV_USE_SNAPSHOT` reste délibérément désactivé :
`lv_snapshot_take()` s'est révélé retourner `NULL` sur l'écran actif dans
cette version, y compris en RGB565 et en ARGB8888 — la capture ne passe pas
par lui, et l'activer laisserait croire le contraire à qui relit ce fichier.

## `simulateur/vendor/stb_image_write.h`

Récupéré tel quel depuis
`https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h`
(single-header, domaine `nothings/stb`). Choisi pour éviter d'ajouter
`libsdl2-image-dev` aux prérequis d'un contributeur pour la seule écriture
d'un PNG, et parce qu'un en-tête unique suit la convention déjà posée par
`host-test/vendor/cJSON.c`.

Licence trouvée en fin de fichier (v1.16, vérifiée à la récupération) : le
fichier propose explicitement deux licences au choix —

> This software is available under 2 licenses -- choose whichever you
> prefer.
> ALTERNATIVE A - MIT License
> Copyright (c) 2017 Sean Barrett
> [texte MIT standard]
> ALTERNATIVE B - Public Domain (www.unlicense.org)
> This is free and unencumbered software released into the public domain.
> [texte Unlicense standard]

soit MIT (Sean Barrett, 2017), soit domaine public au sens de l'Unlicense,
au choix de qui l'utilise.

## Fins de ligne

Comme `host-test/`, ce répertoire a `core.autocrlf=true` côté Windows contre
lui : sans précaution, `run.sh` serait réécrit en CRLF au prochain
`git checkout` et deviendrait inexécutable sous WSL. `simulateur/.gitattributes`
force `eol=lf` sur tout le répertoire pour éviter ce piège (voir
`host-test/README.md` pour le même problème déjà rencontré une première
fois).
