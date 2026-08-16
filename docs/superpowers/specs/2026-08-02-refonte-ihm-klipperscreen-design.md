# Refonte IHM alignée KlipperScreen — conception

**Date :** 2026-08-02
**Statut :** design validé (dialogue utilisateur). Décisions cadrantes :
« calquer les menus KlipperScreen au plus juste, quitte à refactoriser toutes
les IHM » ; **fidélité = structure/menus d'abord, icônes plus tard** (texte +
symboles LVGL, thème sombre maison) ; **accueil = résumé compact SANS graphe**.

## Objectif

Reprendre le **découpage et la navigation** de KlipperScreen à l'identique
pour que la communauté s'y retrouve : accueil `main_panel`, sous-menus
**Actions** et **Configuration**, et un **rail** avec un vrai bouton retour.
Le travail fonctionnel existant (gcode, état, logique des panneaux) est
**réutilisé** — c'est un ré-habillage + une restructuration de la navigation,
pas une réécriture.

## Contraintes globales

- **Réutilisation** : les écrans Move/Extrude/Fan/Temperature/Macros et les 5
  panneaux de réglage (Limits, Retraction, Z Calibrate, Bed Level, Network) +
  les 6 stubs restent tels quels ; on ne refait que l'accueil, le rail, et les
  écrans de regroupement (Actions, Homing).
- **RAM** : ne pas gonfler `etat_klipper_t` avec des tableaux (règle
  [[etat-klipper-taille-piles]]). Le résumé d'accueil lit des champs déjà
  présents (températures, position, axes_references, outil_actif, vitesse_pct,
  flux_pct, progression, impression_en_cours) — aucun ajout d'état attendu.
- **Textes** ASCII/anglais (police Montserrat) ; commentaires français.
- **Contrat `ecran_desc_t`**, contexte socle, grisage C3, cibles ≥ 44 px,
  `_Static_assert` de non-débordement + clearance du bandeau.
- **Pas d'icônes bitmap** ce lot-ci (lot dédié ultérieur) : libellés texte +
  `LV_SYMBOL_*` uniquement.
- Builds host-test + idf + sim verts ; SDD (agents) comme le lot précédent.

## 1. Rail (widgets/rail.c + ui/habillage.c)

Passe de {Accueil, Home(homing), Macros, STOP} à **{Back, Accueil, Macros,
STOP}**, de haut en bas :

- **Back** (nouveau) : `navigation_depiler()` — remonte d'UN niveau (la flèche
  KlipperScreen). Icône `LV_SYMBOL_LEFT`.
- **Accueil** : inchangé, `navigation_accueil()` (retour au main_panel).
- **Macros** : inchangé (accès rapide permanent, décision utilisateur).
- **STOP** : **repositionné EN BAS** de la colonne, rouge, isolé ; toujours
  protégé par confirmation (`rail_actions.c`, inchangé).
