# Refonte IHM alignée KlipperScreen — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Reprendre le découpage/navigation KlipperScreen : rail avec vrai
bouton Back, accueil `main_panel` (résumé compact + grille Homing/Temperature/
Actions/Configuration/Print), sous-menu Actions, panneau Homing — en
réutilisant les écrans existants.

**Architecture:** Habillage (rail + barre + bandeau) inchangé sauf le rail et
la géométrie du bandeau. Chaque écran reste un `ecran_desc_t`. Les nouveaux
écrans (Actions, Homing) ne font que du câblage `navigation_empiler` /
`envoyer_gcode` vers de l'existant. L'accueil est réécrit ; tout le reste est
réutilisé.

**Tech Stack:** ESP-IDF 5.5.5, LVGL 9.2.2, cJSON, host-test, simulateur.

## Global Constraints

- **Réutilisation** : Move/Extrude/Fan/Temperature/Macros, les 5 panneaux de
  réglage et les 6 stubs restent tels quels. On ne crée que ECRAN_ACTIONS et
  ECRAN_HOMING, et on réécrit l'accueil (ECRAN_ACCUEIL_HUB).
- **Pas d'ajout à `etat_klipper_t`** : l'accueil lit des champs existants.
- Textes ASCII/anglais + `LV_SYMBOL_*` uniquement (pas d'icône bitmap ce lot).
  Commentaires français.
- Contrat `ecran_desc_t`, contexte socle, grisage C3, cibles ≥ 44 px,
  `_Static_assert` largeur + clearance bandeau.
- Chaque nouvel écran ajouté à `firmware/main/CMakeLists.txt`,
  `simulateur/CMakeLists.txt` et `simulateur/main.c` (`--ecran <nom>`).
- Flux build : l'implémenteur lance host-test (`wsl -d Debian -- "/mnt/e/Dev/BTT KTouch Custom/host-test/run.sh"`)
  + build sim (compile-only), COMMIT ; le contrôleur lance `idf.py build`.
- **Hors périmètre** : icônes bitmap ; graphe de température. (Le rond jaune
  bas-gauche est diagnostiqué — repère de coin de la mire — et corrigé dans la
  Task 1 via la colonne de rail opaque.)

**Templates de référence :** `ui/widgets/rail.c`, `apps/klipper/rail_actions.c`,
`ui/habillage.c` (bandeau), `apps/klipper/ecrans/ecran_menu_reglages.c` (grille
de menu), `apps/klipper/ecrans/ecran_accueil_hub.c` (accueil actuel + zone
température), `apps/klipper/ecrans/ecran_deplacer.c` (formatage position).

---

### Task 1 : Rail — bouton Back, STOP en bas, bandeau dégage le rail, colonne opaque (règle le rond jaune)

**Files:**
- Modify: `firmware/main/ui/widgets/rail.h`, `firmware/main/ui/widgets/rail.c`
- Modify: `firmware/main/apps/klipper/rail_actions.c`
- Modify: `firmware/main/ui/habillage.c`
- Test: `host-test/tests/test_rail.c`, `host-test/tests/test_rail_actions.c` (existants — mettre à jour)

**Bug rond jaune (diagnostiqué) :** le repère de coin bas-gauche de la mire de
`build_test_pattern()` (app_main.c, carré 24×24 `0xFFFF00`) transparaît par le
bas de la colonne du rail, restée transparente (`lv_obj_remove_style_all`) et
vide depuis que STOP est monté en haut. Correctif retenu : **rendre la colonne
du rail OPAQUE** (fond `COULEUR_FOND`), ce qui couvre le repère sans toucher la
mire (le repli dégradé « mire visible si l'UI échoue » reste préservé dans la
zone de contenu). Ne PAS modifier `build_test_pattern()`.

**Interfaces — Produces:** `rail_action_t` devient
`{ RAIL_BACK, RAIL_ACCUEIL, RAIL_MACROS, RAIL_STOP }`, `RAIL_NB = 4`
(`RAIL_HOME` supprimé).

- [ ] **Step 1 — MAJ tests.** Dans test_rail.c : le clic sur le bouton
  d'indice `RAIL_BACK` déclenche `sur_action(RAIL_BACK, ...)`. Dans
  test_rail_actions.c : `rail_action_klipper(RAIL_BACK, NULL)` appelle
  `navigation_depiler()` (via le faux de navigation existant) ; retirer le cas
  `RAIL_HOME`.
- [ ] **Step 2 — Lancer, voir échouer** (RAIL_BACK/RAIL_HOME).
- [ ] **Step 3 — rail.h/rail.c.**
  - rail.h : `typedef enum { RAIL_BACK, RAIL_ACCUEIL, RAIL_MACROS, RAIL_STOP, RAIL_NB } rail_action_t;` (mettre à jour le commentaire : Back = retour d'un niveau, plus de homing dans le rail).
  - rail.c : `LIBELLES = { "Back", "Accueil", "Macros", "STOP" }` ;
    `ICONES = { LV_SYMBOL_LEFT, LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_STOP }`.
  - Placement STOP : **au lieu de `lv_obj_move_to_index(STOP, 0)`**, sortir STOP
    du flux flex (`lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT)`) et l'ancrer
    en bas : `lv_obj_align(r->boutons[RAIL_STOP], LV_ALIGN_BOTTOM_MID, 0, 0)`,
    largeur explicite `LV_PCT(100)`. Les trois autres restent dans le flex
    column START (en haut). Retirer l'ancien `move_to_index`.
  - **Colonne opaque (fix rond jaune)** : après `lv_obj_remove_style_all(r->racine)`,
    réaffirmer un fond opaque : `lv_obj_set_style_bg_color(r->racine, lv_color_hex(0x10161D), 0);`
    `lv_obj_set_style_bg_opa(r->racine, LV_OPA_COVER, 0);` (même sombre que
    `COULEUR_FOND` des écrans — définir la constante en tête de rail.c). Vérifier
    que `r->racine` s'étend bien jusqu'au bas de l'écran (sa taille est forcée
    par habillage.c à `HAUTEUR_ECRAN - BARRE_HAUTEUR` ; si elle ne descend pas
    jusqu'à y=480, l'ajuster pour couvrir le coin bas-gauche où vit le repère).
- [ ] **Step 4 — rail_actions.c.** Remplacer `case RAIL_HOME` par
  `case RAIL_BACK: navigation_depiler(); break;`. Retirer l'include/gcode de
  homing devenu inutile ici s'il n'est plus référencé.
- [ ] **Step 5 — habillage.c `construire_bandeau`.** Le bandeau ne doit plus
  couvrir le rail :
  ```c
  lv_obj_set_size(g_bandeau, LARGEUR_ECRAN - RAIL_LARGEUR, BANDEAU_HAUTEUR);
  lv_obj_set_pos(g_bandeau, RAIL_LARGEUR, HAUTEUR_ECRAN - BANDEAU_HAUTEUR);
  ```
  (Les `_Static_assert` de clearance des écrans restent valides : le bandeau
  couvre toujours le bas de la zone de contenu à x ≥ 58 ; seul le rail est
  désormais dégagé.)
- [ ] **Step 6** — host-test vert (suite entière 0 échec) + build sim ;
  vérifier au sim que STOP est en bas, Back en haut. Commit
  `feat(ihm): rail Back + STOP en bas, le bandeau degage le rail`.

---

### Task 2 : Sous-menu Actions (ECRAN_ACTIONS)

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_actions.{h,c}`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/CMakeLists.txt`, `simulateur/main.c` (`--ecran actions`)
- Test: `host-test/tests/test_ecran_actions.c`

**Interfaces — Produces:** `extern const ecran_desc_t ECRAN_ACTIONS;` id `"actions"`, titre `"Actions"`.
**Consumes:** `ECRAN_DEPLACER`, `ECRAN_EXTRUDER`, `ECRAN_VENTILATEURS`,
`ECRAN_TEMPERATURES`, `ECRAN_MACROS`, `ECRAN_CONSOLE` ; `klipper_gcode_niveau_lit`.

Grille de boutons texte (idiome exact de `ecran_menu_reglages.c`), 7 cases :

| Libellé | Action |
|---|---|
| Move | `navigation_empiler(&ECRAN_DEPLACER)` |
| Extrude | `navigation_empiler(&ECRAN_EXTRUDER)` |
| Fan | `navigation_empiler(&ECRAN_VENTILATEURS)` |
| Temperature | `navigation_empiler(&ECRAN_TEMPERATURES)` |
| Macros | `navigation_empiler(&ECRAN_MACROS)` |
| Disable Motors | `klipper_gcode_niveau_lit(KLIPPER_LIT_DISABLE)` → `envoyer_gcode` (M84, envoi direct — KlipperScreen le fait sans confirmation) |
| Console | `navigation_empiler(&ECRAN_CONSOLE)` |

`mettre_a_jour = NULL`, `detruire = NULL`. Grille (ex. 3×3, 7 cases occupées),
`_Static_assert` largeur + clearance bandeau comme `ecran_menu_reglages.c`.

- [ ] **Step 1** Test : construire ne plante pas ; clic « Move » empile un
  écran (faux de navigation) ; clic « Disable Motors » émet `M84`.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter (copier l'ossature de ecran_menu_reglages.c ;
  reprendre l'idiome `construire_arguments_gcode`/`envoyer_gcode` pour le M84).
- [ ] **Step 4** host-test + sim verts ; `--ecran actions`.
- [ ] **Step 5** Commit `feat(ihm): sous-menu Actions (Move/Extrude/Fan/Temp/Macros/Disable/Console)`.

---

### Task 3 : Panneau Homing (ECRAN_HOMING)

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_homing.{h,c}`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/CMakeLists.txt`, `simulateur/main.c` (`--ecran homing`)
- Test: `host-test/tests/test_ecran_homing.c`

**Interfaces — Produces:** `extern const ecran_desc_t ECRAN_HOMING;` id `"homing"`, titre `"Homing"`.
**Consumes:** `klipper_gcode_home` (existant : masque bit0=X bit1=Y bit2=Z, 0 = tout).

4 boutons texte (grille 2×2, idiome `ecran_niveau_lit.c`) :

| Libellé | gcode |
|---|---|
| Home All | `klipper_gcode_home(masque=0)` → `G28` |
| Home X | `klipper_gcode_home(0b001)` → `G28 X` |
| Home Y | `klipper_gcode_home(0b010)` → `G28 Y` |
| Home Z | `klipper_gcode_home(0b100)` → `G28 Z` |

Envoi direct (homing non destructif, même politique que l'ancien bouton Home
du rail). `mettre_a_jour = NULL`, `detruire = NULL`. `_Static_assert` largeur +
clearance.

- [ ] **Step 1** Test : clic « Home X » émet `G28 X` ; « Home All » émet `G28`.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter.
- [ ] **Step 4** Builds verts ; `--ecran homing`.
- [ ] **Step 5** Commit `feat(ihm): panneau Homing (all/X/Y/Z)`.

---

### Task 4 : Accueil main_panel (réécriture de ECRAN_ACCUEIL_HUB)

**Files:**
- Modify (réécriture): `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.{h,c}`
- Test: `host-test/tests/test_ecran_accueil_hub.c` (réécrire)

**Interfaces:** garde le symbole `ECRAN_ACCUEIL_HUB` et son id (`"accueil_hub"`)
pour ne PAS toucher `app_main.c` (boot) ni la registration rail
(`id_accueil = ECRAN_ACCUEIL_HUB.id`) ni le chooser habillage. Seul le CONTENU
change. titre reste `"Home"`.
**Consumes:** `ECRAN_HOMING` (T3), `ECRAN_TEMPERATURES`, `ECRAN_ACTIONS` (T2),
`ECRAN_MENU_REGLAGES`, `ECRAN_FICHIERS`.

**Contenu :**
1. **Zone résumé** (haut), tout en lecture, grisée C3 sur `donnees_perimees` :
   - températures compactes : chaque extrudeur présent (`T0..`) + `Bed`,
     `actuelle/consigne` via `ui_format_temperature` (réutiliser la géométrie
     par palier de l'actuel ecran_accueil_hub — `klipper_paliers.h` — mais en
     version RÉDUITE : une seule ligne compacte, pas la grande zone) ;
   - ligne position `X:.. Y:.. Z:..` (« -- » si axe non référencé, réutiliser
     `formater_axe` de ecran_deplacer.c) + outil actif `T<outil_actif>` ;
   - vitesse `vitesse_pct` % / flux `flux_pct` % ;
   - si `impression_en_cours` : mini-progression (`progression` 0..1, réutiliser
     `progression_t` ou un simple label `NN%`).
   - (état liaison déjà montré par la pastille de la barre — ne pas redoubler.)
2. **Grille de menu 5 tuiles** (remplace la grille 6 actuelle) :

| Tuile | `navigation_empiler` |
|---|---|
| Homing | `&ECRAN_HOMING` |
| Temperature | `&ECRAN_TEMPERATURES` |
| Actions | `&ECRAN_ACTIONS` |
| Configuration | `&ECRAN_MENU_REGLAGES` |
| Print | `&ECRAN_FICHIERS` |

Retirer l'ancienne enum de menu 6 cases et les tuiles de température cliquables
(le résumé n'est plus cliquable ; Temperature passe par sa tuile).
`_Static_assert` : la grille 5 + la zone résumé tiennent dans 436 px et
au-dessus du bandeau.

- [ ] **Step 1** Réécrire le test : avec un état {2 extrudeurs, plateau,
  position homée, impression en cours 42 %}, vérifier le résumé (labels temp,
  `X:.. Y:.. Z:..`, `42%`) et que les 5 tuiles empilent le bon écran ; grisage
  C3.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Réécrire ecran_accueil_hub.{h,c} (contexte : labels du résumé
  + 5 boutons de menu ; supprimer l'ancien pool de tuiles cliquables et l'enum
  MENU 6).
- [ ] **Step 4** host-test + sim verts ; `--ecran` par défaut (accueil) montre
  le main_panel ; parcours Home → chaque tuile.
- [ ] **Step 5** Commit `feat(ihm): accueil main_panel (resume compact + grille Homing/Temp/Actions/Config/Print)`.

---

### Task 5 : Fine Tune redescend dans le flux d'impression

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_menu_reglages.c` (retirer la case Fine Tune)
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil.{h,c}` (écran de statut d'impression : ajouter l'entrée Fine Tune)
- Test: `host-test/tests/test_ecran_menu_reglages.c` (MAJ : 11 cases), test de ecran_accueil (ajout)

**Interfaces — Consumes:** `ECRAN_REGLAGE_FIN`.

- Dans `ecran_menu_reglages.c` : retirer la case « Fine Tune » et son include ;
  la grille passe de 12 à 11 entrées (ajuster géométrie + `_Static_assert`).
- Dans `ecran_accueil.c` (statut d'impression) : ajouter un bouton/entrée
  « Fine Tune » → `navigation_empiler(&ECRAN_REGLAGE_FIN)`, visible pendant
  l'impression (là où KlipperScreen le place, Job Status). Suivre la
  disposition existante de cet écran (boutons pause/reprendre/annuler/urgence).

- [ ] **Step 1** Tests : menu_reglages n'a plus « Fine Tune » (11 cases,
  aucune n'empile ECRAN_REGLAGE_FIN) ; ecran_accueil a une entrée qui empile
  ECRAN_REGLAGE_FIN.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter.
- [ ] **Step 4** Builds verts.
- [ ] **Step 5** Commit `feat(ihm): Fine Tune dans le flux d'impression (comme KlipperScreen)`.

---

## Self-review (couverture spec)

- Rail Back + STOP bas + bandeau dégage rail : T1. ✅
- Accueil main_panel résumé + grille 5 : T4. ✅
- Sous-menu Actions : T2. ✅ Panneau Homing : T3. ✅
- Fine Tune → flux impression : T5. ✅
- Réutilisation (aucun panneau réécrit hors accueil) : T2/T3/T5 ne font que du
  câblage. ✅
- Cohérence noms : `ECRAN_ACTIONS`, `ECRAN_HOMING`, symbole `ECRAN_ACCUEIL_HUB`
  conservé, `RAIL_BACK`/`RAIL_NB=4`. ✅
- **Rond jaune** : couvert par T1 (colonne de rail opaque couvre le repère de
  coin bas-gauche de la mire de build_test_pattern).
- Pas d'ajout à `etat_klipper_t` : l'accueil ne lit que de l'existant. ✅
