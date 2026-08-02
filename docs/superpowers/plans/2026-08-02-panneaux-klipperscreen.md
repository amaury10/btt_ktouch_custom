# Panneaux KlipperScreen — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Compléter le catalogue d'écrans façon KlipperScreen (Fine Tune, Z
Calibrate, Bed Level, Limits, Retraction, stubs) et regrouper les réglages
sous un sous-menu Configuration, en réutilisant les patterns existants.

**Architecture:** Chaque panneau = un `ecran_desc_t` autonome (contexte
socle, gcode via klipper_gcode.c + envoyer_gcode). Un sous-menu
ECRAN_CONFIGURATION (grille texte, idiome de ecran_accueil_hub.c) relie le
tout ; la case « Reglages » du hub y mène. Deux panneaux (Limits, Retraction)
ajoutent quelques `float` relus à `etat_klipper_t` + abonnement Moonraker.

**Tech Stack:** ESP-IDF 5.5.5, LVGL 9.2.2, cJSON, host-test (CMake/PC),
simulateur, vkp (Moonraker simulé localhost:7125).

## Global Constraints

- Ne PAS ajouter de gros tableau à `etat_klipper_t` (crash pile/RAM déjà vu —
  klipper_fichiers.h). Limits/Retraction n'ajoutent que des `float` scalaires.
- Tout texte AFFICHÉ est ASCII/anglais (police Montserrat). Commentaires en français.
- Contrat `ecran_desc_t` : contexte par instance alloué par le socle, jamais
  de statique de fichier ; grisage C3 sur `donnees_perimees`, jamais d'erreur.
- Gcode : fonction pure bornée dans klipper_gcode.c (rend false sans toucher
  `sortie` si invalide/tampon court) puis envoyer_gcode (idiome
  construire_arguments_gcode recopié par écran, cf. ecran_ventilateurs.c).
- Cibles tactiles ≥ 44 px ; `_Static_assert` que rien ne passe sous le
  bandeau (BARRE_HAUTEUR_ECRAN + bas_zone <= BANDEAU_Y_ECRAN).
- Chaque nouvel écran est ajouté au registre de navigation ET au flag
  `--ecran` du simulateur (simulateur/main.c) pour test isolé.
- Fin de chaque tâche : host-test + `idf.py build` (firmware/) + build
  simulateur verts.

**Templates de référence (à lire, pas à copier aveuglément) :**
- Écran slider + presets + clavier : `ecran_ventilateurs.c`.
- Écran grille de menu : `ecran_accueil_hub.c`.
- Écran sélecteur de pas + jog : `ecran_deplacer.c`.
- Fonctions gcode + tests : `klipper_gcode.{h,c}`, `host-test/tests/test_klipper_gcode.c`.
- Abonnement/parse Moonraker : `moonraker_ws.c`, `moonraker_rpc.{h,c}`.

---

### Task 1: Fonctions gcode pures (klipper_gcode.{h,c} + host-test)

**Files:**
- Modify: `firmware/main/apps/klipper/klipper_gcode.h`
- Modify: `firmware/main/apps/klipper/klipper_gcode.c`
- Test: `firmware/host-test/tests/test_klipper_gcode.c` (ajouts)

**Interfaces — Produces** (signatures exactes que les écrans consommeront) :

