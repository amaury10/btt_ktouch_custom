# Panneaux KlipperScreen — conception

**Date :** 2026-08-02
**Statut :** validé (mandat utilisateur : « construire les panneaux équivalents à
ceux de KlipperScreen avec la même logique de découpage et de navigation »,
« fais-les tous d'un coup », en autonomie ; texte plutôt qu'icônes, couleurs
libres).

## Objectif

Compléter le catalogue d'écrans en reprenant fidèlement le découpage
KlipperScreen (docs/img/panels), avec des libellés **texte** (pas d'icônes),
la palette sombre déjà en place, et la **même logique de navigation** : un
sous-menu **Configuration** regroupe les panneaux de réglage, comme le menu
« Configuration » de KlipperScreen.

## Contraintes globales (rappel, s'appliquent à CHAQUE tâche)

- **RAM / piles :** ne PAS gonfler `etat_klipper_t` avec de gros tableaux.
  De petits champs scalaires (quelques `float`) sont sûrs ; un tableau du
  gabarit de `fichiers[]` a déjà provoqué un crash de pile + épuisement de
  RAM interne (voir `klipper_fichiers.h`, `etat-klipper-taille-piles`). Les
  panneaux qui ont besoin de relire des valeurs machine ajoutent au plus une
  poignée de `float` à `etat_klipper_t`.
- **Polices :** tout texte AFFICHÉ est ASCII / anglais (Montserrat embarqué
  ne garantit pas le Latin-1 ; un glyphe accentué se rend en tofu). Les
  commentaires de code restent en français.
- **Contrat d'écran :** chaque panneau est un `ecran_desc_t`
  {id, titre, taille_contexte, construire, mettre_a_jour, detruire}, état
  par instance dans le contexte alloué par le socle (JAMAIS de statique de
  fichier), grisage C3 des valeurs sur `donnees_perimees` (jamais une boîte
  d'erreur).
- **Gcode :** toute action passe par une fonction pure de `klipper_gcode.c`
  (bornée, testée sur hôte, rend `false` sans toucher `sortie` si invalide)
  puis `ui_commander(BACKEND_ACTION_GCODE, {"script":...})` via l'idiome
  `construire_arguments_gcode`/`envoyer_gcode` recopié par écran.
- **Cible tactile :** ≥ 44 px ; garde `_Static_assert` que rien ne passe
  sous le bandeau de notification (coord. absolues, cf. ecran_ventilateurs.c).
- **Builds :** host-test + firmware (idf.py) + simulateur verts ; gcode
  validé en live contre vkp (localhost:7125) ou l'imprimante réelle quand
  c'est possible.

## Découpage / navigation

Le hub d'accueil (ECRAN_ACCUEIL_HUB) garde sa grille de 6 cases validée au
matériel. Sa case **« Reglages »**, aujourd'hui no-op « A venir », devient le
point d'entrée du sous-menu **Configuration** :

```
Home (hub)  ──► Deplacer / Temperatures / Extruder / Ventilateurs / Imprimer
            └─► Reglages ──► [ECRAN_CONFIGURATION]  (sous-menu, grille texte)
                              ├─ Fine Tune        (ECRAN_REGLAGE_FIN)
                              ├─ Z Calibrate      (ECRAN_ZCALIBRATE)
                              ├─ Bed Level        (ECRAN_NIVEAU_LIT)
                              ├─ Limits           (ECRAN_LIMITES)
                              ├─ Retraction       (ECRAN_RETRACTION)
                              ├─ Network          (ECRAN_REGLAGES_WIFI, déjà là)
                              └─ [stubs] Power / Bed Mesh / Input Shaper /
                                          Spoolman / Updater / Console
```

On ne démonte PAS le haut de la navigation (validé au matériel) : on ajoute
un niveau de regroupement, ce qui EST la logique KlipperScreen (menu
« Configuration ») sans risque de régression sur ce qui marche.

## Panneaux

### Faisables sans backend (état ou gcode déjà disponibles)

1. **Configuration** (sous-menu) — grille de boutons texte, chaque bouton
   `navigation_empiler(&ECRAN_*)` ; réutilise l'idiome de grille de
   `ecran_accueil_hub.c`. Les cases stub ouvrent leur écran stub.

2. **Fine Tune** (`ECRAN_REGLAGE_FIN`) — reprend fine_tune.png. Lit
   `vitesse_pct` (M220), `flux_pct` (M221), `babystep_z_um`
   (SET_GCODE_OFFSET) — **déjà dans `etat_klipper_t`, zéro backend**. Trois
   lignes réglables : Z-offset (± pas 0.01/0.05 mm, reset SET_GCODE_OFFSET
   Z=0 MOVE=1), Speed (± pas 1/5/10/25 %, M220), Flow (± idem, M221). Un
   sélecteur de pas partagé façon KlipperScreen (pas Z distinct des pas %).
   Valeurs relues et grisées C3.

3. **Z Calibrate** (`ECRAN_ZCALIBRATE`) — reprend zcalibrate.png. Envoi pur :
   Start = PROBE_CALIBRATE (ou Z_ENDSTOP_CALIBRATE selon config — on propose
   les deux boutons), Raise/Lower Nozzle = TESTZ Z=+pas / Z=-pas, Accept =
   ACCEPT, Abort = ABORT, sélecteur de pas (.01/.05/.1/.5/1/5). Affiche
   `position[2]` comme « Z: » (déjà dans l'état). L'offre sondée « Saved/New »
   nécessiterait la capture de `gcode_response` — reportée (affiche « - »).

4. **Bed Level** (`ECRAN_NIVEAU_LIT`) — reprend bed_level.png. Envoi pur :
   Screws Adjust = SCREWS_TILT_CALCULATE, Z-Tilt = Z_TILT_ADJUST, QGL =
   QUAD_GANTRY_LEVEL, Disable Motors = M84. La visualisation « coins »
   dépendrait de la sortie SCREWS_TILT_CALCULATE (gcode_response) — reportée :
   on liste les actions en boutons texte.

### Faisables avec petit ajout backend (quelques `float`, sûr)

5. **Limits** (`ECRAN_LIMITES`) — reprend limits.png. SET_VELOCITY_LIMIT
   VELOCITY/ACCEL/SQUARE_CORNER_VELOCITY/ACCEL_TO_DECEL. Relit les valeurs
   courantes du `toolhead` (4 `float` ajoutés à `etat_klipper_t` +
   abonnement/parse). Liste défilante de lignes (label + valeur + ±/reset).

6. **Retraction** (`ECRAN_RETRACTION`) — reprend retraction.png. SET_RETRACTION
   RETRACT_LENGTH/RETRACT_SPEED/UNRETRACT_EXTRA_LENGTH/UNRETRACT_SPEED. Relit
   `firmware_retraction` (4 `float` ajoutés + abonnement/parse). Idem liste.

### Stubs honnêtes (backend absent — placeholder, pas de faux)

7. **Power / Bed Mesh / Input Shaper / Spoolman / Updater / Console** — chacun
   un `ecran_desc_t` minimal : titre + une ligne « Requires <backend> — not
   yet available » + retour via le rail. Aucune action simulée. Chacun
   documente le backend manquant (API Moonraker device_power ; données de
   maillage ; test de résonance ; serveur Spoolman ; OTA impossible depuis
   notre fw ; capture `gcode_response`). Ils occupent leur case dans le
   sous-menu Configuration pour que le découpage soit complet et visible.

## Nouvelles fonctions gcode pures (klipper_gcode.{h,c})

- `klipper_gcode_vitesse_impression(pct)` → `M220 S<pct>` (pct ∈ [1,300]).
- `klipper_gcode_flux(pct)` → `M221 S<pct>` (pct ∈ [1,300]).
- `klipper_gcode_offset_z(delta_um, reset)` → reset ? `SET_GCODE_OFFSET Z=0 MOVE=1`
  : `SET_GCODE_OFFSET Z_ADJUST=<delta> MOVE=1` (delta borné ±2000 µm, formaté mm).
- `klipper_gcode_calibration_z(action)` → PROBE_CALIBRATE / Z_ENDSTOP_CALIBRATE /
  ACCEPT / ABORT (enum).
- `klipper_gcode_testz(delta_um)` → `TESTZ Z=<±delta>` (borné ±5 mm).
- `klipper_gcode_niveau_lit(action)` → SCREWS_TILT_CALCULATE / Z_TILT_ADJUST /
  QUAD_GANTRY_LEVEL / M84 (enum).
- `klipper_gcode_limite_vitesse(champ, valeur)` → `SET_VELOCITY_LIMIT <CLE>=<v>`
  (enum champ → clé ; valeur bornée).
- `klipper_gcode_retraction(champ, valeur_um)` → `SET_RETRACTION <CLE>=<v>`.

Toutes bornées, sans allocation, testées sur hôte (mêmes bornes-exactes que
les tests existants de klipper_gcode).

## Ordre de réalisation

Phase A (aucun risque backend) : Configuration → Fine Tune → Z Calibrate →
Bed Level. Phase B (petit ajout backend relu) : Limits → Retraction. Phase C
(stubs) : les 6 placeholders, en une passe.

Chaque phase produit un logiciel testable seul. Les stubs de Phase C rendent
le sous-menu Configuration complet même si leur backend viendra plus tard.
