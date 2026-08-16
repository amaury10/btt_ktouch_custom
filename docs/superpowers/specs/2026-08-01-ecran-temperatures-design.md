# Écran Températures (panneau KlipperScreen) — sous-projet 2

Date : 2026-08-01
Statut : design (décisions prises en autonomie pendant l'absence de
l'utilisateur — à relire ; suit le découpage KlipperScreen validé).

## 1. Contexte & objectif

La refonte (sous-projet 1) a remplacé l'ancien `ecran_accueil_idle.c` par un
Accueil-hub + un écran Déplacer, puis **supprimé l'idle** (tâche 7). L'idle
portait le **réglage des consignes de température** (tap sur une tuile →
clavier numérique, + préréglages PLA/PETG/ABS/Off). Ce code est parti avec
lui : le hub actuel n'affiche les tuiles qu'en **lecture seule**.

**Régression fonctionnelle à combler : on ne peut plus régler une température
au tactile.** Ce sous-projet revit cette fonction dans un **panneau
Températures dédié**, conformément au découpage KlipperScreen (Temperature est
son propre panneau), atteint depuis la case menu « Températures » du hub.

## 2. Architecture / navigation

- Nouvel écran `ECRAN_TEMPERATURES` (id `"temperatures"`), empilé par la case
  menu `ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES` (index 1) du hub, et par un tap
  sur n'importe quelle tuile de température du hub (le design du sous-projet 1
  prévoyait déjà les tuiles « cliquables → écran Températures »).
- Vit dans le conteneur de navigation de 742 px (à droite du rail
  persistant), sous la barre d'état. Rail et navigation inchangés.
- Réseau-libre, contrat `ecran.h` (contexte alloué par le socle), grisage C3
  sur `donnees_perimees` — comme tous les écrans du jalon.

## 3. Composants (742×436, thème sombre)

### 3.1 Grille de tuiles de chauffe (haut)
Une tuile par chauffe présente (extrudeurs T0…Tn + plateau), **adaptative au
nombre de têtes** via `klipper_paliers.h` (MONO/MOYEN/COMPACT) — même géométrie
que le hub/l'ancien idle. Chaque tuile montre nom / température actuelle /
consigne, outil actif surligné (bordure bleue). **Chaque tuile est tappable →
clavier numérique** pré-rempli avec la consigne courante ; à la validation, la
saisie est bornée [0, 350] °C (une saisie hors borne ou non numérique →
notification d'erreur, **aucun** gcode envoyé) et envoyée via
`klipper_gcode_consigne_temp(chauffeur, cible)` → `ui_commander`. Nom de
chauffeur : `extruder`/`extruderN` pour les extrudeurs, `heater_bed` pour le
plateau.

### 3.2 Rangée de préréglages (bas)
Quatre boutons : **PLA · PETG · ABS · Off**. Chaque préréglage envoie
**DEUX** gcodes (jamais un combiné) : la **buse ACTIVE** (relue au moment du
clic) puis le **plateau**. Valeurs (reprises telles quelles de l'ancien idle) :
PLA 210/60, PETG 240/80, ABS 250/100, Off 0/0 (`Off` n'est pas un cas spécial :
mêmes appels avec cibles nulles, `klipper_gcode_consigne_temp` accepte 0 =
éteindre).

## 4. Flux de données

- **Klipper → UI** : températures actuelle/consigne par chauffe, outil actif,
  via l'`etat_klipper_t` déjà transmis à `mettre_a_jour`. Périmé → tuiles
  grisées (pattern existant).
- **Réglage** : tuile → `clavier_ouvrir(titre, consigne_courante,
  CLAVIER_NUMERIQUE, rappel, &info)` → `klipper_gcode_consigne_temp` →
  `ui_commander(BACKEND_ACTION_GCODE, {"script":...})`.
- **Préréglage** : bouton → 2× `klipper_gcode_consigne_temp` (buse active +
  plateau) → `ui_commander`.

## 5. Réutilisation

Toute la logique de réglage est **reprise à l'identique de l'ancien
`ecran_accueil_idle.c`** (dans l'historique git, commit `ce47e3a` avant sa
suppression) : `nom_chauffeur_extrudeur`, `consigne_u16`,
`cellule_info_nom_chauffeur`, `cellule_clavier_rappel`, `cellule_bouton_cb`,
`preset_bouton_cb`, la construction/`mettre_a_jour` des tuiles par palier. On
revit du code **éprouvé et déjà relu**, pas une invention. La géométrie des
tuiles est celle du hub (`ecran_accueil_hub.c`).

## 6. Gestion d'erreurs

Patterns existants : saisie invalide → notification (bandeau), jamais de
consigne aberrante envoyée ; backend déconnecté → tuiles grisées ; un envoi de
consigne réussi ne pose jamais de bannière (silencieux, comme l'idle).

## 7. Tests (host-test)

Sur le modèle de l'ancien `test_ecran_accueil_idle.c` (section températures,
git `ce47e3a`) : construction sans crash ; une tuile tap ouvre le clavier
pré-rempli à la bonne consigne ; une saisie valide émet
`SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210` (via
`source_etat_sim_derniere_commande`) ; une saisie hors borne/non numérique
n'émet RIEN et notifie ; chaque préréglage émet 2 gcodes (buse active +
`heater_bed`) aux bonnes cibles ; grisage sur `donnees_perimees` ; navigation
depuis le hub (case menu ET tap tuile → profondeur 2, id `"temperatures"`).

## 8. Périmètre & hors-scope

- **Dans ce sous-projet** : `ECRAN_TEMPERATURES` (tuiles réglables + presets),
  câblage depuis le hub (case menu + tap tuile).
- **Hors-scope** : Extruder (extrusion/rétraction, sélection d'outil actif),
  Ventilateurs, Imprimer (job_status) — sous-projets KlipperScreen suivants.
  La sélection de l'outil actif reste dans un futur panneau Extruder ; ici la
  buse « active » est celle rapportée par l'état (`outil_actif`), non
  modifiable.
