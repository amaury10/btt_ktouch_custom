# Jalon 2b — Simulateur et interface — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Voir la K-Touch fonctionner sur un PC — les mêmes écrans, la même bibliothèque, le même socle — puis faire tourner ces écrans sur l'appareil, en traversant la tranche verticale de la section 10 de la spécification : première configuration, écran d'accueil Klipper, trois actions.

**Architecture:** Le socle du jalon 2a est déjà portable ; ce jalon lui ajoute une couche d'interface qui l'est tout autant. Les écrans ne connaissent ni le réseau ni FreeRTOS : ils lisent un état par une façade (`ui/source_etat.h`) et émettent des actions par la même façade, dont il existe deux implémentations — une sur cible qui délègue à `boucle_instantane()`/`boucle_commander()`, une sur PC qui fait tourner `boucle_cycle()` dans la boucle SDL. Un simulateur LVGL/SDL rend ces écrans sur PC, avec un mode capture hors écran qui écrit un PNG sans serveur graphique.

**Tech Stack:** LVGL 9.2.2 (sous-module épinglé, identique à la version du registre côté ESP-IDF) · SDL2 2.32.4 · CMake + Ninja + gcc sous WSL Debian 13 · ESP-IDF v5.5.5 · `stb_image_write.h` fourni pour l'écriture PNG.

## Global Constraints

Ces contraintes s'appliquent à **toutes** les tâches, sans rappel.

- **Tout ce qui vit sous `firmware/main/ui/` et `firmware/main/apps/klipper/ecrans/` compile sur PC.** Aucune inclusion de `freertos/*`, `esp_wifi.h`, `esp_http_client.h`, `nvs.h` ou `driver/*` dans ces répertoires. `esp_err.h` reste la seule exception tolérée (le dépôt en fournit un tenant-lieu en `shim/esp_err.h`, partagé par le harnais de test et le simulateur), comme au jalon 2a. Un fichier d'interface qui a besoin d'un en-tête ESP-IDF est le signe qu'il fait trop de choses : en extraire la partie pure.
- **Un écran ne parle jamais au backend ni au réseau.** Il lit `ui_etat_instantane()` et appelle `ui_commander()`, qui rend la main immédiatement. Aucun appel HTTP, aucune attente, aucun `vTaskDelay` dans un rappel LVGL — un `POST` peut prendre plusieurs secondes et gèlerait l'interface.
- **La tâche réseau ne touche jamais un widget.** Sur cible, LVGL n'est manipulé que depuis la tâche LVGL du BSP. Aucun appel `lv_*` depuis `core/boucle.c` ni depuis un backend.
- **Un écran n'affiche jamais de boîte d'erreur réseau.** Quand les données sont périmées il les grise ; la barre d'état de l'habillage explique pourquoi. Même règle pour les échecs de commande : le socle affiche la notification, l'écran ne s'en occupe pas.
- **Navigation sans cache.** Empiler construit, dépiler détruit. Seul l'écran visible reçoit `mettre_a_jour`.
- **Le socle alloue, l'extension jamais.** Comme `backend_desc_t.taille_etat` au jalon 2a, `ecran_desc_t.taille_contexte` déclare la taille et le socle appelle `calloc` une fois. Un écran qui alloue son contexte lui-même est un défaut.
- **Textes d'interface en anglais.** Les polices Montserrat compilées dans LVGL ne couvrent que l'ASCII `0x20`-`0x7F` : un « é » ou un tiret cadratin s'affiche en carré vide, défaut déjà constaté au jalon 1 sur cet appareil. Le français exigerait de générer et de suivre une police sur mesure, ce que ce jalon ne fait pas. Les commentaires et la documentation restent en français.
- **Polices identiques des deux côtés.** Montserrat 14, 20, 28 et 48 activées à la fois dans `simulateur/lv_conf.h` et dans `firmware/sdkconfig.defaults`. Toute police utilisée par un écran doit être activée des deux côtés, sans quoi le simulateur ment.
- **La partition `nvs` est partagée avec le firmware d'origine.** `nvs_flash_erase()` ne doit apparaître nulle part. Aucune configuration WiFi n'est jamais persistée.
- **Aucune défaillance locale n'est fatale.** Pas d'`ESP_ERROR_CHECK` sur l'affichage, le tactile, le réseau ou le backend : on journalise et on continue, pour qu'une panne reste diagnosticable à distance par `/logs` et `/revert`.
- **Le sauvetage du jalon 1 reste armé et intact** : minuteur, compteur de démarrages en mémoire RTC, `/revert`, et l'ordre de démarrage de `app_main()` (compteur → sauvetage → NVS → netlog → WiFi → serveur web → affichage). L'interface se construit **après** le serveur web, jamais avant.
- **Aucun identifiant ni chemin local dans un fichier suivi.** `firmware/sdkconfig` est exclu du dépôt et contient les identifiants WiFi. Écrire `<chemin-vers-esp-idf>` et `<racine-du-depot>` dans la documentation.
- ESP-IDF s'active en sourçant `export.ps1` **dans la même invocation** que toute commande `idf.py`.

## Une note sur la forme de ce plan

Les tests et les contrats d'en-tête sont donnés **en entier**, y compris les cas limites et leur justification : c'est là que les jalons précédents ont trouvé leurs défauts, et c'est la partie qu'un plan a le plus intérêt à figer. En revanche, le code de **mise en page LVGL** est spécifié par des contraintes précises (dimensions, polices, couleurs, ordre des opérations, pièges nommés) plutôt que recopié ligne à ligne. C'est un écart délibéré à la règle « pas de description sans code » de la compétence `writing-plans`, et sa raison est concrète : ces lignes-là, je ne peux ni les compiler ni les regarder au moment où j'écris le plan, alors que l'implémenteur, lui, aura le compilateur et une capture PNG sous les yeux à chaque étape. Un pavé de code de mise en page non vérifié dans un plan est du texte que l'implémenteur recopie sans le tester — et sur ce projet, la majorité des défauts trouvés en revue venaient précisément de texte de plan.

**Signatures réelles du harnais de test**, à ne pas réinventer — vérifiées dans `host-test/tests/petit_test.h` :

```c
VERIFIER(condition)                          /* un seul argument */
VERIFIER_FLOAT(obtenu, attendu, tolerance)
VERIFIER_TEXTE(obtenu, attendu)
```

En cas d'échec, la macro imprime `ECHEC <fichier>:<ligne> : <condition stringifiée>` : le libellé descriptif n'a pas sa place dans l'appel, il se met en commentaire juste avant. Les extraits de test de ce plan suivent cette forme.

## Faits d'environnement établis avant ce plan

Vérifiés en exécutant le code, pas déduits. Ne pas les revérifier.

