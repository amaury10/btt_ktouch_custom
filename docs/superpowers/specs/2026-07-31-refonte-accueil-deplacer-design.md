# Refonte ergonomique — Accueil-hub + écran Déplacer + rail (sous-projet 1)

Date : 2026-07-31
Statut : design validé visuellement (mockups), en attente de relecture spec.

## 1. Contexte & objectif

L'accueil idle actuel tasse quatre groupes de contrôles (jog XY, colonne Z,
homing, sélecteur de pas) dans une zone de 92 px, avec préréglages et macros en
rangées fines (24/28 px) — l'écran est saturé verticalement (« égalité exacte »
dans le code). Résultat : **jog aux boutons trop petits, pas/vitesse
inutilisables ou absents, homing peu clair, boutons fins**.

Décision (validée) : **restructurer l'app façon KlipperScreen** — un
**accueil-hub** (températures multitête + grille de menu) qui ouvre des
**écrans dédiés**, plus un **rail d'accès rapide persistant** à gauche.

Ce spec couvre le **premier sous-projet** : le **rail persistant**,
l'**Accueil-hub** et l'**écran Déplacer**. Les autres écrans (Températures,
Extruder, Ventilateurs, Macros, Impression) sont des sous-projets ultérieurs
(spec → plan → implémentation chacun).

## 2. Architecture / navigation

- **Rail gauche persistant (~58 px)**, présent sur tous les écrans :
  🏠 **Accueil** · ⌂ **Home** (Home All) · ≡ **Macros** · ⛔ **STOP** (arrêt
  d'urgence, rouge, en bas). Le bouton de l'écran courant est surligné.
- **Zone principale (~742 px)** qui change selon l'écran actif.
- **Navigation** : réutiliser la pile d'écrans existante (`navigation.c`). Le
  rail déclenche des actions : aller à l'Accueil, Home All, ouvrir Macros,
  E-STOP. Ces actions doivent fonctionner **depuis n'importe quel écran**.
- **Accueil** = hub : tuiles température (multitête) + grille de menu (Déplacer,
  Températures, Extruder, Ventilateurs, Imprimer, Réglages).
- **Déplacer** = écran de jog dédié.

## 3. Composants

### 3.1 Rail (widget partagé, nouveau)
- 4 boutons icône+libellé, cible tactile ~46 px. STOP visuellement distinct
  (rouge) et poussé en bas.
- Expose une API type widget (comme `selecteur_pas`, `tuile`) : création dans un
  parent, callbacks par bouton, mise à jour de l'état « écran actif ».
- **STOP** = `M112` (arrêt d'urgence Klipper) — **à valider** : M112 seul, ou
  M112 + confirmation ? (M112 coupe tout, irréversible sans firmware_restart.)

### 3.2 Accueil-hub (remplace/refond l'accueil idle)
- **Tuiles température** : une par chauffe (extrudeurs T0…Tn + plateau),
  actuelle / consigne, cliquable (→ écran Températures, sous-projet ultérieur).
  Outil actif surligné. **Adaptatif au nombre de chauffes** — réutiliser le
  concept de paliers existant (`klipper_paliers.h`, MONO/MOYEN/COMPACT) pour
  passer de 1 rangée (≤4 tuiles) à une grille compacte jusqu'à 8 extrudeurs + 1
  plateau. **À valider** : nb réel d'extrudeurs des machines cibles (dimensionne
  la grille par défaut).
- **Grille de menu** : 6 gros boutons (~52 px) — Déplacer, Températures,
  Extruder, Ventilateurs, Imprimer, Réglages. (Accueil/Home/Macros sont dans le
  rail, donc hors grille.)

### 3.3 Déplacer (écran dédié, nouveau)
- **Croix jog XY** : Y+ (haut), X−/X+ (milieu), Y− (bas), gros boutons.
- **Colonne Z** : Z+ / Z−, à droite de la croix.
- **Sélecteur de Pas** : 0.1 / 1 / 10 / 100 mm (réutiliser `selecteur_pas`,
  agrandi).
- **Sélecteur de Vitesse** : Lent / Moyen / Rapide (nouveau, même idiome que le
  sélecteur de pas). Mappe vers un feedrate. **Valeurs proposées (à valider)** :
  XY 600 / 3000 / 6000 mm·min⁻¹ ; Z 300 / 600 / 1200 mm·min⁻¹.
- **Rangée Home** : All / X / Y / Z (per-axe), pour rendre le homing clair et
  groupé avec le mouvement. **À valider** : garder la rangée Home ici en plus du
  ⌂ du rail, ou homing uniquement via le rail ?

## 4. Flux de données

- **Klipper → UI** (via le backend existant) : températures actuelle/consigne
  par chauffe, position X/Y/Z, outil actif, état homed. Alimente les tuiles et
  la ligne d'état. Données périmées/déconnecté → grisé (pattern existant).
- **Jog** : bouton → couche gcode existante (`klipper_gcode.c`) :
  `SAVE_GCODE_STATE / G91 / G1 <axe><pas> F<feedrate> / RESTORE`. Le **Pas** et
  la **Vitesse** sélectionnés paramètrent `<pas>` et `<feedrate>`.
- **Home** : `G28` (All) / `G28 X|Y|Z` (per-axe).
- **E-STOP** (rail) : `M112`.
- **Boutons Accueil/Macros** (rail) et cases de menu : navigation d'écran (pas
  de gcode).

## 5. Gestion d'erreurs

Réutilise les patterns existants : écran indisponible → mode dégradé
(WiFi/`/revert` vivants) ; backend déconnecté → données grisées ; commande gcode
en échec → notification (bandeau). Rien de nouveau à inventer ici.

## 6. Tests (host-test)

- Chaque écran testable via le harnais existant (LVGL simulateur) :
  construction sans crash, callbacks émettent le **bon gcode** (jog avec le bon
  axe/pas/feedrate, home avec le bon axe, E-STOP = M112), navigation par le rail
  (aller à Accueil/Macros depuis Déplacer), tuiles reflètent l'état.
- Nouveaux fichiers : `test_rail.c`, `test_accueil_hub.c`, `test_deplacer.c`
  (sur le modèle de `test_ecran_accueil_idle.c`, dont une partie sera
  reprise/retirée).

## 7. Périmètre & hors-scope

- **Dans ce sous-projet** : rail, Accueil-hub, Déplacer.
- **Hors-scope (sous-projets suivants)** : écrans Températures, Extruder,
  Ventilateurs, Macros, Impression (`job_status`) ; la sélection d'outil actif
  (dans Extruder) ; le traitement du bandeau de notification en overlay (peut
  être fait avec l'un de ces écrans).
- L'ancien `ecran_accueil_idle.c` est **remplacé** par l'Accueil-hub + Déplacer ;
  son code de jog/homing/pas est repris/déplacé, pas jeté.

## 8. Détails à trancher à la relecture

1. **Nb d'extrudeurs réels** des machines (dimensionne les tuiles).
2. **Feedrates** Lent/Moyen/Rapide (valeurs §3.3).
3. **Rangée Home** sur Déplacer en plus du rail, ou pas (§3.3).
4. **E-STOP** : M112 direct ou avec confirmation (§3.1).
