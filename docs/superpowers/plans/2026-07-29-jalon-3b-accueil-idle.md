# Jalon 3b — Accueil Idle — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Quand rien n'imprime, l'écran d'accueil devient un poste de pilotage — état complet, jog XY/Z, homing, températures manuelles par chauffeur, macros — avec les trois paliers d'affichage des outils (1 / 2-4 / 5-8 têtes) pour qu'un mono-extrudeur ne soit jamais cantonné à une petite cellule.

**Architecture:** Un nouvel `ECRAN_ACCUEIL_IDLE` distinct de l'accueil impression du 2b ; le socle choisit lequel empiler AU DÉMARRAGE selon `impression_en_cours` (la bascule automatique en cours de session est différée — voir la note en fin de plan, elle n'est pas atteignable dans cette tranche puisque le lancement d'impression arrive au 3d). Les commandes (jog, home, consigne de température) sont construites par des **fonctions gcode pures compilables sur PC** et envoyées par une action générique `BACKEND_ACTION_GCODE` que le backend Moonraker relaie tel quel à `printer.gcode.script` — même chemin que les macros, testé au 3a. Le choix du palier d'affichage est une **fonction pure** partagée.

**Tech Stack:** ESP-IDF v5.5.5 · LVGL 9.2.2 · cJSON · harnais hôte WSL (ASan/UBSan) · simulateur LVGL/SDL avec `--hote` vers un vrai Moonraker (vkp).

## Global Constraints