- WSLg fournit un affichage : `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, SDL2 2.32.4 ouvre une fenêtre 800×480 avec le pilote vidéo `x11` et un rendu `opengl`.
- LVGL v9.2.2 (commit `7f07a12`) se compile en autonome avec CMake et embarque ses propres pilotes SDL (`src/drivers/sdl/lv_sdl_window.c`) : aucun dépôt `lv_drivers` séparé n'est nécessaire.
- **`lv_snapshot_take()` rend NULL** sur l'écran actif, en RGB565 comme en ARGB8888, même avec `LV_USE_SNAPSHOT 1`. Ne pas bâtir la capture dessus.
- **Un afficheur hors écran fonctionne** : `lv_display_create()` + `lv_display_set_flush_cb()` recopiant dans un tampon RGB565 à nous rend la trame complète (768 000 octets non nuls sur 768 000 pour un fond uni). C'est le chemin retenu pour la capture, et il ne demande aucun serveur graphique.
- **`/tmp` de WSL est effacé entre deux invocations** depuis PowerShell. Tout répertoire de compilation doit vivre dans le dépôt (`/mnt/e/...`), comme `host-test/build` déjà.
- Côté ESP, LVGL vient du registre en 9.2.2 et sa configuration est générée depuis Kconfig (`CONFIG_LV_CONF_SKIP=y`). Seule `CONFIG_LV_FONT_MONTSERRAT_14` est activée aujourd'hui.
- `firmware/sdkconfig.defaults` **ne réécrit pas** un `sdkconfig` déjà généré : après y avoir ajouté un symbole, supprimer `firmware/sdkconfig` et relancer la génération, puis vérifier dans `firmware/build/config/sdkconfig.h`. Piège déjà payé au jalon 1 sur `CONFIG_ESP_TASK_WDT_PANIC`.

---

### Task 1: Simulateur — socle LVGL/SDL et capture PNG

**Files:**
- Create: `simulateur/CMakeLists.txt`
- Create: `simulateur/lv_conf.h`
- Create: `simulateur/main.c`
- Create: `simulateur/afficheur.c`, `simulateur/afficheur.h`
- Create: `simulateur/vendor/stb_image_write.h` (téléchargé, domaine public)
- Create: `simulateur/run.sh`, `simulateur/README.md`
- Create: sous-module `simulateur/lvgl` épinglé sur `v9.2.2`
- Modify: `.gitmodules`, `.gitignore`

**Interfaces:**
- Consumes: rien du jalon 2a pour l'instant.
- Produces: `afficheur_demarrer(afficheur_mode_t mode)` où le mode vaut `AFFICHEUR_FENETRE` ou `AFFICHEUR_HORS_ECRAN` ; `afficheur_capturer(const char *chemin_png)` ; `afficheur_pomper(uint32_t ms)` qui avance le temps LVGL et traite les événements. Les tâches suivantes s'en servent sans savoir laquelle des deux sorties est active.

**Pourquoi la capture d'abord.** L'utilisateur est absent plusieurs jours et l'appareil est éteint : sans capture, aucune affirmation sur l'aspect d'un écran n'est vérifiable, ni par moi ni par lui à son retour. Un mode capture rend chaque tâche visuelle prouvable par un fichier plutôt que par une déclaration, et il servira ensuite aux rapports de bogue et à la documentation du dépôt.

- [ ] **Step 1: Ajouter LVGL en sous-module épinglé**

```bash
cd "<racine-du-depot>"
git submodule add https://github.com/lvgl/lvgl.git simulateur/lvgl
git -C simulateur/lvgl checkout v9.2.2
git config -f .gitmodules submodule.simulateur/lvgl.shallow true
git add .gitmodules simulateur/lvgl
```

Vérification : `git -C simulateur/lvgl describe --tags` doit rendre `v9.2.2`.

**La version est un invariant, pas un détail.** Côté ESP, LVGL vient du registre en 9.2.2 (`firmware/dependencies.lock`). Si le sous-module dérive, le simulateur cesse de dire la vérité sur l'appareil. L'étape 6 pose un test qui échoue si les deux versions divergent.

- [ ] **Step 2: Écrire `simulateur/lv_conf.h`**

Partir de `simulateur/lvgl/lv_conf_template.h`, le copier en `simulateur/lv_conf.h`, et y poser exactement ces valeurs (le reste garde les défauts du gabarit) :

```c
#define LV_COLOR_DEPTH        16   /* identique au panneau RGB de la K-Touch */
#define LV_USE_SDL             1
#define LV_USE_OS              0   /* LV_OS_NONE, comme côté ESP */
#define LV_TXT_ENC            LV_TXT_ENC_UTF8
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_48  1
#define LV_USE_KEYBOARD        1
#define LV_USE_MSGBOX          1
#define LV_USE_BAR             1
#define LV_USE_TEXTAREA        1
#define LV_USE_LABEL           1
#define LV_USE_BUTTON          1
#define LV_USE_BUTTONMATRIX    1
#define LV_USE_DROPDOWN        1
#define LV_COLOR_MIX_ROUND_OFS 0   /* identique à sdkconfig.defaults */
```

Ne pas activer `LV_USE_SNAPSHOT` : il ne sert pas (voir les faits d'environnement) et sa présence laisserait croire que la capture passe par lui.

En tête du fichier, un commentaire qui dit **pourquoi ce fichier existe en double** : côté ESP la configuration LVGL est générée depuis Kconfig, ici elle est écrite à la main, et les deux doivent rester alignées sur les valeurs ci-dessus — celles dont un écran peut voir la différence.

- [ ] **Step 3: Récupérer `stb_image_write.h`**

```bash
mkdir -p simulateur/vendor
curl -fsSL -o simulateur/vendor/stb_image_write.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
head -3 simulateur/vendor/stb_image_write.h
```

Expected : la première ligne mentionne `stb_image_write` et une version. Vérifier que la licence en fin de fichier est bien double MIT / domaine public, et l'indiquer dans `simulateur/README.md` — le dépôt a vocation à devenir public.

Justification du choix : le PNG évite d'ajouter `libsdl2-image-dev` aux prérequis d'un contributeur, et un en-tête unique du domaine public suit la convention déjà posée par `host-test/vendor/cJSON.c`.

- [ ] **Step 4: Écrire `simulateur/afficheur.h`**

```c
/* Sortie graphique du simulateur : fenêtre SDL, ou rendu hors écran destiné
 * à une capture PNG.
 *
 * Les deux modes partagent le MÊME format de pixel que le panneau de la
 * K-Touch (RGB565) et la même taille (800x480) : une capture montre donc les
 * pixels que l'appareil pousserait vers sa dalle, pas une approximation. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AFFICHEUR_FENETRE = 0,   /* fenêtre SDL, interactif */
    AFFICHEUR_HORS_ECRAN,    /* aucun serveur graphique requis */
} afficheur_mode_t;

#define AFFICHEUR_LARGEUR 800
#define AFFICHEUR_HAUTEUR 480

/* Initialise LVGL et crée l'afficheur. Rend false si SDL refuse d'ouvrir une
 * fenêtre (mode FENETRE sans serveur graphique, typiquement) : l'appelant
 * peut alors retomber sur AFFICHEUR_HORS_ECRAN plutôt que de mourir. */
bool afficheur_demarrer(afficheur_mode_t mode);

/* Avance l'horloge LVGL de `ms` et traite les événements en attente. */
void afficheur_pomper(uint32_t ms);

/* Écrit la trame courante en PNG. Rend false si le mode est FENETRE (les
 * pixels vivent alors dans SDL, pas dans notre tampon), si le chemin ne peut
 * pas être ouvert, ou si l'encodage échoue. */
bool afficheur_capturer(const char *chemin_png);

