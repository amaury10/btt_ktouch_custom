# Graphes de température + accueil interactif — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Graphe de température sur l'accueil (2 colonnes façon KlipperScreen) +
résumé interactif (nom = toggle courbe, valeur = édite consigne), alimenté par un
store d'historique séparé et un échantillonneur continu.

**Architecture:** Store `klipper_temp_historique` (tampon circulaire, hors
`etat_klipper_t`) rempli toutes les 5 s par un timer global ; l'accueil réécrit
en 2 colonnes lit ce store pour un `lv_chart` (redraw seulement à l'arrivée d'un
point) et rend chaque chauffant du résumé cliquable.

**Tech Stack:** ESP-IDF 5.5.5, LVGL 9.2.2 (`lv_chart` déjà activé), cJSON, host-test, sim.

## Global Constraints

- **RAM** : l'historique vit dans un **store statique séparé**, JAMAIS dans
  `etat_klipper_t`. JAMAIS copier le store entier (~2,2 Ko) sur une pile — API
  incrémentale (dernier point) + backfill par série (tampon `int16_t[120]` local).
- **Perf** : le `lv_chart` ne se redessine que quand `klipper_temp_historique_generation()`
  change (~5 s), pas à la cadence UI (200 ms).
- Contrat `ecran_desc_t`, contexte socle, grisage C3, ≥ 44 px, `_Static_assert`
  largeur/hauteur + clearance bandeau. ASCII/anglais à l'écran, commentaires FR.
- **Piège `*/`** : jamais de `*/` dans un commentaire C (un glob `a_*/b` casse le
  build idf, invisible host-test/sim). Relancer idf après tout commentaire touché.
- Chaque nouveau .c ajouté à `firmware/main/CMakeLists.txt`,
  `simulateur/CMakeLists.txt`, et host-test si testé.
- Flux : implémenteur host-test + sim + COMMIT ; contrôleur lance idf.py.

**Templates :** `apps/klipper/klipper_fichiers.{h,c}` (store statique + portMUX),
`apps/klipper/ecrans/ecran_menu_reglages.c` (grille de tuiles),
`apps/klipper/ecrans/ecran_temperatures.c` (parsing/bornes de consigne + clavier),
`apps/klipper/ecrans/ecran_ventilateurs.c` (clavier), l'actuel
`ecran_accueil_hub.c` (résumé + tuiles, à réécrire).

---

### Task 1 : Store `klipper_temp_historique` + tests hôte

**Files:**
- Create: `firmware/main/apps/klipper/klipper_temp_historique.{h,c}`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/CMakeLists.txt`, `host-test/CMakeLists.txt`, `host-test/tests/main.c`
- Test: `host-test/tests/test_klipper_temp_historique.c`

**Interfaces — Produces:**
```c
#include "etat_klipper.h"   /* etat_klipper_t, KLIPPER_EXTRUDEURS_MAX */
#define KLIPPER_HISTO_POINTS  120                          /* ~10 min a 1 pt/5 s */
#define KLIPPER_HISTO_SERIES  (KLIPPER_EXTRUDEURS_MAX + 1) /* 8 extrudeurs + plateau = 9 */
/* Mapping FIXE : serie i in [0, KLIPPER_EXTRUDEURS_MAX-1] = extrudeurs[i] ;
   serie KLIPPER_EXTRUDEURS_MAX = plateau. */

/* Pousse UN point par serie (temperature reelle actuelle, arrondie en int16 C),
   met a jour present[], avance la tete, incremente la generation. */
void     klipper_temp_historique_pousser(const etat_klipper_t *e);
/* Compteur monotone, +1 a chaque pousser -- l'accueil ne redessine le chart que
   quand il change. */
uint32_t klipper_temp_historique_generation(void);
/* Le chauffant de cette serie etait-il present au dernier pousser ? */
bool     klipper_temp_historique_serie_presente(uint8_t serie);
/* Dernier point d'une serie ; false si serie invalide/vide. */
bool     klipper_temp_historique_dernier(uint8_t serie, int16_t *sortie);
/* Copie les points valides d'UNE serie dans `dest` (<= KLIPPER_HISTO_POINTS),
   ordre chronologique (wraparound gere ici). Rend le nombre copie. */
size_t   klipper_temp_historique_serie(uint8_t serie, int16_t *dest, size_t max);
```

**Impl:** `static` : `int16_t g_points[KLIPPER_HISTO_SERIES][KLIPPER_HISTO_POINTS];`
`uint16_t g_tete; uint16_t g_nb; uint32_t g_gen; bool g_present[KLIPPER_HISTO_SERIES];`
+ portMUX (`#ifdef ESP_PLATFORM`, no-op host/sim — copier le patron exact de
`klipper_fichiers.c`). Section critique = écritures de scalaires uniquement.
`pousser` : pour i<KLIPPER_EXTRUDEURS_MAX, `g_present[i]=e->extrudeurs[i].presente`,
`g_points[i][g_tete]=(int16_t)lroundf(e->extrudeurs[i].actuelle)` ; série lit =
plateau. `g_tete=(g_tete+1)%POINTS ; if(g_nb<POINTS) g_nb++ ; g_gen++`.
`serie(idx,dest,max)` : déroule les `g_nb` points du plus ancien au plus récent.