```c
/* M220 S<pct> — vitesse d'impression. pct in [1,300]. */
bool klipper_gcode_vitesse_impression(char *sortie, size_t taille, uint16_t pct);

/* M221 S<pct> — flux (extrusion). pct in [1,300]. */
bool klipper_gcode_flux(char *sortie, size_t taille, uint16_t pct);

/* SET_GCODE_OFFSET. reset=true -> "SET_GCODE_OFFSET Z=0 MOVE=1" ;
   sinon "SET_GCODE_OFFSET Z_ADJUST=<delta_mm> MOVE=1". delta_um in [-2000,2000],
   non nul si !reset. Formaté mm, <=3 décimales, sans zéros de fin. */
bool klipper_gcode_offset_z(char *sortie, size_t taille, int32_t delta_um, bool reset);

typedef enum { KLIPPER_ZCAL_PROBE, KLIPPER_ZCAL_ENDSTOP,
               KLIPPER_ZCAL_ACCEPT, KLIPPER_ZCAL_ABORT } klipper_zcal_action_t;
/* PROBE_CALIBRATE / Z_ENDSTOP_CALIBRATE / ACCEPT / ABORT. */
bool klipper_gcode_calibration_z(char *sortie, size_t taille, klipper_zcal_action_t action);

/* TESTZ Z=<±delta_mm>. delta_um in [-5000,5000], non nul. Signe explicite
   ('+' ou '-'), formaté mm <=3 décimales. */
bool klipper_gcode_testz(char *sortie, size_t taille, int32_t delta_um);

typedef enum { KLIPPER_LIT_SCREWS, KLIPPER_LIT_ZTILT,
               KLIPPER_LIT_QGL, KLIPPER_LIT_DISABLE } klipper_lit_action_t;
/* SCREWS_TILT_CALCULATE / Z_TILT_ADJUST / QUAD_GANTRY_LEVEL / M84. */
bool klipper_gcode_niveau_lit(char *sortie, size_t taille, klipper_lit_action_t action);

typedef enum { KLIPPER_LIM_VELOCITY, KLIPPER_LIM_ACCEL,
               KLIPPER_LIM_SQV, KLIPPER_LIM_ACCEL_TO_DECEL } klipper_lim_champ_t;
/* SET_VELOCITY_LIMIT VELOCITY=|ACCEL=|SQUARE_CORNER_VELOCITY=|ACCEL_TO_DECEL=<v>.
   v entier in [1, 100000]. */
bool klipper_gcode_limite_vitesse(char *sortie, size_t taille, klipper_lim_champ_t champ, uint32_t valeur);

typedef enum { KLIPPER_RETR_LENGTH, KLIPPER_RETR_SPEED,
               KLIPPER_RETR_EXTRA, KLIPPER_RETR_UNRETRACT_SPEED } klipper_retr_champ_t;
/* SET_RETRACTION RETRACT_LENGTH=|RETRACT_SPEED=|UNRETRACT_EXTRA_LENGTH=|UNRETRACT_SPEED=<v>.
   valeur_um in [0, 20000] pour les longueurs ; pour les vitesses, valeur_um
   est réinterprété en mm/s entier in [1,1000] (voir note d'impl). Formaté mm. */
bool klipper_gcode_retraction(char *sortie, size_t taille, klipper_retr_champ_t champ, uint32_t valeur_um);
```

- Consumes : `KLIPPER_GCODE_MAX` (déjà défini, 160 — suffit ; réaffirmer via
  buffer local dans les tests). Réutilise le style de bornage de
  `klipper_gcode_ventilateur`/`klipper_gcode_jog`.

**Note d'impl :** le formatage mm depuis µm/millièmes existe déjà pour le jog
(voir `klipper_gcode_jog`/`klipper_gcode_extrude` : format ≤2 décimales sans
zéros de fin). Réutiliser le même helper interne s'il existe, sinon en écrire
un local `formater_mm(char*, size_t, int32_t milli, unsigned decimales)`.
Pour la rétraction, garder deux domaines clairs : longueurs en µm→mm ;
vitesses `_SPEED` en mm/s entier (la valeur passée est alors des mm/s, pas
des µm — documenter dans le .h et **découper en deux fonctions si l'ambiguïté
gêne** : `klipper_gcode_retraction_longueur(champ,µm)` +
`klipper_gcode_retraction_vitesse(champ,mm_s)`. L'implémenteur tranche et
documente.)

- [ ] **Step 1 — Tests d'abord.** Dans test_klipper_gcode.c, ajouter des cas
  borne-exacte pour chaque fonction, sur le modèle des tests existants :
  - `vitesse_impression(100)` → `"M220 S100"` ; `(0)`/`(301)` → false, buffer intact ;
  - `flux(100)` → `"M221 S100"` ; `(0)`/`(301)` → false ;
  - `offset_z(50,false)` → `"SET_GCODE_OFFSET Z_ADJUST=0.05 MOVE=1"` ;
    `offset_z(-10,false)` → `"...Z_ADJUST=-0.01 MOVE=1"` ; `(0,true)` →
    `"SET_GCODE_OFFSET Z=0 MOVE=1"` ; `(0,false)` → false ; `(2001,false)` → false ;
  - `calibration_z(KLIPPER_ZCAL_PROBE)` → `"PROBE_CALIBRATE"` ; ENDSTOP →
    `"Z_ENDSTOP_CALIBRATE"` ; ACCEPT → `"ACCEPT"` ; ABORT → `"ABORT"` ;
  - `testz(50)` → `"TESTZ Z=+0.05"` ; `testz(-50)` → `"TESTZ Z=-0.05"` ;
    `(0)` → false ; `(5001)` → false ;
  - `niveau_lit(KLIPPER_LIT_SCREWS)` → `"SCREWS_TILT_CALCULATE"` ; ZTILT →
    `"Z_TILT_ADJUST"` ; QGL → `"QUAD_GANTRY_LEVEL"` ; DISABLE → `"M84"` ;
  - `limite_vitesse(KLIPPER_LIM_VELOCITY,250)` → `"SET_VELOCITY_LIMIT VELOCITY=250"` ;
    ACCEL=3000, SQV → `"SQUARE_CORNER_VELOCITY=5"`, ACCEL_TO_DECEL ; `(_,0)` → false ;
  - `retraction`/variantes : `LENGTH,1500µm` → `"SET_RETRACTION RETRACT_LENGTH=1.5"`,
    `SPEED,40` → `"SET_RETRACTION RETRACT_SPEED=40"`, EXTRA=0 autorisé →
    `"UNRETRACT_EXTRA_LENGTH=0"`, borne haute rejetée.
  - Test « tampon trop court » (taille=4) → false, premier octet inchangé,
    pour au moins une fonction de chaque forme (comme les tests existants).