- **`apps/klipper/ecrans/` et `apps/klipper/*.c` compilent sur PC** : aucune inclusion de `freertos/*`, `esp_wifi.h`, `esp_http_client.h`, `nvs.h`, `driver/*` — seul `esp_err.h` est toléré ; `lvgl.h` autorisé dans les écrans. Les fichiers ESP-only (WS, HTTP) restent isolés (`moonraker_ws.c`, `backend_moonraker.c`) et exclus des builds PC dans les trois CMakeLists.
- **Un écran ne parle jamais au réseau et ne bloque jamais.** Il construit une chaîne gcode (logique métier, pas du réseau) et la remet à `ui_commander()`, qui rend la main immédiatement.
- **`etat_klipper_t` reste un POD à taille fixe, sans pointeur.** Cette tranche N'AJOUTE AUCUN champ à l'état (tout ce dont l'accueil idle a besoin existe déjà : `extrudeurs[]`, `plateau`, `nb_extrudeurs`, `outil_actif`, `position[3]`, `axes_references`, `deplacement_absolu`, `macros`).
- **Le contrat `backend_desc_t` ne change pas de forme incompatible.** L'unique ajout backend est l'action `BACKEND_ACTION_GCODE` (une chaîne de plus), reconnue par le factice et par Moonraker — le jouet du 2b compile inchangé (critère 8, vérifié mécaniquement à la fin).
- **Sécurité (spec §7), non renégociable :** le jog et le homing sont **désactivés** hors ligne (`donnees_perimees`) ET quand l'axe concerné n'est pas référencé (`axes_references`) ; le homing d'un axe déjà référencé qui va bouger passe par le dialogue de confirmation ; jamais de mise à jour optimiste (l'état affiché ne suit que l'état poussé, jamais une anticipation locale) ; boutons désactivés en style RÉSOLU (leçon de la revue finale 2b — vérifier la couleur résolue, pas le drapeau).
- **Trois paliers d'affichage des outils** choisis sur `nb_extrudeurs` par un helper partagé (jamais recalculé à la main) : 1 tête = grandes tuiles ; 2-4 = grille moyenne, valeur en 28, outil actif marqué ; 5-8 = grille compacte, valeur en 20, tap ⇒ vue détaillée. Une capture par palier exigée en revue.
- **`core/` sans FreeRTOS/ESP-IDF hors `esp_err.h`, sans LVGL** — inchangé.
- Invariants hérités : NVS partagée jamais effacée, aucun `ESP_ERROR_CHECK` nouveau, ordre de boot intact, pas d'identifiants ni de chemins locaux dans les fichiers suivis, commentaires français, textes UI anglais (ASCII — Montserrat ne couvre que 0x20-0x7F), `-Wall -Wextra -Werror`.
- Harnais : `VERIFIER(condition)` un argument, libellés en commentaire. Suite actuelle : **1836 vérifications, 0 échec** — chaque tâche finit plus haut et vert. Commande : `wsl -d Debian -- "<racine-du-depot>/host-test/run.sh"` (PowerShell, jamais Git Bash).
- Build ESP : `. "<chemin-vers-esp-idf>\export.ps1"; idf.py -C firmware build` (même invocation) ; le chemin ESP-IDF réel ne s'écrit jamais dans un fichier suivi.
- Simulateur : `--hote localhost:7125` branche un vrai Moonraker (vkp, redémarrable par `docker compose up -d` dans `~/virtual-klipper-printer`) ; `--scenario N` le backend factice (0/10 = repos mono, 11 = U1 4 têtes, 12 = 8 têtes). Captures : `--capture <abs.png>`, **ouvertes et regardées** — un PNG que personne n'a ouvert ne prouve rien (les bugs de mise en page de ce projet se sont tous cachés jusqu'à ce que quelqu'un regarde).

## Une note sur la forme de ce plan

Comme aux tranches précédentes : contrats d'en-tête et tests des fonctions PURES (gcode, palier) donnés en entier — c'est là que vivent les défauts et c'est ce qu'un plan a intérêt à figer. Le code de MISE EN PAGE LVGL est spécifié par contraintes précises (dimensions, polices, ordre des opérations, pièges nommés) plutôt que recopié ligne à ligne : l'implémenteur a le compilateur et une capture PNG sous les yeux à chaque étape, pas moi au moment d'écrire.

## Leçons des tranches précédentes qui s'appliquent ici

- Boutons désactivés : enregistrer un style local `LV_STATE_DISABLED` DÉDIÉ (bg + texte), basculer l'état sur le bouton ET son label, et tester la couleur RÉSOLUE (`lv_obj_get_style_bg_color`), pas `lv_obj_has_state` — voir `ecran_accueil.c` (revue finale 2b).
- Un écran fraîchement empilé est peuplé par `habillage_pomper()` dès le pomper suivant même si la génération n'a pas bougé (correctif nav en direct, `habillage.c`) — pas besoin d'y penser, c'est acquis.
- `lv_label` avec un glyphe court (`<`, chiffre) : `lv_obj_center(label)` centre bien ; pas de `LV_LABEL_LONG_DOT` sur un libellé d'un caractère.
- Mise en page : tout chevauchement possible entre deux constantes de position devient un `_Static_assert`, pas un pixel qui sort du cadre.
- La cible `lvgl` est compilée sans sanitizers : un test mémoire discrimine par un effet observable (longueur de libellé lue), jamais par l'attente d'un rapport ASan émis par LVGL.
- RED réel exigé à chaque test nouveau (mutation quand le RED naturel n'existe pas).

---

### Task 1: Branche + couche de commande gcode pure + relais `BACKEND_ACTION_GCODE`

**Files:**
- Create: `firmware/main/apps/klipper/klipper_gcode.c`, `klipper_gcode.h`
- Create: `host-test/tests/test_klipper_gcode.c`
- Modify: `firmware/main/core/backend.h` (constante d'action)
- Modify: `firmware/main/apps/klipper/backend_moonraker.c` (relais)
- Modify: `firmware/main/core/backend_factice.c` (acceptation)
- Modify: `firmware/main/CMakeLists.txt`, `host-test/CMakeLists.txt`, `simulateur/CMakeLists.txt`, `host-test/tests/main.c`

**Interfaces:**
- Consumes: `etat_klipper.h` (masque `axes_references`).
- Produces (consommé par les tâches 4, 5, 6) :

```c
/* klipper_gcode.h — construit des scripts gcode Klipper, EN FONCTIONS PURES
 * (aucun réseau, aucune allocation) : c'est la logique métier « quel gcode
 * pour cette action UI », testable entièrement sur PC. Un écran les appelle
 * puis remet le résultat à ui_commander(BACKEND_ACTION_GCODE, {"script":...}).
 * Chaque fonction rend false SANS toucher `sortie` si un argument est
 * invalide ou si le tampon est trop court (jamais de troncature silencieuse
 * rendue comme un succès — leçon du 2b). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Longueur de tampon suffisante pour tout script produit ici (le plus long
 * est le jog avec SAVE/RESTORE_GCODE_STATE, ~110 octets). */
#define KLIPPER_GCODE_MAX 160

/* Déplacement relatif d'UN axe, borné par SAVE/RESTORE_GCODE_STATE pour ne
 * jamais laisser la machine en mode relatif ni changer sa vitesse courante :
 *   SAVE_GCODE_STATE NAME=ktouch_jog
 *   G91
 *   G1 <axe><distance signée> F<vitesse>
 *   RESTORE_GCODE_STATE NAME=ktouch_jog
 * `axe` ∈ {'X','Y','Z'} (majuscule) ; `distance_mm` non nul et fini, borné à
 * ±1000 mm ; `vitesse_mm_min` ∈ [1, 60000]. La distance est formatée avec au
 * plus 2 décimales, sans zéros de fin superflus. */
bool klipper_gcode_jog(char *sortie, size_t taille,
                       char axe, float distance_mm, uint16_t vitesse_mm_min);

/* Référencement. `axes_masque` reprend la convention de
 * etat_klipper_t::axes_references (bit0=X bit1=Y bit2=Z). 0 OU 0b111 ⇒ "G28"
 * (tout) ; sinon "G28" suivi des seuls axes demandés dans l'ordre X Y Z
 * (ex. bit0|bit2 ⇒ "G28 X Z"). Un bit hors des 3 de poids faible est ignoré. */
bool klipper_gcode_home(char *sortie, size_t taille, uint8_t axes_masque);

/* Consigne de température d'un chauffeur nommé, via la commande Klipper
 * générique (fonctionne pour extrudeurs ET plateau, contrairement à
 * M104/M140) :
 *   SET_HEATER_TEMPERATURE HEATER=<chauffeur> TARGET=<cible_c>
 * `chauffeur` non NULL, non vide, ≤ 32 octets, uniquement [A-Za-z0-9_] (nom
 * d'objet Klipper — un caractère hors de ce jeu ⇒ false, jamais injecté dans
 * le gcode). `cible_c` ∈ [0, 350] (0 = éteindre). */
bool klipper_gcode_consigne_temp(char *sortie, size_t taille,
                                 const char *chauffeur, uint16_t cible_c);
```

Et dans `core/backend.h`, à côté des actions existantes :

```c
/* Jalon 3b : script gcode arbitraire construit par un écran (klipper_gcode.c)
 * et relayé tel quel à printer.gcode.script. arguments_json = {"script":"..."}.
 * Distinct de BACKEND_ACTION_MACRO (qui porte {"nom":...}) : ici l'appelant a
 * déjà construit le gcode complet, le backend ne fait que le transmettre. */
#define BACKEND_ACTION_GCODE "gcode"
```

- [ ] **Step 1 : Créer la branche**

```bash
git checkout -b jalon-3b-accueil-idle jalon-3a-transport-ws
```

- [ ] **Step 2 : Écrire les tests des trois builders d'abord**

`host-test/tests/test_klipper_gcode.c` :

```c
#include "petit_test.h"
#include "klipper_gcode.h"

void suite_klipper_gcode(void)
{
    printf("suite : klipper_gcode\n");
    char g[KLIPPER_GCODE_MAX];

    /* --- jog --- */
    /* déplacement positif d'un entier : pas de décimale superflue */
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 10.0f, 3000) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_jog\nG91\nG1 X10 F3000\nRESTORE_GCODE_STATE NAME=ktouch_jog");
    /* déplacement négatif fractionnaire : signe et décimales, sans zéros de fin */
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'Z', -0.1f, 600) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_jog\nG91\nG1 Z-0.1 F600\nRESTORE_GCODE_STATE NAME=ktouch_jog");
    /* axe invalide, distance nulle, non finie, hors borne, vitesse nulle : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'A', 10.0f, 3000) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 0.0f, 3000) == false);
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 2000.0f, 3000) == false); /* > 1000 */
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 10.0f, 0) == false);
    /* tampon trop court : false */
    char court[8];
    VERIFIER(klipper_gcode_jog(court, sizeof(court), 'X', 10.0f, 3000) == false);

    /* --- home --- */
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0) == true);        VERIFIER_TEXTE(g, "G28");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x07) == true);     VERIFIER_TEXTE(g, "G28"); /* tout */
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x01) == true);     VERIFIER_TEXTE(g, "G28 X");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x05) == true);     VERIFIER_TEXTE(g, "G28 X Z");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x02) == true);     VERIFIER_TEXTE(g, "G28 Y");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0xF8) == true);     VERIFIER_TEXTE(g, "G28"); /* bits hauts ignorés = aucun des 3 ⇒ tout */

    /* --- consigne température --- */
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "extruder", 210) == true);
    VERIFIER_TEXTE(g, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210");
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "heater_bed", 0) == true);
    VERIFIER_TEXTE(g, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0");
    /* nom vide, NULL, avec caractère injectant, trop long, cible hors borne : false */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "", 210) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), NULL, 210) == false);
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "extruder\nM112", 210) == false); /* injection */
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "extruder", 400) == false);       /* > 350 */
}
```

Le cas `home(0xF8)` fige un choix : un masque dont AUCUN des trois bits de
poids faible n'est levé équivaut à « tout référencer » (`G28`). Documenter ce
choix dans `klipper_gcode.h`.

- [ ] **Step 3 : Lancer, voir échouer** — `klipper_gcode.h` introuvable, puis
  chaque `VERIFIER_TEXTE` en échec. RED réel, consigné.

- [ ] **Step 4 : Implémenter `klipper_gcode.c`.** Pièges nommés :
  - le formatage de la distance : `snprintf("%.2f")` puis retirer les zéros de
    fin ET le point isolé (`10.00`→`10`, `-0.10`→`-0.1`), sinon les
    `VERIFIER_TEXTE` échouent ; une petite fonction locale `formater_mm()`.
  - `%.2f` sur cible : `CONFIG_LIBC_NEWLIB_NANO_FORMAT` retire `%f` — mais ce
    fichier n'utilise `%f` que pour la distance de jog ; vérifier dans
    `firmware/build/config/sdkconfig.h` que le symbole est absent/0 (il l'était
    au 2b) ; s'il est à 1, le signaler et formater la distance sans `%f` (entier
    + fraction en centièmes calculés à la main). NE PAS deviner : vérifier.
  - validation du nom de chauffeur : boucle sur chaque octet ∈ `[A-Za-z0-9_]`,
    tout autre ⇒ false (c'est la barrière anti-injection ; un `\n` ou `M112`
    dans le nom ne doit jamais atteindre le gcode).

- [ ] **Step 5 : Relais backend + acceptation factice.**
  - `backend_moonraker.c` : `BACKEND_ACTION_GCODE` lit `{"script":"..."}` de
    `arguments_json` (même extraction cJSON bornée que `{"nom":...}` pour les
    macros, un helper `extraire_champ_script()` calqué sur l'existant) et
    l'envoie à `printer.gcode.script` (WS en ligne, POST HTTP sinon) —
    exactement le chemin macro, avec `"script"` déjà construit au lieu du nom.
  - `backend_factice.c` : `BACKEND_ACTION_GCODE` ⇒ `ESP_OK` (le simulateur doit
    pouvoir « exécuter » un jog sans erreur) ; arguments NULL ⇒
    `ESP_ERR_NOT_SUPPORTED`. Aucun effet sur l'état factice (un vrai jog
    changerait la position, mais le factice n'a pas à la simuler pour ce
    jalon — un commentaire le dit).

- [ ] **Step 6 : Vert + build ESP + commit**

```bash
git commit -m "feat(klipper): couche de commande gcode pure (jog/home/temp) + relais BACKEND_ACTION_GCODE"
```

---

### Task 2: Helper de palier d'affichage des outils (fonction pure)

**Files:**
- Create: `firmware/main/apps/klipper/klipper_paliers.c`, `klipper_paliers.h`
- Create: `host-test/tests/test_klipper_paliers.c`
- Modify: les trois CMakeLists + `host-test/tests/main.c`

**Interfaces:**
- Produces (consommé par les tâches 3 et 6) :

```c
/* klipper_paliers.h — CHOIX PUR du palier d'affichage des chauffeurs en
 * fonction du nombre d'extrudeurs présents (spec §6). Extrait ici, une seule
 * fois, pour que l'accueil idle, l'accueil impression (jalon suivant) et le
 * panneau filament (3e) affichent tous les outils de la même façon au même
 * nombre de têtes — jamais recalculé à la main écran par écran. */
#pragma once
#include <stdint.h>

typedef enum {
    PALIER_MONO = 0,   /* 1 tête : grandes tuiles (police 48) */
    PALIER_MOYEN,      /* 2-4 têtes : grille 2x2, police 28, outil actif marqué */
    PALIER_COMPACT,    /* 5-8 têtes : grille 2x4, police 20, tap => détail */
} palier_outils_t;

/* 0 ou 1 => MONO ; 2..4 => MOYEN ; >=5 => COMPACT. Une machine sans extrudeur
 * annoncé (0) retombe sur MONO plutôt que sur une grille vide. */
palier_outils_t palier_outils(uint8_t nb_extrudeurs);

/* Nombre de colonnes de la grille de chauffeurs pour un palier donné :
 * MONO=1 (une tuile), MOYEN=2, COMPACT=2. (Le nombre de lignes se déduit du
 * nombre de chauffeurs présents et des colonnes, côté écran.) */
uint8_t palier_colonnes(palier_outils_t palier);

/* Police LVGL (taille en points) de la VALEUR de température pour un palier :
 * MONO=48, MOYEN=28, COMPACT=20. Rendue en int (pas un lv_font_t*, pour que
 * ce fichier reste sans dépendance LVGL et testable sur PC) ; l'écran
 * convertit en &lv_font_montserrat_<n>. */
uint8_t palier_taille_police(palier_outils_t palier);
```

- [ ] **Step 1 : Tests d'abord**

```c
#include "petit_test.h"
#include "klipper_paliers.h"

void suite_klipper_paliers(void)
{
    printf("suite : klipper_paliers\n");
    /* bornes exactes du choix */
    VERIFIER(palier_outils(0) == PALIER_MONO);
    VERIFIER(palier_outils(1) == PALIER_MONO);
    VERIFIER(palier_outils(2) == PALIER_MOYEN);
    VERIFIER(palier_outils(4) == PALIER_MOYEN);
    VERIFIER(palier_outils(5) == PALIER_COMPACT);
    VERIFIER(palier_outils(8) == PALIER_COMPACT);
    /* géométrie et police cohérentes avec la spec §6 */
    VERIFIER(palier_colonnes(PALIER_MONO) == 1);
    VERIFIER(palier_colonnes(PALIER_MOYEN) == 2);
    VERIFIER(palier_colonnes(PALIER_COMPACT) == 2);
    VERIFIER(palier_taille_police(PALIER_MONO) == 48);
    VERIFIER(palier_taille_police(PALIER_MOYEN) == 28);
    VERIFIER(palier_taille_police(PALIER_COMPACT) == 20);
}
```

- [ ] **Step 2 : RED, implémenter, vert.**
- [ ] **Step 3 : Commit** `feat(klipper): helper pur de palier d'affichage des outils (1 / 2-4 / 5-8)`

---

### Task 3: `ECRAN_ACCUEIL_IDLE` — affichage de l'état complet + choix au démarrage

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.c`, `ecran_accueil_idle.h`
- Create: `firmware/main/apps/klipper/accueil_choix.c`, `accueil_choix.h` (helper pur : quel écran d'accueil)
- Create: `host-test/tests/test_ecran_accueil_idle.c`, `host-test/tests/test_accueil_choix.c`
- Modify: `firmware/main/app_main.c` (choix au démarrage), `simulateur/main.c` (idem sur `--scenario`/`--hote`)
- Modify: les trois CMakeLists + `host-test/tests/main.c`

**Interfaces:**
- Consumes: `etat_klipper_t`, `klipper_paliers.h`, tuile/labels LVGL, navigation, habillage.
- Produces:

```c
/* ecran_accueil_idle.h */
extern const ecran_desc_t ECRAN_ACCUEIL_IDLE;

/* accueil_choix.h — helper PUR : quel écran d'accueil pour un état donné.
 * true ⇒ l'accueil impression (ECRAN_ACCUEIL, jalon 2b) ; false ⇒ l'accueil
 * idle (ECRAN_ACCUEIL_IDLE). Le socle (app_main, simulateur) appelle ceci
 * AU DÉMARRAGE pour empiler le bon écran de fond. */
#pragma once
#include <stdbool.h>
#include "etat_klipper.h"
bool accueil_impression_actif(const etat_klipper_t *etat);
```

**Contenu de l'accueil idle** (zone 800×436 sous la barre d'état) :
- **Cellules de température** en haut, disposées selon `palier_outils(nb_extrudeurs)` : chaque chauffeur présent (extrudeurs puis plateau) montre son nom court (`T0`…`T7`, `Bed`), sa valeur actuelle (police du palier) et sa consigne dessous en 20, via `ui_format_temperature()`. L'**outil actif** (`outil_actif`) est marqué (bordure ou fond distinct). Au palier COMPACT, la valeur en 20 et pas de consigne inline (le tap→détail viendra en tâche 6).
- **Ligne d'état machine** : position `X:… Y:… Z:…` (1 décimale, `--` si l'axe n'est pas référencé — `axes_references`), et l'outil actif nommé.
- **Zone de contrôles** (remplie aux tâches 4/5) : un conteneur réservé pour le pad de jog et le homing ; en tâche 3 il reste vide (placeholder visible « Controls »), pour que la mise en page soit posée et capturée avant d'y accrocher les widgets.
- **Rangée de boutons bas** : `Macros` (tâche 7) — en tâche 3, un placeholder.
- Grisage intégral sur `donnees_perimees` (cellules, position, contrôles) — style RÉSOLU.

- [ ] **Step 1 : Tests d'abord** — `test_accueil_choix.c` :

```c
#include "petit_test.h"
#include "accueil_choix.h"
#include <string.h>

void suite_accueil_choix(void)
{
    printf("suite : accueil_choix\n");
    etat_klipper_t e;
    memset(&e, 0, sizeof(e));
    /* repos : impression pas en cours ⇒ accueil idle */
    e.impression_en_cours = false;
    VERIFIER(accueil_impression_actif(&e) == false);
    /* impression en cours ⇒ accueil impression */
    e.impression_en_cours = true;
    VERIFIER(accueil_impression_actif(&e) == true);
    /* pause = impression en cours (juste suspendue) ⇒ impression */
    e.impression_en_pause = true;
    VERIFIER(accueil_impression_actif(&e) == true);
}
```

  `test_ecran_accueil_idle.c` : construire l'écran sur l'afficheur hôte, driver
  `mettre_a_jour` avec des états des trois paliers (mono / 4 têtes / 8 têtes) et
  vérifier, par lecture des libellés (`lv_label_get_text`), que :
  - le bon nombre de cellules de température est présent (1+1 mono, 4+1 U1,
    8+1 huit-têtes) ;
  - une température aberrante (999) rend `--` (formateur inchangé) ;
  - un axe non référencé rend `--` sur sa position (masque `axes_references`
    à 0 ⇒ `X:--`), un axe référencé rend la valeur ;
  - l'outil actif marqué correspond à `outil_actif` ;
  - grisage `donnees_perimees` true⇒gris / false⇒normal (style RÉSOLU,
    `lv_color_eq`, aller-retour) sur au moins une cellule.

- [ ] **Step 2 : RED, implémenter accueil_choix puis l'écran.** Poser la mise en
  page par palier via `klipper_paliers`. Pièges : conteneurs de grille sans
  scrollbar/padding de thème (`lv_obj_remove_style_all` + neutraliser, motif de
  `habillage.c`/`tuile.c`) ; `_Static_assert` sur les sommes de largeur/hauteur
  de chaque palier (aucune cellule ne déborde de 800×436, aucun chevauchement
  avec la zone de contrôles ni la rangée basse).

- [ ] **Step 3 : Câbler le choix au démarrage.**
  - `app_main.c` : là où `ECRAN_ACCUEIL` est empilé aujourd'hui (voir le bloc
    tâche 8/10 du 2b, sous `PT_LVGL_SCOPE_LOCK`), empiler
    `accueil_impression_actif(&etat)` ? `ECRAN_ACCUEIL` : `ECRAN_ACCUEIL_IDLE` —
    mais l'état n'est pas encore connu au tout premier démarrage (la boucle
    vient de démarrer). Règle : empiler `ECRAN_ACCUEIL_IDLE` par défaut (repos
    est l'état de démarrage le plus courant et le plus sûr — aucun contrôle
    dangereux hors ligne), l'ordre de boot restant intact et additif. La
    bascule vivante idle↔impression est différée (voir fin de plan).
  - `simulateur/main.c` : sur `--scenario`/`--hote`, empiler l'accueil selon
    `accueil_impression_actif()` évalué après le premier cycle (le simulateur,
    lui, connaît l'état factice/réel dès le départ). Un `--scenario 1`
    (impression) doit donc démarrer sur l'accueil impression, un `--scenario 0`
    sur l'accueil idle — ce qui prouve les DEUX écrans et le helper de choix.

- [ ] **Step 4 : Captures — un par palier.**

```
--scenario 10 --capture <abs>/idle-mono.png --cycles 3     # CR-10, 1 tête
--scenario 11 --capture <abs>/idle-u1.png   --cycles 3     # U1, 4 têtes
--scenario 12 --capture <abs>/idle-8t.png   --cycles 3     # 8 têtes
```

  Ouvrir les trois. Vérifier : le mono a de GRANDES tuiles (pas une petite
  cellule perdue), le 4-têtes une grille 2×2 lisible avec l'outil actif marqué,
  le 8-têtes une grille 2×4 compacte sans débordement ; la ligne de position
  correcte ; placeholders contrôles/macros présents. **Joindre les trois au
  rapport.**

- [ ] **Step 5 : Vert + build ESP + commit**

```bash
git commit -m "feat(klipper): ecran d'accueil idle -- etat complet, trois paliers d'outils"
```

---

### Task 4: Jog XY/Z + sélecteur de pas

**Files:**
- Create: `firmware/main/ui/widgets/selecteur_pas.c`, `selecteur_pas.h`
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.c` (pad de jog dans la zone de contrôles)
- Create: `host-test/tests/test_selecteur_pas.c`; étendre `test_ecran_accueil_idle.c`
- Modify: CMakeLists concernés

**Interfaces:**
- Consumes: `klipper_gcode.h` (jog), `selecteur_pas.h`, `ui_commander`, `axes_references`.
- Produces:

```c
/* selecteur_pas.h — widget « 0.1 / 1 / 10 / 100 mm » : quatre boutons
 * mutuellement exclusifs, un seul actif. Le pas courant est lu par l'écran au
 * moment d'un appui sur le pad de jog. Structure à champs publics vivant dans
 * le contexte de l'écran (jamais alloué par le widget), même forme que
 * tuile_t/progression_t. */
#pragma once
#include "lvgl.h"
typedef struct {
    lv_obj_t *racine;
    lv_obj_t *boutons[4];
    uint8_t   index_actif;    /* 0..3 */
} selecteur_pas_t;

/* Pas en mm indexés : {0.1, 1, 10, 100}. */
extern const float SELECTEUR_PAS_MM[4];

void  selecteur_pas_creer(selecteur_pas_t *s, lv_obj_t *parent);
float selecteur_pas_valeur(const selecteur_pas_t *s);  /* SELECTEUR_PAS_MM[index_actif] */
```

**Mise en page du pad de jog** (dans la zone de contrôles de l'accueil idle) :
- un pad XY : 4 boutons directionnels (X-, X+, Y-, Y+) autour d'un centre, plus
  une colonne Z (Z+, Z-) à côté ;
- le sélecteur de pas 0.1/1/10/100 en dessous ;
- un appui construit le gcode via `klipper_gcode_jog(axe, ±pas, VITESSE)` où
  `VITESSE` = 3000 mm/min pour XY, 600 pour Z (constantes nommées), et envoie
  `ui_commander(BACKEND_ACTION_GCODE, {"script":"<gcode>"})` — le même
  assemblage `{"script":...}` que l'écran macros pour son nom, via un helper
  local `envoyer_gcode(const char*)` de l'écran.
- **Désactivation (spec §7)** : un bouton d'axe est désactivé (style RÉSOLU) si
  `donnees_perimees` OU si l'axe n'est pas référencé (`axes_references`). Z-
  reste permis même non homé ? NON : désactiver tout axe non référencé, sans
  exception — un jog sur un axe non homé est refusé par Klipper de toute façon
  et l'UI ne doit pas mentir. Réévalué à chaque `mettre_a_jour`.

- [ ] **Step 1 : Tests du sélecteur d'abord** (host) : création, `index_actif`
  par défaut = 1 (soit 1 mm, un pas raisonnable) ; un `lv_obj_send_event`
  CLICKED sur `boutons[2]` met `index_actif=2` et `selecteur_pas_valeur()`=10 ;
  un seul bouton porte l'état actif à la fois (les autres reviennent à l'état
  normal — vérifier par style résolu). RED, implémenter, vert.
- [ ] **Step 2 : Tests d'intégration jog** (étendre `test_ecran_accueil_idle.c`) :
  avec un état où X et Y sont référencés mais pas Z, un clic sur X+ envoie
  (trace du seam sim) un `BACKEND_ACTION_GCODE` dont le script contient
  `G1 X<pas> F3000` ; les boutons Z sont désactivés (style résolu) ; passer
  `donnees_perimees=true` désactive TOUS les boutons de jog ; le pas choisi
  change la distance (sélecteur à 10 ⇒ `X10`). RED, implémenter, vert.
- [ ] **Step 3 : Capture** — `--scenario 10 --capture <abs>/idle-jog.png --cycles 3`
  (mono, machine référencée dans le factice ? sinon un scénario où X/Y/Z sont
  référencés — au besoin ajouter au factice un `axes_references=0x07` sur le
  scénario 10). Ouvrir : pad lisible, sélecteur de pas avec 1 mm actif, boutons
  d'un axe non homé visiblement grisés. Décrire.
- [ ] **Step 4 : Vert + build ESP + commit** `feat(klipper): jog XY/Z avec selecteur de pas sur l'accueil idle`

---

### Task 5: Homing (par axe + global, confirmation)

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.c` (boutons de homing)
- Étendre `host-test/tests/test_ecran_accueil_idle.c`

**Interfaces:**
- Consumes: `klipper_gcode.h` (home), `confirmation.h` (dialogue), `ui_commander`.

**Mise en page** : une rangée de boutons de homing dans la zone de contrôles :
`Home All`, `Home X`, `Home Y`, `Home Z`. Un appui construit
`klipper_gcode_home(masque)` et envoie via `envoyer_gcode()`.
- **Confirmation (spec §7)** : si l'axe (ou un axe de « All ») est DÉJÀ
  référencé — donc un `G28` va le faire bouger depuis une position connue —
  ouvrir `confirmation_ouvrir_ex(...)` (« Home X? / The axis will move to its
  endstop. » action « Home », refus « Cancel ») avant d'envoyer. Si aucun axe
  concerné n'est référencé (rien ne bouge d'une position « connue »), envoyer
  directement. « Home All » quand au moins un axe est référencé ⇒ confirmation.
- Les boutons de homing NE sont PAS désactivés par « axe non référencé » (homer
  est précisément ce qu'on fait quand ce n'est pas référencé) ; ils sont
  désactivés seulement sur `donnees_perimees`.

- [ ] **Step 1 : Tests d'abord** : (a) machine non référencée (`axes_references=0`),
  clic « Home X » ⇒ pas de dialogue, `BACKEND_ACTION_GCODE` avec `G28 X` envoyé
  directement ; (b) X déjà référencé, clic « Home X » ⇒ un dialogue de
  confirmation s'ouvre (aucun gcode encore envoyé) ; confirmer ⇒ `G28 X` envoyé,
  décliner ⇒ rien ; (c) `donnees_perimees` ⇒ boutons de homing désactivés
  (style résolu) ; (d) « Home All » avec Y référencé ⇒ confirmation, puis `G28`.
  Piloter le dialogue avec `lv_obj_send_event` (technique de `test_clavier.c`/
  `test_commandes.c`). RED, implémenter, vert.
- [ ] **Step 2 : Capture** — un scénario avec un axe référencé, clic Home X, la
  confirmation ouverte : `--scenario ... --capture <abs>/idle-home-confirm.png`.
  Ouvrir, décrire (dialogue centré, fond assombri, action non focalisée par
  défaut).
- [ ] **Step 3 : Vert + build ESP + commit** `feat(klipper): homing par axe et global avec confirmation`

---

### Task 6: Températures manuelles (clavier numérique + préréglages)

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.c` (tap sur une cellule)
- Étendre `host-test/tests/test_ecran_accueil_idle.c`

**Interfaces:**
- Consumes: `klipper_gcode.h` (consigne_temp), `clavier.h` (`CLAVIER_NUMERIQUE`),
  `ui_commander`.

**Comportement** :
- Un tap sur une cellule de température ouvre
  `clavier_ouvrir("Nozzle target" / "Bed target", "<consigne courante>",
  CLAVIER_NUMERIQUE, rappel, contexte)`. Le rappel : valeur NULL (annulé) ⇒
  rien ; sinon parser en entier, **borner à [0, 350]** (une saisie hors borne
  ou non numérique ⇒ notification d'erreur `habillage_notifier(..., true)` et
  aucun envoi — jamais de consigne aberrante envoyée à la machine), puis
  `klipper_gcode_consigne_temp("<chauffeur>", cible)` où `<chauffeur>` est
  `extruder`/`extruderN`/`heater_bed` selon la cellule tapée, et envoyer.
- **Préréglages** : sous la zone de température (ou dans le clavier — au plus
  simple : une rangée de boutons `PLA 210/60`, `PETG 240/80`, `ABS 250/100`,
  `Off`) qui envoient directement la paire buse+plateau sans passer par le
  clavier. Constantes nommées `PREREGLAGE_PLA_BUSE 210`, etc. `Off` ⇒ consignes
  0 sur buse active + plateau.
- Au palier COMPACT (5-8 têtes), un tap sur une cellule ouvre d'abord la **vue
  détaillée** du chauffeur (spec §6 : la cellule n'a plus la place d'un réglage
  direct) — pour cette tranche, la « vue détaillée » minimale EST le clavier
  numérique ouvert sur ce chauffeur (pas un sous-écran séparé) ; documenter que
  la vue détaillée riche (graphe, etc.) n'est pas de ce jalon.

- [ ] **Step 1 : Tests d'abord** : (a) tap sur la cellule buse ouvre le clavier
  numérique (un objet clavier apparaît sur `lv_layer_top()`), prérempli avec la
  consigne courante ; (b) valider « 210 » ⇒ `BACKEND_ACTION_GCODE` avec
  `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210` (trace du seam) ;
  (c) valider « 999 » ⇒ notification d'erreur, AUCUN gcode envoyé ; (d) valider
  une saisie non numérique ⇒ idem ; (e) annuler ⇒ rien ; (f) bouton préréglage
  `PLA` ⇒ deux gcodes (buse 210, plateau 60) ou un gcode combiné — figer le
  choix et le tester ; (g) `Off` ⇒ consignes 0. RED, implémenter, vert.
- [ ] **Step 2 : Captures** — le clavier numérique ouvert sur une cellule
  (`--capture <abs>/idle-temp-clavier.png`) et la rangée de préréglages
  (`idle-prereglages.png`). Ouvrir, décrire.
- [ ] **Step 3 : Vert + build ESP + commit** `feat(klipper): consignes de temperature manuelles et prereglages`

---

### Task 7: Macros sur l'accueil idle + intégration + critère 8

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.c` (bouton Macros, bouton Imprimer absent)
- Modify: `simulateur/main.c` (scénarios idle accessibles), `docs/hardware/flashing.md`
- Étendre `test_ecran_accueil_idle.c`

- [ ] **Step 1 : Bouton Macros** — visible si `nb_macros > 0`, ⇒
  `navigation_empiler(&ECRAN_MACROS)` (l'écran existant du 3a). **Le bouton
  Imprimer reste ABSENT** : le navigateur de fichiers arrive au 3d ; ne jamais
  poser un bouton mort. Le documenter en commentaire dans l'écran. Test :
  `nb_macros>0` ⇒ bouton présent et un clic empile ECRAN_MACROS (profondeur
  passe à 2) ; `nb_macros==0` ⇒ bouton caché ; pas de bouton « Print » du tout.
- [ ] **Step 2 : Vérification du critère 8** :

```bash
git diff --stat jalon-2b-simulateur..HEAD -- exemples/   # attendu : VIDE
wsl -d Debian -- bash "<racine-du-depot>/simulateur/run.sh" --app jouet --capture <abs>/jouet-3b.png --cycles 5
```
  Ouvrir la capture jouet : identique au 2b. Le diff `exemples/` vide prouve
  que l'accueil idle n'a pas touché le modèle en fork.
- [ ] **Step 3 : Capture live contre vkp** — `--hote localhost:7125` (redémarrer
  vkp si besoin), naviguer sur l'accueil idle d'une vraie machine :
  `idle-vkp.png`. Ouvrir, décrire (températures réelles, position réelle,
  boutons de contrôle, bouton Macros menant à la vraie liste). C'est le
  pilotage local d'une vraie machine au repos — l'aboutissement de la tranche.
- [ ] **Step 4 : Doc + build ESP + suite** — `flashing.md` : mentionner que
  l'appareil démarre sur l'accueil idle (contrôles) quand rien n'imprime.
  Suite hôte verte, build ESP vert.
- [ ] **Step 5 : Commit** `feat(klipper): macros et integration de l'accueil idle`

---

## Ce que cette tranche ne fait pas

- **La bascule vivante accueil idle ↔ accueil impression** en cours de session
  (quand une impression démarre/s'arrête pendant que l'appareil tourne). Le
  socle choisit l'accueil AU DÉMARRAGE (§Task 3) ; la bascule dynamique
  demanderait au socle de piloter la navigation depuis l'état, ce que le 2b a
  délibérément évité — à concevoir proprement (probablement un remplacement de
  l'écran de fond de pile quand l'utilisateur est à la profondeur 1). De toute
  façon inatteignable dans cette tranche : on ne peut PAS lancer d'impression
  depuis l'accueil idle tant que le navigateur de fichiers (3d) n'existe pas ;
  seule une commande externe (Mainsail, console) fait basculer l'état, et gérer
  ce cas gracieusement est un confort, pas le livrable de 3b. Consigné pour 3c
  ou une tâche dédiée.
- **La vue détaillée riche d'un chauffeur** au palier compact (graphe temporel,
  historique) — le tap ouvre le clavier numérique, rien de plus (§Task 6).
- **Les macros à paramètres** (invite depuis les `params` par défaut) — fin de
  3b dans la spec, reportées : cette tranche livre le lancement simple, déjà en
  place au 3a.
- **Le bouton Imprimer / navigateur de fichiers** (3d) — bouton absent, jamais
  mort.
- **Vitesse/flux/babystep** — écran d'impression (3c), pas l'accueil idle.