- [ ] **Step 1 — Tests d'abord.** test_klipper_temp_historique.c : (a) après N
  `pousser` avec un état {2 extrudeurs présents, plateau}, `dernier` rend la
  dernière valeur ; `serie` rend N points (ou 120 si N>120) en ordre chrono ;
  (b) wraparound : pousser 130 fois des valeurs 0..129, `serie(0,...)` rend 120
  points = 10..129 dans l'ordre ; (c) `serie_presente` reflète `presente` ;
  (d) `generation` incrémente à chaque pousser ; (e) série absente → `dernier`
  false, `serie` rend 0.
- [ ] **Step 2 — Lancer, voir échouer.**
- [ ] **Step 3 — Implémenter** le store (patron klipper_fichiers.c).
- [ ] **Step 4** host-test + build sim verts.
- [ ] **Step 5** Commit `feat(klipper): store d'historique de temperature (tampon circulaire)`.

---

### Task 2 : Échantillonneur continu (timer 5 s)

**Files:**
- Modify: `firmware/main/app_main.c`
- Test: (intégration — voir Step 4 ; pas de test hôte unitaire pour le timer app_main)

**Interfaces — Consumes:** `boucle_etat_copier(void*, size_t)` (core/boucle.h — copie
l'état courant sous verrou, même accès que `habillage_pomper`), `boucle_generation()`,
`klipper_temp_historique_pousser` (Task 1).

Ajouter dans app_main.c, à côté du minuteur d'interface (200 ms, créé sous le
`PT_LVGL_SCOPE_LOCK`), un **second `lv_timer` à 5000 ms** dont le rappel :
```c
static void echantillon_temp_cb(lv_timer_t *t) {
    (void)t;
    etat_klipper_t e;                         /* ~1840 o sur la pile LVGL,
                                                 meme budget que habillage_pomper */
    if (boucle_etat_copier(&e, sizeof(e))) {
        klipper_temp_historique_pousser(&e);
    }
}
```
Créé une seule fois (comme `minuteur_interface`) ; échec de création journalisé,
jamais fatal (même politique). Tourne en continu sur le fil LVGL,
indépendamment de l'écran affiché.

- [ ] **Step 1** Implémenter le rappel + la création du timer (inclure
  `klipper_temp_historique.h`).