- Le **homing quitte le rail** (devient une tuile de l'accueil).

**Bandeau de notification** (habillage.c `construire_bandeau`) : passe de
`(0, 420, 800×60)` à **`(RAIL_LARGEUR=58, 420, 742×60)`** pour ne PLUS recouvrir
la colonne du rail — c'est ce qui permet à STOP de vivre en bas sans être
masqué (la raison d'origine de son passage en haut). Ajuster en conséquence
les `_Static_assert` de clearance des écrans (le bandeau ne couvre plus la
bande sous le rail, mais couvre toujours le bas de la zone de contenu).

`rail_action_t` gagne `RAIL_BACK` ; `RAIL_NB` passe à 5 ? Non — on remplace
l'entrée homing : l'enum devient {RAIL_BACK, RAIL_ACCUEIL, RAIL_MACROS,
RAIL_STOP}, `RAIL_NB=4` (le homing du rail est supprimé). `rail_actions.c` :
`RAIL_BACK → navigation_depiler()`, retrait du `case RAIL_HOME`.

## 2. Accueil = main_panel KlipperScreen (résumé compact)

Remplace le hub actuel (tuiles de température + grille 6). Nouveau contenu :

- **Zone résumé** (haut) : températures compactes (extrudeurs présents +
  plateau, actuelle/consigne) ; ligne position + homing (X/Y/Z, « -- » si non
  référencé) ; outil actif ; vitesse/flux (%) ; mini-progression si impression
  en cours ; état liaison. Tout en LECTURE (grisé C3 sur données périmées).
  Réutilise les idiomes de formatage existants (ui_format_temperature,
  formatage position de ecran_deplacer.c).
- **Grille de menu 5 tuiles** : **Homing · Temperature · Actions ·
  Configuration · Print**.

| Tuile | Ouvre |
|---|---|
| Homing | `ECRAN_HOMING` (nouveau, §4) |
| Temperature | `ECRAN_TEMPERATURES` (existant) |
| Actions | `ECRAN_ACTIONS` (nouveau, §3) |
| Configuration | `ECRAN_MENU_REGLAGES` (existant, lot précédent) |
| Print | `ECRAN_FICHIERS` (existant) |

La bascule vivante repos↔impression (habillage_definir_choix_accueil) reste :
le main_panel est le fond « repos » ; l'écran de statut d'impression reste le
fond « impression » (inchangé).

## 3. Sous-menu Actions (nouveau — ECRAN_ACTIONS)

Grille de boutons texte (idiome de `ecran_menu_reglages.c`), câblés en
`navigation_empiler` vers des écrans **déjà existants** :

Move (`ECRAN_DEPLACER`) · Extrude (`ECRAN_EXTRUDER`) · Fan
(`ECRAN_VENTILATEURS`) · Temperature (`ECRAN_TEMPERATURES`) · Macros
(`ECRAN_MACROS`) · Disable Motors (action M84 directe, confirmation légère) ·
Console (`ECRAN_CONSOLE`, stub). id `"actions"`, titre `"Actions"`.

## 4. Panneau Homing (nouveau — ECRAN_HOMING)

Boutons : Home All (G28) · Home X · Home Y · Home Z (G28 X/Y/Z via
`klipper_gcode_home` masqué, déjà existant). Envoi direct, sans confirmation
(homing non destructif — même politique que l'actuel bouton Home du rail).
id `"homing"`, titre `"Homing"`.

## 5. Raccords de fidélité

- **Fine Tune** : chez KlipperScreen il vit dans le flux d'impression (Job
  Status), pas dans Configuration. Le retirer de `ECRAN_MENU_REGLAGES` et le
  rendre atteignable depuis l'écran de statut d'impression (`ecran_accueil.c`).
- **Disable Motors** : action M84 (helper gcode existant `klipper_gcode_niveau_lit`
  KLIPPER_LIT_DISABLE) depuis la tuile Actions.
- **Rond jaune bas-gauche** : bug à corriger dans ce lot une fois identifié
  (piste : pastille de liaison mal placée OU artefact BSP ; perf-monitor déjà
  écarté, désactivé dans sdkconfig). Rattaché à la refonte habillage.

## Hors périmètre (lots ultérieurs)

- **Icônes bitmap** façon KlipperScreen (pipeline d'assets — lot dédié).
- **Graphe de température** live sur l'accueil.
- Alignement fin des layouts internes de chaque panneau (sliders Limits/
  Retraction, grille 2×3 Fine Tune) — la structure/nav prime d'abord.

## Ordre de réalisation

① Rail (Back + STOP bas + bandeau clear rail) — corrige aussi le mis-tap STOP.
② Sous-menu Actions. ③ Panneau Homing. ④ Accueil main_panel + câblage des 5
tuiles + retrait de l'ancien hub. ⑤ Raccords (Fine Tune → impression, rond
jaune). Chaque étape testable seule ; le rail en premier pour lever le danger
ergonomique tout de suite.
