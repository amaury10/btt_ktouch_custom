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
affiche l'écran d'accueil Klipper (`ECRAN_ACCUEIL`, tâche 6) branché sur la
boucle simulée ; le pointeur de souris agit comme le tactile simulé
(`lv_sdl_mouse_create()`), donc les widgets réagissent au clic. (Avant la
tâche 6, les deux modes affichaient un écran de démonstration jetable — plus
le cas depuis que `simulateur/main.c` empile l'écran réel.)

### Options de ligne de commande

`run.sh` transmet tous les arguments reçus après `--` à l'exécutable ; voir
`main()` dans `simulateur/main.c` pour la liste exacte. Résumé :

- `--capture <chemin>` : mode hors écran, écrit un PNG à `<chemin>` puis
  quitte (code de sortie 0). Sans cette option : mode fenêtre SDL interactif.
- `--scenario <n>` : choisit le scénario du backend factice (voir
  `backend_factice_scenario()` dans `firmware/main/core/backend_factice.h`,
  la seule source de vérité pour cette numérotation — elle a déjà changé une
  fois pendant ce jalon). Numérotation actuelle :
  - `0` — repos (`etat = "standby"`, rien ne chauffe, rien n'imprime) —
    **c'est le défaut** quand `--scenario` n'est pas donné ;
  - `1` — impression en cours, la progression avance à chaque cycle ;
  - `2` — **pause** (impression en cours, `impression_en_pause = true`,
    progression figée à 50 %) — PAS un état de repos, malgré ce qu'un nom de
    variable ou un commentaire pourrait laisser croire à distance (piège
    déjà rencontré une fois pendant la revue de la tâche 6 : ne pas
    supposer, relire `backend_factice.c`) ;
  - `3` — valeurs extrêmes mais **plausibles** (fichier au maximum de sa
    capacité sans octet nul, 350 °C) — sert à vérifier qu'un affichage ne
    déborde nulle part ;
  - `4` — valeurs **aberrantes**, hors plage (999 °C / -999 °C) — sert à
    vérifier qu'un affichage rend `"--"` plutôt qu'un nombre faux
    (`ui_format_temperature()`, voir `firmware/main/ui/widgets/tuile.h`).
  - `5`/`6` (tâche 7, mode capture uniquement) : ne changent rien au backend
    factice lui-même (transmis tel quel à `backend_factice_scenario()`, qui
    les traite comme "tout autre numéro" — voir juste au-dessus) mais font
    en plus ouvrir, par-dessus l'écran d'accueil déjà construit, le clavier
    modal (`5`) ou le dialogue de confirmation destructif (`6`) — voir
    `firmware/main/ui/widgets/clavier.h`/`confirmation.h`. Sert uniquement à
    produire `clavier.png`/`confirmation.png` pour la revue ; sans effet en
    mode fenêtre.
  - `7`/`8` (tâche 8) : démarrent sur `ECRAN_CONFIGURATION` (voir
    `firmware/main/apps/klipper/ecrans/ecran_configuration.h`) à la place de
    `ECRAN_ACCUEIL` — exactement ce que ferait `app_main.c` sur un appareil
    dont `reglages_configures()` rend faux. `7` capture l'écran seul (« au
    repos ») ; `8` ouvre en plus le clavier modal par-dessus, préverni avec
    une adresse comme si elle venait d'être tapée — même technique que le
    scénario 5, un capture-only. Comme `5`/`6`, ces deux numéros ne
    correspondent à aucun scénario du backend factice (repli sur le
    comportement du scénario 3). Utiliser `--cycles 0` avec `7` pour une
    capture "au repos" sans le bandeau "host connected" que `--cycles`
    positif déclenche par ailleurs (voir plus bas).
  - `9` (tâche 9, mode capture uniquement) : démontre l'échec ASYNCHRONE
    d'une commande -- `ui_commander()` l'accepte tout de suite (`ESP_OK`),
    mais son exécution réelle, plus tard par la boucle simulée, échoue
    délibérément (`backend_factice_commande_echoue(true)`, voir
    `firmware/main/core/backend_factice.h`) avant même le premier cycle de
    `--cycles`. Comme `5`/`6`/`7`/`8`, ne correspond à aucun scénario du
    backend factice (repli sur le comportement du scénario 3) ; contrairement
    à eux, laisse aussi le bandeau "host connected" de côté (voir plus bas)
    pour que le bandeau d'échec ("Command failed: pause") reste visible sur
    la capture au lieu d'être remplacé.
  - tout autre numéro retombe sur le comportement du scénario 3 (voir
    `backend_factice_rafraichir()`).
- `--cycles <n>` : avant une capture, avance la boucle simulée de `<n>`
  cycles (un cycle = un rafraîchissement du backend + validation du magasin
  d'état, ce que ferait `boucle_tache()` une fois par seconde sur cible)
  avant de capturer. Sans cette option (ou avec `--cycles 0`), la capture se
  fait à l'instant zéro : tout vaut zéro, l'écran est grisé (liaison encore
  `LIAISON_CONNEXION`). Sans effet en mode fenêtre (qui avance d'un cycle par
  seconde écoulée, en continu).
- `--echec` : remplace `backend_factice` par un backend jouet local à
  `simulateur/main.c` qui échoue systématiquement (`ESP_FAIL` à chaque
  rafraîchissement). Sert à faire progresser `liaison_t` vers `DEGRADEE` (3
  échecs) puis `HORS_LIGNE` (10 échecs) comme le ferait un hôte injoignable
  sur cible, pour capturer l'état périmé/grisé d'un écran (voir §5.3 de la
  spécification : la barre d'état est seule à afficher cet état, jamais une
  boîte d'erreur sur l'écran lui-même). Incompatible en pratique avec
  `--scenario` (le backend d'échec ignore ce choix).

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