- [ ] **Step 2** `idf.py build` (côté contrôleur) + build sim.
- [ ] **Step 3** Validation intégration : au sim, laisser tourner et vérifier
  (via un log ponctuel ou l'écran de la Task 3) que `generation()` croît toutes
  les ~5 s. Documenter dans le rapport que ce câblage n'a pas de test hôte
  unitaire (timer app_main), couvert par les tests de `pousser` (T1) + le sim.
- [ ] **Step 4** Commit `feat(klipper): echantillonneur continu 5 s de l'historique temp`.

---

### Task 3 : Accueil 2 colonnes + graphe (réécriture ECRAN_ACCUEIL_HUB)

**Files:**
- Modify (réécriture): `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.{h,c}`
- Test: `host-test/tests/test_ecran_accueil_hub.c` (réécrire)

**Interfaces:** garde `ECRAN_ACCUEIL_HUB` / id `"accueil_hub"` / titre `"Home"`
(app_main.c/rail/chooser inchangés). **Consumes:** `klipper_temp_historique_*`
(T1), `ECRAN_HOMING/TEMPERATURES/ACTIONS/MENU_REGLAGES/FICHIERS`.

**Layout 2 colonnes (742×436) :**
- **Gauche (~360 px)** : lignes chauffants (chaque = **label NOM** + **label
  VALEUR** distincts et adjacents — créés ici, rendus cliquables en T4/T5),
  bornées à N lignes visibles ; puis position `X:.. Y:.. Z::` + outil, vitesse/
  flux, mini-progression si impression ; puis le **`lv_chart`** (~250 px).
- **Droite (~360 px)** : 5 tuiles empilées (Homing/Temperature/Actions/
  Configuration/Print), `navigation_empiler`, ≥ 44 px.
- `_Static_assert` : les deux colonnes remplissent la largeur ; le bas de chaque
  colonne reste au-dessus du bandeau (coords absolues, idiome existant).

**Chart :**
- `construire` : `lv_chart_create`, `lv_chart_set_point_count(chart, KLIPPER_HISTO_POINTS)`,
  mode `LV_CHART_UPDATE_MODE_SHIFT`, une `lv_chart_series_t*` par chauffant
  **présent** (couleur distincte/série ; stockées dans le ctx). Backfill série
  par série : `int16_t tampon[KLIPPER_HISTO_POINTS];`
  `size_t n = klipper_temp_historique_serie(i, tampon, KLIPPER_HISTO_POINTS);`
  puis `lv_chart_set_next_value` en boucle (JAMAIS de copie du store entier).
- `mettre_a_jour` : garde une `uint32_t derniere_gen` dans le ctx ; **si**
  `klipper_temp_historique_generation() != derniere_gen`, append le dernier
  point de chaque série (`klipper_temp_historique_dernier` → `lv_chart_set_next_value`),
  `lv_chart_refresh`, et mémorise la génération. Sinon, ne touche PAS le chart.
  Les **valeurs texte** du résumé (temps/position/vitesse/flux) se rafraîchissent
  à chaque appel + grisage C3 sur `donnees_perimees`.

Retirer l'ancien layout 1-colonne (résumé 4 lignes + grille 5). Les labels
NOM/VALEUR ne sont PAS encore cliquables ici (T4/T5).

- [ ] **Step 1** Réécrire le test : seeder le store (pousser un état connu),
  construire, vérifier : 5 tuiles empilent le bon écran ; le chart a une série
  par chauffant présent ; les valeurs du résumé ; grisage C3 ; que
  `mettre_a_jour` sans nouvelle génération ne change pas le nombre de points.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Réécrire ecran_accueil_hub.{h,c}.
- [ ] **Step 4** host-test + sim verts ; `--ecran` accueil montre 2 colonnes +
  courbe qui se remplit (avec l'échantillonneur T2).
- [ ] **Step 5** Commit `feat(ihm): accueil 2 colonnes avec graphe de temperature`.

---

### Task 4 : Raccourci VALEUR → consigne

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.{h,c}`
- Test: `host-test/tests/test_ecran_accueil_hub.c` (ajouts)

**Interfaces — Consumes:** `klipper_gcode_consigne_temp` (klipper_gcode.h),
`clavier_ouvrir` (clavier.h). Réutiliser le parsing/bornes de `ecran_temperatures.c`.

Rendre chaque **label VALEUR** cliquable (`LV_OBJ_FLAG_CLICKABLE`, c'est un
`lv_label`/`lv_obj` nu — poser le flag). Au clic : `clavier_ouvrir("Nozzle target"/
"Bed target", prérempli avec la consigne actuelle, CLAVIER_NUMERIQUE, rappel, ctx)`.
Le rappel : `strtol` + bornes EXACTES (extrudeur [0,350], plateau selon la borne
de ecran_temperatures.c — reprendre la même constante) ; hors bornes →
`habillage_notifier(...)`, aucun gcode ; dans les bornes →
`klipper_gcode_consigne_temp(sortie, taille, nom_chauffeur, cible)` puis
`envoyer_gcode`. `nom_chauffeur` = `"extruder"`/`"extruder<i>"`/`"heater_bed"`
(même construction que ecran_temperatures.c). Sous-structure d'info par
chauffant dans le ctx (indice → nom + type), comme les preset_infos existants.

- [ ] **Step 1** Test : cliquer la valeur du chauffant T0 ouvre le clavier ; une
  saisie valide (ex. 200) émet `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200` ;
  la valeur du plateau émet `HEATER=heater_bed` ; une saisie hors bornes n'émet rien.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter.
- [ ] **Step 4** host-test + sim verts.
- [ ] **Step 5** Commit `feat(ihm): raccourci accueil -- taper la valeur edite la consigne`.

---

### Task 5 : Raccourci NOM → afficher/masquer la courbe

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.{h,c}`
- Test: `host-test/tests/test_ecran_accueil_hub.c` (ajouts)

Rendre chaque **label NOM** cliquable. État de visibilité par série dans le ctx
(`bool serie_visible[KLIPPER_HISTO_SERIES]`, tout à `true` au départ). Au clic
sur le nom du chauffant `i` : basculer `serie_visible[i]`, appliquer
`lv_chart_hide_series(chart, series[i], !serie_visible[i])`, et **griser le
label NOM** (couleur grise) quand masqué / couleur normale quand visible.

- [ ] **Step 1** Test : cliquer le nom T0 masque sa série
  (`lv_chart_get_series_next`/l'état hidden, ou vérifier via le flag stocké dans
  le ctx + un getter de test) et grise le label ; recliquer réaffiche + dégrise.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter.
- [ ] **Step 4** host-test + sim verts.
- [ ] **Step 5** Commit `feat(ihm): raccourci accueil -- taper le nom affiche/masque la courbe`.

---

## Self-review (couverture spec)

- Store séparé + API incrémentale (pas de copie pile) : T1. ✅
- Échantillonneur continu 5 s (hors accueil) : T2. ✅
- Accueil 2 colonnes + chart (redraw sur génération seulement) : T3. ✅
- Raccourci valeur → consigne (gcode + bornes réutilisés) : T4. ✅
- Raccourci nom → toggle courbe + grisage : T5. ✅
- Symbole `ECRAN_ACCUEIL_HUB` conservé (boot/rail/chooser) : T3. ✅
- Pas d'ajout à `etat_klipper_t` ; `lv_chart` déjà activé. ✅
- Noms cohérents : `klipper_temp_historique_*`, `KLIPPER_HISTO_POINTS/SERIES`. ✅