- [ ] **Step 2 — Lancer, voir échouer** (fonctions non définies).
- [ ] **Step 3 — Implémenter** les fonctions dans klipper_gcode.c, style
  identique à l'existant (snprintf borné, retour false sans toucher `sortie`).
- [ ] **Step 4 — Lancer host-test, voir passer** ; `idf.py build`
  (firmware/) et build simulateur verts (les .c compilent, pas encore utilisés).
- [ ] **Step 5 — Commit** `feat(klipper): fonctions gcode pour les panneaux de reglage`.

---

### Task 2: Fine Tune (ECRAN_REGLAGE_FIN)

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_reglage_fin.{h,c}`
- Modify: `firmware/main/CMakeLists.txt` (SRCS), `simulateur/main.c` (flag `--ecran fin`)
- Modify: registre de navigation si un tableau central existe (sinon rien —
  l'écran est atteint par empilement depuis la Configuration en Task 8).

**Interfaces — Consumes:** `etat_klipper_t.vitesse_pct`, `.flux_pct`,
`.babystep_z_um` ; `klipper_gcode_vitesse_impression/flux/offset_z` (Task 1).
**Produces:** `extern const ecran_desc_t ECRAN_REGLAGE_FIN;` id=`"reglage_fin"` titre=`"Fine Tune"`.

Reprend fine_tune.png : trois lignes (Z-offset, Speed, Flow), chacune un label
+ valeur relue + boutons `-` / `+`. Un sélecteur de pas Z (0.01 / 0.05 mm) et
un sélecteur de pas % (1 / 5 / 10 / 25) — deux `selecteur_choix_t` (widget de
ecran_ventilateurs.c), ou un seul si l'implémenteur juge la double échelle
gérable. Reset Z-offset = bouton dédié → `offset_z(0,true)`.

- `+`/`-` Speed → `vitesse_impression(clamp(vitesse_pct ± pas, 1, 300))`.
- `+`/`-` Flow → `flux(clamp(flux_pct ± pas, 1, 300))`.
- `+`/`-` Z → `offset_z(±pas_µm, false)`.
- mettre_a_jour : relit les 3 valeurs, formate (`"100 %"`, `"0.00 mm"`),
  grise C3 sur donnees_perimees. Défense NaN/bornes comme ecran_ventilateurs.c.

- [ ] **Step 1** Écrire test host `test_ecran_reglage_fin.c` : construire avec
  un état {vitesse_pct=100, flux_pct=95, babystep_z_um=-20}, vérifier labels
  (`"100 %"`, `"95 %"`, `"-0.02 mm"`) ; simuler clic `+Speed` (pas 5) et
  vérifier qu'un gcode `M220 S105` est émis (capture via le faux ui_commander
  du host-test, cf. tests d'écran existants). Vérifier grisage sur perimees.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter .h (contexte : labels, sélecteurs, sous-structures
  de bouton comme `preset_infos`) + .c (idiome envoyer_gcode recopié).
- [ ] **Step 4** host-test + `idf.py build` + simulateur verts ;
  `./simulateur --ecran fin` affiche l'écran.
- [ ] **Step 5** Commit `feat(klipper): panneau Fine Tune (vitesse/flux/offset Z)`.

---

### Task 3: Z Calibrate (ECRAN_ZCALIBRATE)

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_zcalibrate.{h,c}`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/main.c` (`--ecran zcal`).

**Interfaces — Consumes:** `etat_klipper_t.position[2]` ;
`klipper_gcode_calibration_z`, `klipper_gcode_testz` (Task 1).
**Produces:** `extern const ecran_desc_t ECRAN_ZCALIBRATE;` id=`"zcalibrate"` titre=`"Z Calibrate"`.

Reprend zcalibrate.png. Boutons : Start Probe (PROBE_CALIBRATE), Start Endstop
(Z_ENDSTOP_CALIBRATE), Raise Nozzle (testz +pas), Lower Nozzle (testz -pas),
Accept (ACCEPT), Abort (ABORT). Sélecteur de pas `.01/.05/.1/.5/1/5`
(→ µm : 10/50/100/500/1000/5000). Label `"Z: <position[2]>"` relu (ou `"Z: -"`
si axe Z non référencé — `axes_references` bit2). L'offre sondée Saved/New
affichée `"-"` (backend gcode_response non capturé, documenté dans le .h).

- [ ] **Step 1** Test host : clic Raise Nozzle (pas .05) → `TESTZ Z=+0.05` émis ;
  clic Accept → `ACCEPT` ; label Z formaté depuis position[2] ; grisage C3.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter (les boutons de calibration ne sont JAMAIS grisés —
  sûrs même sur état périmé, comme la grille de menu du hub ; seules les
  valeurs relues sont grisées).
- [ ] **Step 4** Builds verts ; `--ecran zcal`.
- [ ] **Step 5** Commit `feat(klipper): panneau Z Calibrate`.

---

### Task 4: Bed Level (ECRAN_NIVEAU_LIT)

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_niveau_lit.{h,c}`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/main.c` (`--ecran lit`).

**Interfaces — Consumes:** `klipper_gcode_niveau_lit` (Task 1).
**Produces:** `extern const ecran_desc_t ECRAN_NIVEAU_LIT;` id=`"niveau_lit"` titre=`"Bed Level"`.

Reprend bed_level.png, en boutons texte : Screws Adjust (SCREWS_TILT_CALCULATE),
Z-Tilt (Z_TILT_ADJUST), QGL (QUAD_GANTRY_LEVEL), Disable Motors (M84). Grille
2×2. Aucune valeur relue (pas de grisage — envoi pur, toujours sûr). La
visualisation « coins » est reportée (nécessite la sortie SCREWS_TILT_CALCULATE
via gcode_response) : documenté dans le .h.

- [ ] **Step 1** Test host : clic Screws Adjust → `SCREWS_TILT_CALCULATE` émis ;
  clic Disable Motors → `M84`.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter (grille de 4 boutons, idiome de ecran_accueil_hub.c).
- [ ] **Step 4** Builds verts ; `--ecran lit`.
- [ ] **Step 5** Commit `feat(klipper): panneau Bed Level (vis/z-tilt/qgl)`.

---

### Task 5: Limits (ECRAN_LIMITES) + relecture toolhead

**Files:**
- Modify: `firmware/main/core/etat_klipper.h` (4 `float`), `moonraker_ws.c`
  (abonnement `toolhead`), `moonraker_rpc.{h,c}` (parse), `web.c` (/state si
  applicable — vérifier).
- Create: `firmware/main/apps/klipper/ecrans/ecran_limites.{h,c}`
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/main.c` (`--ecran limites`).