void afficheur_arreter(void);
```

- [ ] **Step 5: Implémenter `simulateur/afficheur.c`**

Points imposés, chacun pour une raison précise :

- Mode hors écran : `lv_display_create(800, 480)`, `lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565)`, un tampon de rendu partiel de `800 * 60 * 2` octets, et un rappel de vidage qui recopie ligne à ligne dans un cadre `800 * 480 * 2` détenu par le module. Le rappel **doit** appeler `lv_display_flush_ready()` en sortie, sinon LVGL n'émet plus jamais.
- Le rappel de vidage recopie avec la largeur de la **zone** (`lv_area_get_width`) comme pas source et 800 comme pas destination : confondre les deux produit une image décalée en biais, symptôme discret et facile à prendre pour un défaut de mise en page.
- `afficheur_capturer` convertit RGB565 → RGB888 puis appelle `stbi_write_png`. Le désassemblage RGB565 s'écrit une seule fois, ici, avec réplication des bits de poids fort (`r8 = (r5 << 3) | (r5 >> 2)`) pour que le blanc reste `0xFF` et non `0xF8`.
- Mode fenêtre : `lv_sdl_window_create(800, 480)` puis `lv_sdl_mouse_create()`. Le pointeur souris **est** notre tactile en simulation ; sans lui aucun bouton ne réagit.
- `afficheur_pomper` appelle `lv_tick_inc(ms)` **puis** `lv_timer_handler()`, dans cet ordre, et jamais l'inverse : les animations et les temporisations LVGL lisent le temps au moment du traitement.

- [ ] **Step 6: Écrire le test de version LVGL et le CMake**

`simulateur/CMakeLists.txt` :

```cmake
cmake_minimum_required(VERSION 3.16)
project(ktouch-simulateur C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# On ne construit ni les démos ni les exemples ni ThorVG : ils ajoutent
# plusieurs centaines de cibles C++ dont ce projet n'utilise rien.
set(LV_CONF_BUILD_DISABLE_DEMOS   1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_EXAMPLES 1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL 1 CACHE INTERNAL "")
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(lvgl)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(ktouch-sim main.c afficheur.c)
target_include_directories(ktouch-sim PRIVATE . vendor ${SDL2_INCLUDE_DIRS})
target_link_libraries(ktouch-sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
target_compile_options(ktouch-sim PRIVATE -Wall -Wextra -Werror)
```

Le contrôle de version LVGL est une assertion de compilation dans `afficheur.c`, pas un script :

```c
#include "lvgl.h"
/* Le simulateur ne vaut que s'il rend ce que rend l'appareil. Côté ESP, LVGL
 * vient du registre en 9.2.2 (firmware/dependencies.lock) ; ici il vient du
 * sous-module simulateur/lvgl. Si l'un des deux bouge sans l'autre, le
 * simulateur se met à mentir en silence — ce test transforme cette dérive en
 * erreur de compilation. Mettre à jour les deux ensemble, jamais l'un seul. */
_Static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 2 && LVGL_VERSION_PATCH == 2,
               "LVGL du simulateur != 9.2.2 : verifier firmware/dependencies.lock");
```

- [ ] **Step 7: Écrire `simulateur/main.c` (provisoire) et `run.sh`**

`main.c` accepte `--capture <fichier.png>` (mode hors écran, dessine, capture, sort) et sans argument ouvre la fenêtre. À ce stade il dessine une mire de vérification : fond `0x101820`, un titre en Montserrat 28, une ligne de texte en 48, une `lv_bar` à 42 %, et quatre carrés de 24 px dans les coins — assez pour prouver que les quatre polices, le fond, un widget et le cadrage sont corrects.

`simulateur/run.sh` :

```bash
#!/usr/bin/env bash
# Compile et lance le simulateur. Le répertoire de compilation vit dans le
# dépôt et non dans /tmp : sous WSL, /tmp est effacé entre deux invocations
# lancées depuis Windows.
set -euo pipefail
ici="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$ici" -B "$ici/build" -G Ninja >/dev/null
cmake --build "$ici/build"
exec "$ici/build/ktouch-sim" "$@"
```

- [ ] **Step 8: Compiler, capturer, regarder l'image**

```bash
chmod +x simulateur/run.sh
./simulateur/run.sh --capture /mnt/e/Dev/.../simulateur/build/mire.png
```

Expected : sortie `capture ecrite : ... (800x480)`, code de sortie 0, et un PNG de 800×480 **qu'il faut ouvrir et regarder**. Vérifier à l'œil : les quatre coins présents (donc pas de décalage de pas), le texte 48 lisible et non tronqué, la barre remplie à un peu moins de la moitié. Une image que personne n'a regardée ne prouve rien.

Puis, sans argument, vérifier que la fenêtre SDL s'ouvre et affiche la même mire.

- [ ] **Step 9: `.gitignore` et documentation**

Ajouter `simulateur/build/` et `*.png` sous `simulateur/build/` à `.gitignore`. `simulateur/README.md` explique : prérequis (`build-essential cmake ninja-build pkg-config libsdl2-dev`), `git submodule update --init --recursive --depth 1`, les deux modes, la licence de `stb_image_write.h`, et la règle d'alignement de version LVGL.

- [ ] **Step 10: Commit**

```bash
git add simulateur .gitmodules .gitignore
git commit -m "feat(sim): simulateur LVGL/SDL avec capture PNG hors ecran"
```

---

### Task 2: Plateforme — horloge, batterie, qualité WiFi

**Files:**
- Create: `firmware/main/core/plateforme.h`
- Create: `firmware/main/core/plateforme_esp.c`
- Create: `simulateur/plateforme_sim.c`
- Create: `host-test/tests/test_plateforme.c`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/CMakeLists.txt`, `host-test/CMakeLists.txt`

**Interfaces:**
- Consumes: rien.
- Produces: `plateforme_heure(plateforme_heure_t *)`, `plateforme_batterie(plateforme_batterie_t *)`, `plateforme_wifi(plateforme_wifi_t *)`. La barre d'état de la tâche 4 est le seul consommateur prévu.

**Pourquoi ce fichier existe.** La barre d'état affiche quatre choses dont trois viennent du matériel. Si elle les lit directement via `esp_wifi_sta_get_ap_info()`, elle cesse de compiler sur PC et le simulateur perd sa barre d'état — c'est-à-dire précisément l'élément que la spécification rend seul responsable de l'affichage de l'état de liaison. Trois fonctions derrière un en-tête coûtent moins cher que cette perte.

- [ ] **Step 1: Écrire l'en-tête**

```c
/* Les quelques valeurs matérielles dont l'habillage a besoin, derrière une
 * façade que le simulateur peut fournir autrement.
 *
 * Volontairement minuscule : n'ajouter une fonction ici que le jour où un
 * écran en a besoin. Une façade de plateforme qui grossit sans usage devient
 * une couche d'abstraction à maintenir des deux côtés pour rien. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t heures;    /* 0-23 */
    uint8_t minutes;   /* 0-59 */
    bool    valide;    /* false tant qu'aucune heure n'a été obtenue */
} plateforme_heure_t;

typedef struct {
    uint8_t pourcentage;  /* 0-100 */
    bool    en_charge;
    bool    valide;       /* false si la mesure n'est pas disponible */
} plateforme_batterie_t;

typedef struct {
    bool    associe;
    int8_t  rssi;        /* dBm, négatif ; 0 si !associe */
    uint8_t barres;      /* 0-4, dérivé du rssi par plateforme_wifi_barres() */
} plateforme_wifi_t;

void plateforme_heure(plateforme_heure_t *sortie);
void plateforme_batterie(plateforme_batterie_t *sortie);
void plateforme_wifi(plateforme_wifi_t *sortie);

/* Conversion pure, partagée par les deux implémentations pour que le
 * simulateur et l'appareil affichent le même nombre de barres au même
 * signal. Seuils : >= -55 → 4, >= -65 → 3, >= -75 → 2, >= -85 → 1, sinon 0. */
uint8_t plateforme_wifi_barres(int8_t rssi);
```

- [ ] **Step 2: Écrire le test de la conversion (le seul morceau pur)**

`host-test/tests/test_plateforme.c` :

```c
#include "petit_test.h"
#include "plateforme.h"

void suite_plateforme(void)
{
    printf("suite : plateforme\n");
    /* signal excellent */ VERIFIER(plateforme_wifi_barres(-40) == 4);
    /* borne haute 4 barres */ VERIFIER(plateforme_wifi_barres(-55) == 4);
    /* juste sous 4 barres */ VERIFIER(plateforme_wifi_barres(-56) == 3);
    /* borne 3 barres */ VERIFIER(plateforme_wifi_barres(-65) == 3);
    /* borne 2 barres */ VERIFIER(plateforme_wifi_barres(-75) == 2);
    /* borne 1 barre */ VERIFIER(plateforme_wifi_barres(-85) == 1);
    /* signal inutilisable */ VERIFIER(plateforme_wifi_barres(-90) == 0);
    /* Le RSSI d'un ESP32 non associé vaut 0 : ne pas le rendre comme un
     * signal parfait, sans quoi la barre d'état afficherait quatre barres
     * pleines sur un appareil hors réseau. */
    /* rssi 0 (non associe) ne vaut pas 4 barres */ VERIFIER(plateforme_wifi_barres(0) == 4);
}
```

Le dernier cas est délibérément écrit tel quel : `0 dBm` est numériquement un signal parfait, donc la fonction pure rend bien 4. **C'est à `plateforme_wifi()` de ne pas appeler la conversion quand `associe` est faux**, et de poser `barres = 0`. Le commentaire du test doit dire cela, faute de quoi un lecteur pressé « corrigera » la fonction pure et cassera la borne haute.

- [ ] **Step 3: Le lancer et le voir échouer**

```bash
wsl -d Debian -- "<racine-du-depot>/host-test/run.sh"
```

Expected : erreur de compilation, `plateforme.h` introuvable.

- [ ] **Step 4: Implémenter la conversion et les deux plateformes**

`plateforme_esp.c` : heure via `time()`/`localtime_r` (`valide` faux tant que l'année est antérieure à 2020, ce qui est le cas avant toute synchronisation), WiFi via `esp_wifi_sta_get_ap_info()` (`associe` faux si l'appel échoue, et alors `barres = 0`), batterie **non disponible** — `valide = false`. La lecture de la tension de batterie de la K-Touch n'est pas connue à ce stade et l'inventer produirait un chiffre faux affiché en permanence ; un champ invalide s'affiche comme absent, ce qui est honnête.

`plateforme_sim.c` : heure système réelle, WiFi simulé à `-58 dBm` associé, batterie à 76 % non en charge et **valide**, pour que la tâche 4 puisse dessiner le cas nominal. Un commentaire dit que ces valeurs sont synthétiques.

- [ ] **Step 5: Vérifier**

`./host-test/run.sh` → la suite plateforme passe, total en hausse de 8.

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(core): facade plateforme (heure, batterie, qualite wifi)"
```

---

### Task 3: Écrans et navigation

**Files:**
- Create: `firmware/main/ui/ecran.h`
- Create: `firmware/main/ui/navigation.c`, `firmware/main/ui/navigation.h`
- Create: `host-test/tests/test_navigation.c`
- Modify: `host-test/CMakeLists.txt` (LVGL entre dans le harnais), `simulateur/CMakeLists.txt`, `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: rien.
- Produces:
  ```c
  typedef struct {
      const char *id;
      const char *titre;              /* affiché dans la barre d'état */
      size_t      taille_contexte;    /* le socle alloue ; l'écran n'alloue jamais */
      void (*construire)(lv_obj_t *parent, void *contexte);
      void (*mettre_a_jour)(const void *etat, bool donnees_perimees, void *contexte);
      void (*detruire)(void *contexte);
  } ecran_desc_t;
```

  *(`donnees_perimees` ajouté en cours de jalon — revue de la tâche 4 : sans lui, un écran n'a aucun chemin structurel pour recevoir la péremption et griser, et rien n'empêche d'afficher des zéros comme des mesures. Le chrome le calcule via `habillage_donnees_perimees()` et le propage par `navigation_mettre_a_jour()`.)*

```c

  void        navigation_init(lv_obj_t *conteneur);
  esp_err_t   navigation_empiler(const ecran_desc_t *desc);
  void        navigation_depiler(void);
  void        navigation_accueil(void);
  void        navigation_mettre_a_jour(const void *etat, bool donnees_perimees);
  const char *navigation_titre_courant(void);
  const char *navigation_id_courant(void);
  size_t      navigation_profondeur(void);
  ```

**Écart assumé par rapport à la spécification.** La section 4.2 de la spécification décrit `ecran_desc_t` **sans** `taille_contexte`, l'écran gérant son contexte lui-même. Ce plan ajoute le champ et confie l'allocation au socle, par symétrie exacte avec `backend_desc_t.taille_etat` et pour la même raison, énoncée en section 4.1 : sur un appareil sans port série, une fuite dans un chemin appelé en boucle se manifeste par un redémarrage plusieurs heures plus tard, quand personne n'est devant. La navigation détruit et reconstruit à chaque aller-retour ; c'est exactement un chemin appelé en boucle. **Le relecteur doit trancher cet écart explicitement plutôt que le découvrir.**

- [ ] **Step 1: Faire entrer LVGL dans le harnais de test hôte**

`host-test/CMakeLists.txt` réutilise le sous-module du simulateur plutôt que d'en cloner un second :

```cmake
set(LV_CONF_BUILD_DISABLE_DEMOS   1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_EXAMPLES 1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL 1 CACHE INTERNAL "")
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../simulateur/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../simulateur/lvgl lvgl-build)
```

Un seul sous-module, une seule `lv_conf.h`, donc une seule vérité sur la configuration LVGL du côté PC.

**Attention aux désinfecteurs.** Le harnais compile la cible `tests` avec ASan et UBSan. LVGL est du code tiers : le compiler sous désinfecteurs allonge la compilation et peut signaler des motifs internes qui ne sont pas nos défauts. Compiler la bibliothèque `lvgl` **sans** les désinfecteurs et ne garder ceux-ci que sur nos propres objets ; si l'éditeur de liens s'en plaint, garder ASan partout et documenter la décision plutôt que la subir en silence.

Les tests de navigation ont besoin d'un afficheur LVGL initialisé. Fournir dans `host-test/tests/main.c` une initialisation unique : `lv_init()` puis un afficheur hors écran minimal (32×32 suffit, aucun pixel n'est examiné) — sans afficheur, `lv_screen_active()` rend NULL et tout appel LVGL part en assertion.

- [ ] **Step 2: Écrire les tests d'abord**

```c
#include "petit_test.h"
#include "ecran.h"
#include "navigation.h"

/* Écrans jouets : ils comptent leurs appels pour que le test observe le cycle
 * de vie, et n'affichent rien. */
typedef struct { int construits; int maj; int detruits; int dernier_etat; } trace_t;
static trace_t g_trace_a, g_trace_b;

typedef struct { int marqueur; } ctx_a_t;

static void a_construire(lv_obj_t *parent, void *ctx)
{
    (void)parent;
    ctx_a_t *c = ctx;
    /* Prouve que le socle a bien remis le contexte à zéro avant de le confier. */
    if (c->marqueur == 0) g_trace_a.construits++;
    c->marqueur = 0x5A;
}
static void a_maj(const void *etat, void *ctx)
{
    (void)ctx; g_trace_a.maj++; g_trace_a.dernier_etat = *(const int *)etat;
}
static void a_detruire(void *ctx) { (void)ctx; g_trace_a.detruits++; }

static const ecran_desc_t ECRAN_A = {
    .id = "a", .titre = "A", .taille_contexte = sizeof(ctx_a_t),
    .construire = a_construire, .mettre_a_jour = a_maj, .detruire = a_detruire,
};
/* ECRAN_B identique, sur g_trace_b, avec .taille_contexte = 0 pour couvrir
 * l'écran sans état. */

void suite_navigation(void)
{
    printf("suite : navigation\n");
    memset(&g_trace_a, 0, sizeof(g_trace_a));
    memset(&g_trace_b, 0, sizeof(g_trace_b));
    navigation_init(lv_screen_active());

    /* pile vide au depart */ VERIFIER(navigation_profondeur() == 0);
    /* id courant NULL au depart */ VERIFIER(navigation_id_courant() == NULL);

    /* empiler A */ VERIFIER(navigation_empiler(&ECRAN_A) == ESP_OK);
    /* A construit une fois */ VERIFIER(g_trace_a.construits == 1);
    /* profondeur 1 */ VERIFIER(navigation_profondeur() == 1);
    /* titre courant */ VERIFIER(strcmp(navigation_titre_courant(), "A") == 0);

    int etat = 7;
    navigation_mettre_a_jour(&etat);
    /* A recoit la mise a jour */ VERIFIER(g_trace_a.maj == 1);
    /* A recoit le bon etat */ VERIFIER(g_trace_a.dernier_etat == 7);

    /* empiler B */ VERIFIER(navigation_empiler(&ECRAN_B) == ESP_OK);
    /* A n'est PAS detruit sous B */ VERIFIER(g_trace_a.detruits == 0);
    etat = 9;
    navigation_mettre_a_jour(&etat);
    /* Règle de la spécification 5.4 : seul l'écran visible est mis à jour. */
    /* A ne recoit plus rien sous B */ VERIFIER(g_trace_a.maj == 1);
    /* B recoit la mise a jour */ VERIFIER(g_trace_b.maj == 1);

    navigation_depiler();
    /* B detruit au depilement */ VERIFIER(g_trace_b.detruits == 1);
    /* profondeur revenue a 1 */ VERIFIER(navigation_profondeur() == 1);
    etat = 11;
    navigation_mettre_a_jour(&etat);
    /* A recoit a nouveau */ VERIFIER(g_trace_a.maj == 2);
    /* A n'a PAS ete reconstruit */ VERIFIER(g_trace_a.construits == 1);

    navigation_depiler();
    /* depiler le dernier ecran ne le detruit pas */ VERIFIER(g_trace_a.detruits == 0);
    /* profondeur reste 1 */ VERIFIER(navigation_profondeur() == 1);

    /* empiler NULL est refuse */ VERIFIER(navigation_empiler(NULL) == ESP_ERR_INVALID_ARG);
    /* mise a jour sans etat ne plante pas */ VERIFIER((navigation_mettre_a_jour(NULL), true));
}
```

Le cas « dépiler le dernier écran » est le piège classique : dépiler jusqu'au vide laisse un appareil à écran noir sans aucun moyen de revenir, et sur celui-ci il n'y a pas de bouton physique pour s'en sortir. La pile garde donc toujours au moins un écran.

Ajouter aussi un test de profondeur maximale : empiler `NAVIGATION_PROFONDEUR_MAX` écrans puis un de plus, et vérifier que le dernier rend `ESP_ERR_NO_MEM` sans rien construire ni détruire.

- [ ] **Step 3: Lancer, voir échouer**

Expected : `navigation.h` introuvable.

- [ ] **Step 4: Implémenter**

Contraintes d'implémentation :

- Pile statique de `NAVIGATION_PROFONDEUR_MAX` (4) entrées, chacune `{ const ecran_desc_t *desc; void *contexte; lv_obj_t *racine; }`. Pas de `malloc` pour la pile elle-même ; les contextes viennent de `calloc(1, taille_contexte)` et `taille_contexte == 0` donne un contexte NULL, jamais un `calloc(1, 0)` dont le retour est implémentation-dépendante.
- Empiler crée un `lv_obj_t` conteneur plein cadre dans le conteneur racine, le passe à `construire`, et **cache** le conteneur de l'écran précédent (`lv_obj_add_flag(..., LV_OBJ_FLAG_HIDDEN)`) sans le détruire.
- Dépiler appelle `detruire` puis `lv_obj_delete(racine)` puis `free(contexte)`, dans cet ordre : détruire l'objet LVGL d'abord ferait tourner les rappels de suppression de widgets alors que le contexte que ces rappels peuvent lire est encore vivant — l'inverse est ce qui produit une lecture après libération.
- `navigation_accueil()` dépile jusqu'à ne garder que l'écran du fond.
- `navigation_mettre_a_jour(NULL)` ne fait rien ; un `etat` NULL n'est pas une erreur, c'est simplement « rien de neuf à montrer » (cas de la boucle non démarrée).

- [ ] **Step 5: Vérifier, puis commit**

```bash
git commit -m "feat(ui): descripteur d'ecran et pile de navigation"
```

---

### Task 4: Habillage — barre d'état, notifications, façade d'état

**Files:**
- Create: `firmware/main/ui/habillage.c`, `firmware/main/ui/habillage.h`
- Create: `firmware/main/ui/source_etat.h`
- Create: `firmware/main/ui/source_etat_esp.c`
- Create: `simulateur/source_etat_sim.c`
- Create: `host-test/tests/test_habillage.c`

**Interfaces:**
- Consumes: `navigation_*` (tâche 3), `plateforme_*` (tâche 2), `liaison_etat_t` (jalon 2a).
- Produces:
  ```c
  /* ui/source_etat.h — la seule porte par laquelle un écran voit le monde. */
  bool      ui_etat_instantane(void *dest, size_t taille,
                               uint32_t *generation, liaison_etat_t *liaison);
  esp_err_t ui_commander(const char *action, const char *arguments_json);

  /* ui/habillage.h */
  void habillage_construire(lv_obj_t *ecran_racine);
  void habillage_pomper(void);            /* rafraîchit barre d'état + écran visible */
  void habillage_notifier(const char *texte, bool erreur);
  ```

**Pourquoi une façade et pas un appel direct à `boucle_instantane()`.** `core/boucle.c` inclut FreeRTOS : un écran qui l'appelle directement cesse de compiler sur PC, et le simulateur perd tout. La façade a deux implémentations — sur cible elle transmet à `boucle_instantane()`/`boucle_commander()` en trois lignes, sur PC elle lit le magasin d'état que la boucle SDL fait tourner elle-même avec `boucle_cycle()`. C'est le même mécanisme que celui déjà retenu pour `plateforme.h`, appliqué à l'état.

- [ ] **Step 1: Écrire `ui/source_etat.h`**

Le contrat, mot pour mot dans l'en-tête : `ui_etat_instantane()` copie, ne rend jamais de pointeur vers un tampon interne, et rend `false` sans rien toucher si la boucle n'a pas démarré ou si la taille ne correspond pas — reprise exacte du contrat de `boucle_instantane()`. `ui_commander()` rend la main immédiatement et **ne bloque jamais** ; son code de retour dit si la commande a été acceptée dans la file, pas si elle a réussi.

- [ ] **Step 2: Écrire les tests de la barre d'état**

La partie testable sans regarder l'écran, c'est le choix des libellés et des couleurs. L'extraire en fonctions pures dans `habillage.h` et les tester :

```c
/* Pur : quel texte et quelle couleur pour un état de liaison donné. */
const char *habillage_texte_liaison(liaison_etat_t etat);
uint32_t    habillage_couleur_liaison(liaison_etat_t etat);
/* Pur : les données affichées doivent-elles être grisées ? */
bool        habillage_donnees_perimees(liaison_etat_t etat);
```

```c
void suite_habillage(void)
{
    printf("suite : habillage\n");
    /* connexion */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_CONNEXION), "connecting");
    /* en ligne */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_EN_LIGNE), "online");
    /* degradee */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_DEGRADEE), "unstable");
    /* hors ligne */ VERIFIER_TEXTE(habillage_texte_liaison(LIAISON_HORS_LIGNE), "offline");
    /* Un état inconnu ne doit jamais rendre NULL : lv_label_set_text(NULL)
     * déréférence et fait tomber l'interface. */
    /* etat inconnu rend un texte */ VERIFIER(habillage_texte_liaison((liaison_etat_t)99) != NULL);

    /* Règle 5.3 : périmé dès qu'on n'est plus en ligne, y compris pendant la
     * connexion initiale — afficher des zéros en blanc franc pendant les
     * premières secondes ferait lire « buse à 0 C » comme une mesure. */
    /* en ligne : donnees fraiches */ VERIFIER(!habillage_donnees_perimees(LIAISON_EN_LIGNE));
    /* degradee : donnees perimees */ VERIFIER(habillage_donnees_perimees(LIAISON_DEGRADEE));
    /* hors ligne : donnees perimees */ VERIFIER(habillage_donnees_perimees(LIAISON_HORS_LIGNE));
    /* connexion : donnees perimees */ VERIFIER(habillage_donnees_perimees(LIAISON_CONNEXION));

    /* couleurs distinctes en ligne / hors ligne */ VERIFIER(habillage_couleur_liaison(LIAISON_EN_LIGNE) != habillage_couleur_liaison(LIAISON_HORS_LIGNE));
}
```

- [ ] **Step 3: Lancer, voir échouer. Puis implémenter.**

Mise en page imposée (l'appareil fait 800×480) :

- Barre d'état : bande pleine largeur de 44 px en haut, fond `0x1B2430`. À gauche le titre de l'écran courant (`navigation_titre_courant()`, Montserrat 20). À droite, dans l'ordre : pastille + texte de liaison, barres WiFi, batterie (masquée si `!valide`), heure `HH:MM` (masquée si `!valide`).
- Bouton retour à gauche du titre, visible **seulement** si `navigation_profondeur() > 1`, agissant sur `navigation_depiler()`.
- Zone de contenu : le reste, 800×436, passée à `navigation_init()`.
- Notifications : un bandeau qui se superpose en bas, 60 px, apparaît sur `habillage_notifier()` et disparaît seul après 4 s via un `lv_timer` à répétition unique. Une seconde notification pendant qu'une première est affichée **remplace** le texte et réarme le minuteur, elle ne les empile pas.

`habillage_pomper()` fait, dans cet ordre : lire `ui_etat_instantane()`, rafraîchir la barre d'état, et n'appeler `navigation_mettre_a_jour()` **que si la génération a changé** ou si l'état de liaison a changé (le grisage dépend de la liaison, pas du contenu).

- [ ] **Step 4: Implémenter les deux `source_etat`, vérifier, commit**

```bash
git commit -m "feat(ui): habillage (barre d'etat, notifications) et facade d'etat"
```

---

### Task 5: Widgets communs — tuile de valeur et barre de progression

**Files:**
- Create: `firmware/main/ui/widgets/tuile.c`, `tuile.h`
- Create: `firmware/main/ui/widgets/progression.c`, `progression.h`
- Create: `host-test/tests/test_widgets.c`

**Interfaces:**
- Produces : un type `tuile_t` **opaque du point de vue de l'écran** — une structure à champs publics `{ lv_obj_t *racine; lv_obj_t *libelle; lv_obj_t *valeur; lv_obj_t *consigne; }` déclarée dans `tuile.h`, allouée par l'écran dans son propre contexte (jamais par `malloc`), et remplie par `tuile_creer(tuile_t *, lv_obj_t *parent, const char *libelle)`. Puis `tuile_definir_valeur(tuile_t *, const char *)`, `tuile_definir_consigne(tuile_t *, const char *)`, `tuile_griser(tuile_t *, bool)` ; `progression_t` et `progression_creer/definir/griser` suivent exactement la même forme. Plus, et c'est la partie testée, les formateurs purs :
  ```c
  /* Écrit "205.0" dans `sortie` ; "--" si la valeur n'est pas plausible. */
  void ui_format_temperature(char *sortie, size_t taille, float celsius);
  /* Écrit "1h 23m" ; "--" si secondes vaut 0 (inconnu). */
  void ui_format_duree(char *sortie, size_t taille, uint32_t secondes);
  ```

**Ne pas écrire plus de widgets que la tranche verticale n'en exige** (spécification §6) : ni éditeur de consigne, ni graphe, ni liste paresseuse. Ils arriveront avec l'écran qui en aura besoin.

- [ ] **Step 1: Tests des formateurs, écrits en premier**

```c
void suite_widgets(void)
{
    char b[16];
    printf("suite : widgets (formateurs)\n");

    ui_format_temperature(b, sizeof(b), 205.0f);   /* buse nominale */ VERIFIER_TEXTE(b, "205.0");
    ui_format_temperature(b, sizeof(b), 0.0f);     /* zero est une vraie mesure */ VERIFIER_TEXTE(b, "0.0");
    ui_format_temperature(b, sizeof(b), 59.94f);   /* arrondi au dixieme */ VERIFIER_TEXTE(b, "59.9");
    ui_format_temperature(b, sizeof(b), -12.0f);   /* negatif invraisemblable */ VERIFIER_TEXTE(b, "--");
    ui_format_temperature(b, sizeof(b), 999.0f);   /* au-dela du plausible */ VERIFIER_TEXTE(b, "--");
    ui_format_temperature(b, sizeof(b), NAN);      /* NaN */ VERIFIER_TEXTE(b, "--");
    ui_format_temperature(b, sizeof(b), INFINITY); /* infini */ VERIFIER_TEXTE(b, "--");

    ui_format_duree(b, sizeof(b), 0);       /* inconnu */ VERIFIER_TEXTE(b, "--");
    ui_format_duree(b, sizeof(b), 59);      /* moins d'une minute */ VERIFIER_TEXTE(b, "0m");
    ui_format_duree(b, sizeof(b), 83);      /* une minute */ VERIFIER_TEXTE(b, "1m");
    ui_format_duree(b, sizeof(b), 3600);    /* une heure pile */ VERIFIER_TEXTE(b, "1h 00m");
    ui_format_duree(b, sizeof(b), 5025);    /* heures et minutes */ VERIFIER_TEXTE(b, "1h 23m");
    /* Borne haute de etat_klipper.h : KLIPPER_TEMPS_RESTANT_MAX_S = 359999. */
    ui_format_duree(b, sizeof(b), 359999u); /* borne haute */ VERIFIER_TEXTE(b, "99h 59m");

    /* Un tampon trop court ne doit jamais déborder ni laisser la chaîne
     * sans terminateur : le résultat est tronqué, la chaîne reste valide. */
    char court[4];
    ui_format_duree(court, sizeof(court), 5025);
    /* tampon court reste termine */ VERIFIER(court[3] == '\0');
}
```

Les valeurs invraisemblables ne sont pas de la coquetterie : Moonraker renvoie parfois `0` pour une sonde absente et des valeurs aberrantes pendant un redémarrage de klippy. Afficher `--` plutôt qu'un nombre faux est la même règle que le grisage — ne jamais présenter comme mesuré ce qui ne l'est pas. Bornes retenues : `[-5, 500]` °C.

- [ ] **Step 2: Voir échouer, implémenter, vérifier**

`ui_format_temperature` utilise `snprintf("%.1f")`. Attention à la dépendance déjà notée au jalon 2a : `CONFIG_LIBC_NEWLIB_NANO_FORMAT` retire `%f` de `printf` sur cible. Vérifier dans `firmware/build/config/sdkconfig.h` que ce symbole est absent ou à 0 ; s'il est à 1, le corriger dans `sdkconfig.defaults` **et** le signaler dans le rapport, car tout affichage de température en dépend.

- [ ] **Step 3: Commit**

```bash
git commit -m "feat(ui): tuile de valeur, barre de progression et formateurs"
```

---

### Task 6: Écran d'accueil Klipper

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_accueil.c`, `.h`
- Modify: `simulateur/main.c` (fait tourner la boucle avec le backend factice)
- Create: `host-test/tests/test_ecran_accueil.c`

**Interfaces:**
- Consumes: `etat_klipper_t` (jalon 2a), tuile, progression, navigation, habillage.
- Produces: `const ecran_desc_t ECRAN_ACCUEIL;`

**C'est la première fois que la chaîne complète tourne** : backend factice → `boucle_cycle` → magasin d'état → génération → écran. À la fin de cette tâche, une capture PNG montre la K-Touch en train d'imprimer.

- [ ] **Step 1: Faire tourner la boucle dans le simulateur**

`simulateur/main.c` devient : `afficheur_demarrer`, `habillage_construire`, `navigation_empiler(&ECRAN_ACCUEIL)`, puis une boucle qui appelle `afficheur_pomper(5)` en continu, `boucle_cycle()` une fois par seconde via un compteur, `etat_store_valider()` juste après, et `habillage_pomper()` à chaque tour.

Le mode capture accepte `--scenario <n>` pour choisir le scénario du backend factice et `--cycles <n>` pour avancer d'autant de cycles avant de capturer — sans quoi on ne capture que l'instant zéro, où tout vaut zéro et où l'écran est gris.

- [ ] **Step 2: Écrire le test de l'écran**

Ce qui se teste sans regarder : que `mettre_a_jour` ne plante sur aucun état pathologique, et que le grisage suit la liaison. Le test construit l'écran sur un afficheur hors écran et lui envoie une série d'états — tout à zéro, progression à 100 %, températures aberrantes, nom de fichier de la longueur maximale sans terminateur naturel, temps restant à la borne — en vérifiant qu'aucun appel ne fait tomber l'assertion LVGL et que les libellés lus par `lv_label_get_text()` valent ce qu'on attend.

Cas obligatoire : **un nom de fichier occupant tout le champ sans octet nul**. `etat_klipper_t` est un POD à champs fixes ; si l'écran passe le tableau directement à `lv_label_set_text()`, il lit au-delà. Le test doit remplir le champ entièrement et vérifier que le libellé fait au plus la taille du champ.

- [ ] **Step 3: Voir échouer, implémenter**

Mise en page, 800×436 sous la barre d'état :

- Deux tuiles en haut, 380×140 chacune : `Nozzle` avec valeur courante en Montserrat 48 et consigne en 20 juste dessous (`205.0 / 210.0`), puis `Bed` à l'identique.
- Nom du fichier en Montserrat 20 sur une ligne, avec `LV_LABEL_LONG_DOT` pour que les noms longs se terminent par des points de suspension au lieu de sortir du cadre.
- Barre de progression pleine largeur avec le pourcentage à un décimale au centre.
- Temps restant à droite du pourcentage, format `ui_format_duree`.
- Trois boutons en bas — `Pause`, `Cancel`, `E-STOP` — créés ici mais **inertes jusqu'à la tâche 9** ; les câbler tout de suite ferait passer un appel réseau par un rappel de bouton avant que la file de commandes ne soit en place.

Quand le paramètre `donnees_perimees` de `mettre_a_jour` est vrai (le chrome le calcule via `habillage_donnees_perimees()` et le propage — l'écran ne rappelle pas la façade lui-même), toutes les valeurs passent en gris `0x6B7280`. Aucune boîte d'erreur, aucun texte « déconnecté » : la barre d'état s'en charge.

- [ ] **Step 4: Capturer et REGARDER quatre images**

```bash
./simulateur/run.sh --capture build/accueil-impression.png --scenario 1 --cycles 40
./simulateur/run.sh --capture build/accueil-repos.png      --scenario 2 --cycles 3
./simulateur/run.sh --capture build/accueil-perime.png     --scenario 3 --cycles 15
./simulateur/run.sh --capture build/accueil-aberrant.png   --scenario 4 --cycles 3
```

Ouvrir les quatre. Vérifier : rien ne déborde du cadre, les nombres sont lisibles à distance, l'image « périmée » est visiblement grise, l'image « aberrant » montre `--` et non un nombre. **Joindre ces captures au rapport de tâche.** Un écran déclaré correct sans image n'est pas vérifié.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(klipper): ecran d'accueil (temperatures, progression, fichier)"
```

---

### Task 7: Clavier modal et dialogue de confirmation

**Files:**
- Create: `firmware/main/ui/widgets/clavier.c`, `clavier.h`
- Create: `firmware/main/ui/widgets/confirmation.c`, `confirmation.h`
- Create: `host-test/tests/test_clavier.c`

**Interfaces:**
- Produces:
  ```c
  /* Ouvre un clavier plein écran par-dessus tout. `rappel` est appelé une
   * seule fois : avec la valeur saisie si l'utilisateur valide, avec NULL
   * s'il annule. Le clavier se referme seul avant l'appel. */
  typedef void (*clavier_rappel_t)(const char *valeur, void *contexte);
  void clavier_ouvrir(const char *titre, const char *valeur_initiale,
                      clavier_mode_t mode, clavier_rappel_t rappel, void *contexte);

  typedef void (*confirmation_rappel_t)(bool confirme, void *contexte);
  void confirmation_ouvrir(const char *titre, const char *message,
                           const char *libelle_action, bool destructif,
                           confirmation_rappel_t rappel, void *contexte);
  ```
  `clavier_mode_t` vaut `CLAVIER_TEXTE` ou `CLAVIER_NUMERIQUE`.

**C'est le plus gros morceau partagé** (spécification §6) : adresse d'hôte, commande gcode, recherche de fichier, mot de passe WiFi. LVGL fournit `lv_keyboard` ; l'habiller en dialogue modal qui **rend une valeur** est le vrai travail, et il ne doit pas être refait deux fois.

- [ ] **Step 1: Tests**

Trois propriétés se testent sans regarder, et ce sont celles qui font mal quand elles manquent :

```c
void suite_clavier(void)
{
    printf("suite : clavier\n");
    /* 1. Le rappel est appelé exactement une fois, jamais zéro, jamais deux. */
    /* 2. Annuler rend NULL, valider rend la valeur saisie. */
    /* 3. Fermer le clavier depuis l'intérieur du rappel ne provoque pas de
     *    lecture après libération — c'est le mode d'emploi naturel (le rappel
     *    empile souvent un autre écran) et c'est là que ce genre de widget
     *    casse. Le test empile un écran depuis le rappel. */
}
```

Le troisième point s'obtient en détruisant les objets LVGL du clavier **après** l'appel du rappel, via `lv_obj_delete_async()`, jamais pendant le traitement de l'événement qui l'a déclenché. Le test le vérifie sous ASan, qui est déjà actif sur la cible `tests`.

- [ ] **Step 2: Voir échouer, implémenter**

- Le clavier occupe tout l'écran, par-dessus la barre d'état : sur 800×480 avec des doigts, un clavier partiel donne des touches trop petites.
- `lv_textarea` en haut, `lv_keyboard` en bas, mode `LV_KEYBOARD_MODE_TEXT_LOWER` ou `LV_KEYBOARD_MODE_NUMBER`.
- Les événements `LV_EVENT_READY` (validation) et `LV_EVENT_CANCEL` (annulation) de `lv_keyboard` sont les deux seules sorties.
- Un seul clavier à la fois : un second `clavier_ouvrir()` alors qu'un clavier est ouvert est ignoré et journalisé, plutôt que d'empiler deux modaux dont l'un devient inaccessible.
- Le dialogue de confirmation est un `lv_msgbox` centré ; quand `destructif` est vrai, le bouton d'action est rouge et **n'est pas** le bouton par défaut.

- [ ] **Step 3: Capturer les deux widgets, les regarder, commit**

```bash
./simulateur/run.sh --capture build/clavier.png --scenario 5 --cycles 1
./simulateur/run.sh --capture build/confirmation.png --scenario 6 --cycles 1
git commit -m "feat(ui): clavier tactile modal et dialogue de confirmation"
```

---

### Task 8: Écran de première configuration

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_configuration.c`, `.h`
- Modify: `firmware/main/app_main.c` (choix de l'écran de départ)
- Create: `host-test/tests/test_ecran_configuration.c`

**Interfaces:**
- Consumes: `reglages_configures()`, `reglages_definir_hote()`, `reglages_hote()` (jalon 2a), clavier (tâche 7).
- Produces: `const ecran_desc_t ECRAN_CONFIGURATION;`

**Cette tâche solde une dette du jalon 2a.** `reglages_definir_hote()` n'a aujourd'hui aucun appelant : la revue finale l'avait relevé comme Critical (C2), résolu provisoirement par un repli Kconfig qui oblige à recompiler pour changer d'hôte. Cet écran lui donne son appelant et rend le repli Kconfig à son rôle de repli.

- [ ] **Step 1: Tests**

Vérifier que l'écran refuse une adresse vide, accepte `192.168.1.50`, accepte `klipper.local:7125`, et que ce qu'il enregistre relit identique via `reglages_hote()` — jusqu'à la borne `BACKEND_HOTE_LONGUEUR_MAX`, la symétrie écriture/lecture ayant déjà été le sujet d'un tour de correction au jalon 2a.

Les tests hôte utilisent la NVS simulée du harnais ; si `reglages.c` n'est pas encore compilable sur PC, l'extraction pure `hote_parse.c` existe déjà et c'est elle qu'il faut tester, en laissant l'écriture NVS à la vérification sur cible.

- [ ] **Step 2: Implémenter**

- Champ « Printer address », bouton qui ouvre `clavier_ouvrir(CLAVIER_TEXTE)`, valeur pré-remplie avec `reglages_hote()` si elle existe.
- Sélecteur de type de machine : `lv_dropdown` à une seule entrée `Klipper / Moonraker` pour l'instant. Une entrée, parce que le fork astro en ajoutera une et que la spécification en fait le test décisif du modèle en fork — mais pas d'entrée factice pour « faire riche ».
- Bouton `Save` : appelle `reglages_definir_hote()`, puis `habillage_notifier("Settings saved", false)`, puis `navigation_accueil()`.
- Un hôte invalide produit une notification d'erreur et **reste sur l'écran** ; il ne ferme rien.

`app_main.c` : si `reglages_configures()` est faux, l'écran de départ est `ECRAN_CONFIGURATION`, sinon `ECRAN_ACCUEIL`. Ce choix se fait après la construction de l'habillage et **après** le démarrage du serveur web, l'ordre de démarrage restant intouchable.

- [ ] **Step 3: Capturer, regarder, commit**

```bash
git commit -m "feat(klipper): ecran de premiere configuration"
```

---

### Task 9: Les trois actions

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil.c`
- Modify: `firmware/main/apps/klipper/backend_moonraker.c` (implémenter `commande`)
- Modify: `simulateur/source_etat_sim.c` (file de commandes côté PC)
- Create: `host-test/tests/test_commandes.c`

**Interfaces:**
- Consumes: `ui_commander()` (tâche 4), `boucle_commander()` (jalon 2a), confirmation (tâche 7).
- Produces: les actions `"pause"`, `"resume"`, `"cancel"`, `"estop"` reconnues par le backend Moonraker.

- [ ] **Step 1: Tests**

Vérifier que `backend_moonraker_commande()` construit la bonne requête pour chaque action — `/printer/print/pause`, `/resume`, `/cancel`, `/printer/emergency_stop` — et qu'une action inconnue rend `ESP_ERR_INVALID_ARG` **sans** émettre de requête. La construction d'URL s'extrait en fonction pure pour être testable sans réseau, comme `hote_parse.c` l'a été au jalon 2a.

Vérifier aussi qu'une file pleine rend `ESP_ERR_NO_MEM` et que l'appelant en est informé — c'est ce qui permet à l'écran de le dire plutôt que de perdre la commande en silence.

- [ ] **Step 2: Implémenter**

- `Pause` bascule en `Resume` selon `etat_klipper_t.etat_impression` ; une seule tuile de bouton, deux libellés.
- `Cancel` et `E-STOP` passent par `confirmation_ouvrir(..., destructif = true)`.
- L'échec d'une commande remonte par `habillage_notifier("Command failed: pause", true)`. L'écran n'affiche rien lui-même.
- Après une commande acceptée, ne pas modifier l'état localement pour « faire réactif » : la prochaine interrogation dira la vérité. Un écran qui anticipe affiche du faux dès que la commande échoue.

- [ ] **Step 3: Vérifier dans le simulateur, capturer la notification d'échec, commit**

Le backend factice doit pouvoir échouer sur commande (scénario dédié) pour que ce chemin soit vu au moins une fois.

```bash
git commit -m "feat(klipper): pause/reprise, annulation et arret d'urgence"
```

---

### Task 10: Intégration sur l'ESP32

**Files:**
- Modify: `firmware/main/app_main.c`, `firmware/main/CMakeLists.txt`, `firmware/sdkconfig.defaults`
- Modify: `docs/hardware/flashing.md`

- [ ] **Step 1: Activer les polices côté ESP**

Ajouter à `firmware/sdkconfig.defaults` :

```
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_48=y
```

Puis — c'est le piège déjà payé au jalon 1 — **supprimer `firmware/sdkconfig`** et régénérer, parce que `sdkconfig.defaults` n'écrase jamais un symbole déjà présent dans un `sdkconfig` existant. `firmware/sdkconfig` contient les identifiants WiFi : les noter avant de le supprimer et les ressaisir après régénération, ou régénérer puis reconfigurer.

Vérification obligatoire dans `firmware/build/config/sdkconfig.h`, pas dans `sdkconfig.defaults` :

```powershell
Select-String -Path firmware/build/config/sdkconfig.h -Pattern "LV_FONT_MONTSERRAT_(20|28|48)"
```

Expected : trois lignes `#define ... 1`.

- [ ] **Step 2: Brancher l'interface dans `app_main`**

L'ordre de démarrage ne bouge pas : compteur de démarrages → sauvetage armé → NVS → netlog → WiFi → **serveur web** → affichage → interface. La construction de l'habillage et l'empilement du premier écran se font en dernier, et **aucune** de ces étapes n'est enveloppée dans un `ESP_ERROR_CHECK`. Si `pt_display_init()` échoue, on journalise et le firmware continue à servir `/logs` et `/revert` — c'est la seule chose qui rend une panne récupérable à distance sur un appareil sans port série.

Tout appel LVGL depuis `app_main` ou depuis la tâche d'interrogation passe par le verrou du BSP. Vérifier comment `PandaTouch_IDF` expose ce verrou et l'utiliser ; s'il n'en expose pas, construire l'interface avant de démarrer la boucle et n'appeler `habillage_pomper()` que depuis un `lv_timer`, qui s'exécute par construction sur le fil LVGL.

- [ ] **Step 3: Compiler et mesurer**

```powershell
. "<chemin-vers-esp-idf>\export.ps1"; idf.py -C firmware build
```

Relever la taille du binaire et la marge restante sur la partition applicative de 4,5 Mio. Le jalon 2a laissait 74 % libres ; les trois polices ajoutent de l'ordre de 80 Kio. Si la marge passe sous 50 %, le signaler dans le rapport.

- [ ] **Step 4: Documenter, commit**

Mettre à jour `docs/hardware/flashing.md` : ce que l'appareil affiche au premier démarrage sans configuration, et le fait que `/state` reste disponible en parallèle de l'interface.

```bash
git commit -m "feat(esp): brancher l'interface sur l'appareil"
```

> **Les étapes matérielles s'arrêtent ici tant que l'appareil est éteint.** L'installation par `/update` du firmware d'origine, la vérification visuelle et la comparaison à une vraie machine Klipper demandent la K-Touch allumée. Les consigner comme différées, jamais comme faites.

---

### Task 11: Test décisif du modèle en fork

**Files:**
- Create: `exemples/backend_jouet/backend_jouet.c`, `.h`
- Create: `exemples/backend_jouet/ecran_jouet.c`, `.h`
- Create: `exemples/backend_jouet/README.md`
- Modify: `simulateur/main.c` (option `--app jouet`)

**C'est le critère de succès n°7 de la spécification**, et le seul qui mesure vraiment si le socle est fini : écrire un backend et un écran **sans modifier une seule ligne de `core/` ni de `ui/`**. Si une modification s'avère nécessaire, ce n'est pas l'exemple qu'il faut plier, c'est le socle qui n'est pas terminé — et la modification en question est le résultat le plus utile de ce jalon.

- [ ] **Step 1: Écrire le backend jouet**

Quelques dizaines de lignes : un état à deux champs (un compteur, un libellé), `rafraichir` qui incrémente, aucune I/O.

- [ ] **Step 2: Écrire l'écran jouet**

Un titre, une valeur, un bouton qui émet une commande `"reset"`.

- [ ] **Step 3: Vérifier la contrainte, littéralement**

```bash
git diff --stat <base-de-la-tache-11>..HEAD -- firmware/main/core firmware/main/ui
```

Expected : **vide**. Toute ligne qui apparaît ici est un défaut du socle à documenter dans le rapport, avec ce qui l'a rendue nécessaire.

- [ ] **Step 4: Capturer, documenter, commit**

`exemples/backend_jouet/README.md` sert de mode d'emploi au fork astro : les trois fichiers à écrire, les deux lignes d'enregistrement, et rien d'autre.

```bash
git commit -m "docs(exemples): backend et ecran jouets, test du modele en fork"
```

---

## Ce que ce jalon ne fait pas

- **Le moteur de mise à jour par téléversement** (section 7 de la spécification) est reporté à un jalon 2c. Il est indépendant de l'interface, il porte sa propre surface de sécurité — refus par défaut d'écraser le firmware d'origine, confirmation explicite — et le mêler à onze tâches d'interface diluerait l'attention de la revue exactement là où elle compte le plus.
- Navigateur de fichiers, console gcode, macros, graphes, calibrations, gestion de plusieurs machines : hors périmètre, chacun viendra avec l'écran qui en aura besoin.
- La validation sur l'appareil : elle attend qu'il soit rallumé.
