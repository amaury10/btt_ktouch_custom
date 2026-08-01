# Écran Extruder (panneau KlipperScreen) — sous-projet 3

Date : 2026-08-01
Statut : design (décisions prises en autonomie pendant l'absence de
l'utilisateur — suit le découpage KlipperScreen ; à relire).

## 1. Contexte & objectif

Suite du découpage KlipperScreen. Après Déplacer, Températures, le panneau
**Extruder** : pousser/rétracter du filament, et — sur machine multi-tête —
**choisir l'outil actif** (fonction que le spec Températures a explicitement
renvoyée « à un futur panneau Extruder »). Atteint par la case menu
`ECRAN_ACCUEIL_HUB_MENU_EXTRUDER` (index 2) du hub.

## 2. Architecture / navigation

- Nouvel `ECRAN_EXTRUDER` (id `"extruder"`), empilé par la case menu Extruder
  du hub. Vit dans le conteneur de nav 742px. Contrat `ecran.h`, C3, réseau-libre.

## 3. Composants (742×436, thème sombre)

### 3.1 Sélecteur d'outil actif (haut, si `nb_extrudeurs > 1`)
`selecteur_choix` (widget T1) avec T0…Tn ; l'outil actif rapporté par l'état
(`outil_actif`) est présélectionné. Un choix envoie
`ACTIVATE_EXTRUDER EXTRUDER=extruder`/`extruderN` (convention de nommage
Klipper, identique aux chauffeurs). **Masqué** si `nb_extrudeurs ≤ 1` (rien à
choisir) — la ligne d'état montre alors juste « T0 ».

### 3.2 Température de la buse active (lecture)
Une tuile (ou ligne) montrant actuelle/consigne de la buse **active**, en
LECTURE seule ici (le réglage se fait dans le panneau Températures). Sert de
repère « assez chaud pour extruder ? ». Grisée sur `donnees_perimees`.

### 3.3 Sélecteur de longueur
`selecteur_choix` : **5 / 10 / 25 / 50 mm** (défaut 10). Valeurs usuelles
KlipperScreen.

### 3.4 Sélecteur de vitesse
`selecteur_choix` : **Lent / Moyen / Rapide** → 2 / 5 / 10 mm·s⁻¹ (soit
120 / 300 / 600 mm·min⁻¹, défaut Moyen). Feedrate passé à `G1 E … F<mm/min>`.

### 3.5 Boutons Extruder / Rétracter
Deux gros boutons. **Extruder** pousse (`E` positif), **Rétracter** tire
(`E` négatif), avec la longueur et la vitesse sélectionnées.

## 4. Flux de données

- **Klipper → UI** : `nb_extrudeurs`, `outil_actif`, température de la buse
  active — via l'`etat_klipper_t` transmis à `mettre_a_jour`. Périmé → grisé.
- **Changement d'outil** : `ACTIVATE_EXTRUDER EXTRUDER=<chauffeur>` →
  `ui_commander`.
- **Extrusion / rétraction** : `SAVE_GCODE_STATE NAME=ktouch_extrude / M83 /
  G1 E<±longueur> F<feedrate> / RESTORE_GCODE_STATE NAME=ktouch_extrude`
  (même discipline SAVE/RESTORE que le jog, pour ne jamais laisser la machine
  en mode relatif) → `ui_commander`.

## 5. Garde à froid (sécurité)

Klipper **refuse côté serveur** toute extrusion sous `min_extrude_temp`
(protège l'extrudeur/le filament) — la sûreté ne dépend donc PAS de l'UI. Ce
panneau **envoie** le gcode ; si la buse est trop froide, Klipper rejette et
l'échec asynchrone remonte via le bandeau (`ui_commande_echec` →
« Command failed », chemin existant). Décision MVP : **pas de désactivation
client-side** des boutons (on ne connaît pas `min_extrude_temp`, qui vit dans
la config, pas dans `/state`) — la garde autoritative reste Klipper. À
raffiner plus tard si l'on décide d'exposer `min_extrude_temp`.

## 6. Réutilisation

`selecteur_choix` (T1), la tuile de température (`tuile.h`/
`ui_format_temperature`), `nom_chauffeur_extrudeur` (même convention que
Températures), l'idiome `construire_arguments_gcode`/`envoyer_gcode` (cJSON,
comme `rail_actions.c`/`ecran_temperatures.c`), la structure d'écran
(`ecran_deplacer.c`).

## 7. Nouveau gcode (fonctions pures, `klipper_gcode.c`)

- `bool klipper_gcode_extrude(char* sortie, size_t taille, float distance_mm,
  uint16_t vitesse_mm_min)` : `SAVE_GCODE_STATE NAME=ktouch_extrude / M83 /
  G1 E<distance> F<vitesse> / RESTORE_GCODE_STATE NAME=ktouch_extrude`.
  `distance_mm` non nul, fini, borné ±200 mm ; `vitesse_mm_min` ∈ [1, 6000].
  Distance formatée ≤2 décimales, signe négatif = rétraction.
- `bool klipper_gcode_activer_outil(char* sortie, size_t taille,
  uint8_t indice)` : `ACTIVATE_EXTRUDER EXTRUDER=extruder`/`extruderN`.
  `indice < KLIPPER_EXTRUDEURS_MAX`.

## 8. Tests (host-test)

Gcode pur (`test_klipper_gcode.c`) : extrude `E10 F300`, retract `E-10`,
bornes rejetées, `ACTIVATE_EXTRUDER EXTRUDER=extruder`/`extruder1`. Écran
(`test_ecran_extruder.c`) : Extruder/Rétracter émettent le bon gcode signé
(longueur×vitesse), changement d'outil émet `ACTIVATE_EXTRUDER`, sélecteur
d'outil masqué si `nb_extrudeurs≤1`, grisage C3, navigation depuis le hub
(case menu → profondeur 2, id `"extruder"`).

## 9. Périmètre & hors-scope

- **Dans** : `ECRAN_EXTRUDER` (outil actif + extrude/retract + longueur/vitesse),
  gcode extrude/activer-outil, câblage case menu Extruder.
- **Hors** : Ventilateurs, Imprimer (job_status) — panneaux suivants. Réglage
  de la consigne de la buse (reste dans Températures). Load/Unload macros
  (peuvent venir via l'écran Macros existant).
