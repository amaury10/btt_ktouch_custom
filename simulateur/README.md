*Cette page est également disponible en [anglais](README.en.md).*

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
- `--app <accueil|jouet>` : choisit l'application montée sur le socle —
  `accueil` (le client Klipper, **c'est le défaut**) ou `jouet`
  (`exemples/backend_jouet/`, la démonstration qu'une application tierce
  s'accroche au même socle). Toute autre valeur retombe sur `accueil` plutôt
  que d'échouer, même politique défensive que `--scenario` face à un numéro
  inconnu.
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
  - `10` (tâche 2, jalon 3a, palier « CR-10 ») : imprimante mono-extrudeur
    d'entrée de gamme, au repos, plateau chauffant, quatre macros simples
    (`BED_MESH_CALIBRATE`, `LOAD_FILAMENT`, `UNLOAD_FILAMENT`,
    `LIGHTS_TOGGLE`).
  - `11` (tâche 2, jalon 3a, palier « U1 ») : changeur d'outils à quatre
    extrudeurs (un chaud parmi les quatre, les trois autres à température
    ambiante), `outil_actif` qui change de tête à chaque cycle simulé, comme
    le ferait un vrai changeur d'outils ; plateau chauffant ; huit macros
    dont `_CACHEE` (préfixe caché — présente dans l'état, c'est à
    l'interface de la filtrer plus tard, pas au backend), `PURGE_PARAM`
    (démontrera des paramètres, tâche 6) et `MACRO_ECHEC`, une sentinelle
    qui échoue TOUJOURS à la commande, quel que soit le scénario actif.
  - `12` (tâche 2, jalon 3a, palier « 8 têtes ») : huit extrudeurs
    synthétiques, tous présents, et 48 macros — exactement
    `KLIPPER_MACROS_MAX`, la borne réelle de `etat_klipper_t::macros` — avec
    `macros_tronquees = true` : seul scénario de ce fichier où ce champ est
    levé, pour signaler qu'un producteur en connaît davantage que ce que la
    structure peut porter (voir le CRITICAL corrigé à la revue de la
    tâche 1 sur cet OOB précis, `firmware/main/web_macros.h`).
  - `10`/`11`/`12` répondent aussi à l'action `BACKEND_ACTION_MACRO`
    (`commande()`, `arguments_json = {"nom":"<macro>"}`) : nom connu →
    `ESP_OK`, `MACRO_ECHEC` → `ESP_FAIL`, nom inconnu ou `arguments_json`
    absent/illisible → `ESP_ERR_NOT_SUPPORTED`.
  - `13` (tâche 5, jalon 3b, mode capture uniquement ; fond mis à jour tâche 7,
    retrait de l'ancien accueil idle) : backend factice IDENTIQUE au
    scénario 10 (« CR-10 », au repos, axes X/Y/Z tous référencés — voir
    `backend_factice.c`), mais ouvre EN PLUS, par-dessus `ECRAN_ACCUEIL_HUB`
    déjà empilé, le dialogue de confirmation de homing
    (`confirmation_ouvrir_ex()`, littéraux inline depuis la tâche 7 — ni
    `ECRAN_ACCUEIL_HUB` ni `ECRAN_DEPLACER` n'exposent plus de bouton Home
    confirmé, voir `simulateur/main.c`) — même schéma que `5`/`6` sur l'écran
    d'impression, mais sur l'accueil au repos : un axe DOIT déjà être
    référencé pour que la capture montre la confirmation plutôt que l'envoi
    direct (spec §7). Sert uniquement à produire `idle-home-confirm.png`.
  - `14` (tâche 6, jalon 3b, mode capture uniquement ; fond mis à jour
    tâche 7, retrait de l'ancien accueil idle) : backend factice IDENTIQUE au
    scénario 10 (« CR-10 », au repos), mais ouvre EN PLUS, par-dessus
    `ECRAN_ACCUEIL_HUB` déjà empilé, le clavier numérique de température
    (`clavier_ouvrir()`, littéral inline depuis la tâche 7 — `ECRAN_ACCUEIL_HUB`
    n'a plus de cellule de température cliquable, voir `simulateur/main.c`) —
    même schéma que `13` pour le homing. Sert uniquement à produire
    `idle-temp-clavier.png`.
  - tout autre numéro retombe sur le comportement du scénario 3 (voir
    `backend_factice_rafraichir()`).
- `--cycles <n>` : avant une capture, avance la boucle simulée de `<n>`
  cycles (un cycle = un rafraîchissement du backend + validation du magasin
  d'état, ce que ferait `boucle_tache()` une fois par seconde sur cible)
  avant de capturer. Sans cette option (ou avec `--cycles 0`), la capture se
  fait à l'instant zéro : tout vaut zéro, l'écran est grisé (liaison encore
  `LIAISON_CONNEXION`). Sans effet en mode fenêtre (qui avance d'un cycle par
  seconde écoulée, en continu).
- `--ecran macros` (tâche 6, jalon 3a) : empile `ECRAN_MACROS` (voir
  `firmware/main/apps/klipper/ecrans/ecran_macros.h`) par-dessus l'écran
  d'accueil déjà construit, à la place de rester sur l'accueil seul. Toute
  autre valeur (ou l'absence de l'option) retombe sur l'accueil seul, même
  politique défensive que `--app`. Jamais combiné à `--scenario 7`/`8`
  (configuration) dans les captures prévues — un seul écran empilé
  par-dessus l'accueil à la fois.
- `--macro <nom>` (tâche 6, mode capture uniquement) : le pendant, en
  l'absence de tactile simulé, d'un tap réel sur un bouton de la grille de
  `ECRAN_MACROS` — construit `{"nom":"<nom>"}` avec la même fonction pure
  que ce bouton et empile `BACKEND_ACTION_MACRO` avant le premier cycle de
  `--cycles`, avec la même bannière synchrone que le tap réel poserait
  ("Macro sent: `<nom>`" ou "Command failed: `<nom>`"). Utile avec
  `--scenario 11` (« U1 ») : un nom connu (ex. `LOAD_FILAMENT`) démontre le
  succès, `MACRO_ECHEC` (sentinelle qui échoue toujours) démontre l'échec
  ASYNCHRONE remonté par le seam générique existant ("Command failed:
  macro", sans le nom — voir le commentaire de `bouton_macro_cb()` dans
  `ecran_macros.c` pour pourquoi ce seam ne peut porter que le nom de
  l'action, jamais ses arguments). Comme `--scenario 9`, laisse de côté le
  bandeau "host connected" (voir plus bas) pour que la bannière survive
  jusqu'à la capture.
- `--echec` : remplace `backend_factice` par un backend jouet local à
  `simulateur/main.c` qui échoue systématiquement (`ESP_FAIL` à chaque
  rafraîchissement). Sert à faire progresser `liaison_t` vers `DEGRADEE` (3
  échecs) puis `HORS_LIGNE` (10 échecs) comme le ferait un hôte injoignable
  sur cible, pour capturer l'état périmé/grisé d'un écran (voir §5.3 de la
  spécification : la barre d'état est seule à afficher cet état, jamais une
  boîte d'erreur sur l'écran lui-même). Incompatible en pratique avec
  `--scenario` (le backend d'échec ignore ce choix).
- `--hote <adresse:port>` (tâche 7, jalon 3a) : remplace `backend_factice`
  par `moonraker_pc.c` (`simulateur/moonraker_pc.c`), un backend HTTP réel
  vers un VRAI Moonraker -- via un GET sockets POSIX nu (HTTP/1.0, sans
  garde-en-vie), PAS `esp_http_client` (ESP-only) ni libcurl. Alimente
  `moonraker_parse_status()`/`rpc_lire_macros()`
  (`firmware/main/apps/klipper/moonraker_parse.c`/`moonraker_rpc.c`) -- les
  MÊMES fonctions pures que le backend ESP et que les fixtures de
  `host-test/`, jamais une deuxième lecture du protocole. Le chemin
  WebSocket de `backend_moonraker.c` (`moonraker_ws.c`) reste ESP-only :
  n'est PAS exercé ici (les fixtures + l'appareil réel suffisent à le
  couvrir, voir `docs/dev/klipper-simule.md`). Adresse analysée par
  `hote_parse()` (`firmware/main/core/hote_parse.c`) -- même fonction que
  `ecran_configuration.c`. Prend le pas sur `--scenario`/`--echec` (propres
  au backend factice) ; ignoré pour `--app jouet`. Exemple, contre
  `virtual-klipper-printer` (voir `docs/dev/klipper-simule.md`) :
  `--hote localhost:7125`.

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