**Interfaces — Produces (etat) :** ajouter à `etat_klipper_t`, APRÈS
`babystep_z_um`, groupés :

```c
    float limite_velocity;        /* mm/s, toolhead.max_velocity ; 0 = pas reçu */
    float limite_accel;           /* mm/s^2, toolhead.max_accel */
    float limite_square_corner;   /* mm/s, toolhead.square_corner_velocity */
    float limite_accel_to_decel;  /* mm/s^2, toolhead.max_accel_to_decel */
```

`extern const ecran_desc_t ECRAN_LIMITES;` id=`"limites"` titre=`"Limits"`.
**Consumes:** `klipper_gcode_limite_vitesse` (Task 1).

**RAM :** +16 octets à `etat_klipper_t` (scalaires) — sûr, DANS l'esprit de la
règle (le crash venait d'un tableau de ~2 Ko, pas de scalaires). Vérifier
malgré tout que sizeof reste bien inférieur au seuil documenté dans
moonraker_ws.c ; ne PAS ajouter de tableau.

**Backend :** ajouter `toolhead` à la liste d'objets abonnés
(`printer.objects.subscribe` / la liste dans envoyer_identify_et_abonnement)
et parser `max_velocity`, `max_accel`, `square_corner_velocity`,
`max_accel_to_decel` dans le handler de statut, EXACTEMENT comme les autres
champs y sont parsés (suivre `gcode_move`/`toolhead` existants s'ils sont déjà
lus pour position/vitesse — sinon étendre proprement). Valeurs absentes →
laisser 0.

**Écran :** liste de 4 lignes (label + valeur relue + `-`/`+`/reset), pas fixe
par ligne (ex. velocity ±10, accel ±100, sqv ±1). Reset = renvoyer la valeur
de config… non disponible sans backend ; à défaut, reset = pas de bouton reset
OU un reset vers une valeur sûre documentée. **Simplifier :** pas de bouton
reset dans cette v1 (KlipperScreen en a un, mais il relit la config — hors
scope) ; juste `-`/`+` autour de la valeur relue. Documenter l'écart dans le .h.

- [ ] **Step 1** Test host rpc : un JSON de statut `toolhead` avec les 4 champs
  → `etat.limite_*` peuplés (suivre les tests rpc existants). Test écran : clic
  `+velocity` (pas 10) sur état {limite_velocity=250} → `SET_VELOCITY_LIMIT VELOCITY=260`.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter etat + parse + abonnement + écran.
- [ ] **Step 4** host-test + `idf.py build` + simulateur verts ; valider en
  live contre vkp si possible (`--ecran limites` ne suffit pas pour le
  backend ; vérifier le parse via /state ou un test rpc).
- [ ] **Step 5** Commit `feat(klipper): panneau Limits + relecture toolhead`.

---

### Task 6: Retraction (ECRAN_RETRACTION) + relecture firmware_retraction

**Files:** comme Task 5 mais pour `firmware_retraction`.
- Modify: `etat_klipper.h` (4 `float`), `moonraker_ws.c`, `moonraker_rpc.{h,c}`.
- Create: `firmware/main/apps/klipper/ecrans/ecran_retraction.{h,c}`
- Modify: `CMakeLists.txt`, `simulateur/main.c` (`--ecran retraction`).

**Interfaces — Produces (etat) :** après les champs `limite_*` :

```c
    float retr_length;           /* mm, firmware_retraction.retract_length */
    float retr_speed;            /* mm/s, .retract_speed */
    float retr_unretract_extra;  /* mm, .unretract_extra_length */
    float retr_unretract_speed;  /* mm/s, .unretract_speed */
```

`extern const ecran_desc_t ECRAN_RETRACTION;` id=`"retraction"` titre=`"Retraction"`.
**Consumes:** `klipper_gcode_retraction` (+variantes) de Task 1.

Abonner `firmware_retraction` ; parser les 4 champs (0 si l'objet n'existe pas
sur la machine — beaucoup de configs ne l'ont pas : l'écran grise alors tout,
C3). Écran : 4 lignes label+valeur+`-`/`+`, pas par ligne (length ±0.1 mm,
speed ±5 mm/s, extra ±0.1 mm, unretract_speed ±5 mm/s).

- [ ] **Step 1** Test host rpc : JSON `firmware_retraction` → `etat.retr_*`.
  Test écran : clic `+length` (pas 0.1) sur {retr_length=1.5} →
  `SET_RETRACTION RETRACT_LENGTH=1.6`.
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter.
- [ ] **Step 4** Builds verts.
- [ ] **Step 5** Commit `feat(klipper): panneau Retraction + relecture firmware_retraction`.

---

### Task 7: Écrans stub (Power / Bed Mesh / Input Shaper / Spoolman / Updater / Console)

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_stub.{h,c}` — UN module,
  fabrique paramétrée : un `ecran_desc_t` par stub, partageant construire/
  mettre_a_jour, différenciés par un contexte statique const (titre + ligne
  d'explication). Chaque stub expose son propre `extern const ecran_desc_t`.
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/main.c`.

**Produces:** `ECRAN_POWER`, `ECRAN_BED_MESH`, `ECRAN_INPUT_SHAPER`,
`ECRAN_SPOOLMAN`, `ECRAN_UPDATER`, `ECRAN_CONSOLE` (ids `"power"`, `"bed_mesh"`,
`"input_shaper"`, `"spoolman"`, `"updater"`, `"console"` ; titres KlipperScreen).

Chaque stub affiche son titre + une ligne : `"Requires <X> — not yet available"`
où X = `"Moonraker power API"` / `"bed mesh data"` / `"resonance testing"` /
`"a Spoolman server"` / `"OTA (unavailable on this firmware)"` /
`"gcode_response capture"`. Aucune action. detruire=NULL. mettre_a_jour peut
être NULL (rien de dynamique). **Ne PAS** fabriquer de fausse donnée.

Détail d'impl : comme les `ecran_desc_t` sont des const globaux avec des
pointeurs de fonction, différencier par contexte impose que `construire`
retrouve QUEL stub il construit. Deux options — l'implémenteur tranche :
(a) six paires construire/desc triviales générées par macro X-macro ;
(b) un champ `const char*` de sous-titre stocké hors `ecran_desc_t`, retrouvé
par l'id dans une petite table. Préférer (a) : plus simple, pas de lookup.

- [ ] **Step 1** Test host : chaque `ECRAN_*` stub a un id/titre non vide et
  `construire` ne plante pas (smoke test, comme les tests d'écran existants).
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter (X-macro conseillée).
- [ ] **Step 4** Builds verts ; `--ecran power` (etc.) affiche le placeholder.
- [ ] **Step 5** Commit `feat(klipper): ecrans stub des panneaux a backend absent`.

---

### Task 8: Sous-menu Configuration + câblage du hub

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_configuration.{h,c}`
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.c` (câbler la
  case `ECRAN_ACCUEIL_HUB_MENU_REGLAGES` → `navigation_empiler(&ECRAN_CONFIGURATION)`,
  retirer le sous-titre « A venir »).
- Modify: `firmware/main/CMakeLists.txt`, `simulateur/main.c` (`--ecran config`).

**Interfaces — Consumes:** tous les `ECRAN_*` des Tasks 2-7 + `ECRAN_REGLAGES_WIFI`
(existant, titre « Network »).
**Produces:** `extern const ecran_desc_t ECRAN_CONFIGURATION;` id=`"configuration"` titre=`"Configuration"`.

Grille de boutons texte (idiome ecran_accueil_hub.c), une case par panneau,
chacune `navigation_empiler(&ECRAN_*)` : Fine Tune, Z Calibrate, Bed Level,
Limits, Retraction, Network, Power, Bed Mesh, Input Shaper, Spoolman, Updater,
Console (12 cases). Ajuster colonnes/lignes (ex. 3×4) avec les mêmes
`_Static_assert` de non-débordement et de clearance du bandeau que le hub.
La grille défile si nécessaire (LV_OBJ_FLAG_SCROLLABLE autorisé ici,
contrairement au hub) — mais préférer une grille fixe qui tient dans 436 px si
12 cases y rentrent (cases ~52 px ×4 lignes + écarts ≈ 232 px : OK sans scroll).

- [ ] **Step 1** Test host : construire ECRAN_CONFIGURATION ne plante pas ;
  après construire, un clic sur la case Fine Tune empile bien un écran (via le
  faux de navigation du host-test s'il existe — sinon vérifier que le cb est
  bien attaché, comme les tests du hub).
- [ ] **Step 2** Lancer, voir échouer.
- [ ] **Step 3** Implémenter l'écran + câbler le hub.
- [ ] **Step 4** host-test + `idf.py build` + simulateur verts ; parcours
  manuel sim : Home → Reglages → Configuration → chaque panneau.
- [ ] **Step 5** Commit `feat(klipper): sous-menu Configuration relie les panneaux de reglage`.

---

## Self-review (couverture spec)

- Fine Tune, Z Calibrate, Bed Level, Limits, Retraction : Tasks 2-6. ✅
- Stubs Power/Bed Mesh/Input Shaper/Spoolman/Updater/Console : Task 7. ✅
- Découpage/nav Configuration + case Reglages du hub : Task 8. ✅
- Fonctions gcode bornées + testées : Task 1. ✅
- Contrainte RAM (scalaires seulement, pas de tableau) : Tasks 5-6 explicites. ✅
- ASCII/anglais, contrat ecran_desc_t, grisage C3, ≥44 px, sim flag : rappelés
  par tâche. ✅
- Cohérence des noms : `ECRAN_REGLAGE_FIN`, `ECRAN_ZCALIBRATE`,
  `ECRAN_NIVEAU_LIT`, `ECRAN_LIMITES`, `ECRAN_RETRACTION`, `ECRAN_CONFIGURATION`
  + 6 stubs + gcode enums/signatures — identiques entre Produces (Task 1/2-7)
  et Consumes (Task 8). ✅
