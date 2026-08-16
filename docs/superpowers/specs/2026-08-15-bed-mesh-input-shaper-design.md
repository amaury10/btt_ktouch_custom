# Bed Mesh + Input Shaper (les deux derniers modules réels)

Date : 2026-08-15. Statut : validé par l'utilisateur (périmètre « Bed Mesh
carte + calibrer, Input Shaper ; Spoolman reste un stub faute de serveur »).

## Bed Mesh (remplace le stub ECRAN_BED_MESH)

### Données
- Nouvel objet d'abonnement WS : `"bed_mesh":null` (rpc_construire_
  abonnement ; tampon de requête relevé 512 → 640 par prudence, marge
  documentée).
- Champs consommés : `profile_name`, `mesh_min`, `mesh_max`,
  `probed_matrix` (matrice 2D de Z), `profiles` (noms, pour v2 -- v1 ne
  montre que l'actif).
- **JAMAIS dans etat_klipper_t** (une matrice 15×15 = ~900 o multipliés par
  toutes les copies/piles — la leçon des piles) : store dédié
  `bed_mesh_store.h/.c` (patron usb_fichiers : PSRAM paresseux, verrou
  court, génération). Bornes : `BED_MESH_MAX 15` par axe ; matrice plus
  grande = tronquée + drapeau (affiché).
- Parseur pur `bed_mesh_parse.c` host-testé : extrait les champs du JSON
  du sous-objet `bed_mesh` (présent dans l'instantané d'abonnement ET les
  notify_status_update). Branché dans le chemin WS existant, à côté de
  rpc_fusionner_instantane (même JSON, second consommateur ; jamais un
  second abonnement).

### Écran
- Carte de chaleur : grille de cellules LVGL N×M colorées (interpolation
  bleu → vert → rouge entre mesh_min et mesh_max ; cellule = lv_obj plein,
  pas de canvas), valeur min/max + profil actif affichés au-dessus.
  Aucune matrice (`nb_x/nb_y == 0`) : « No mesh - run calibrate ».
- Boutons : « Calibrate » (confirmation, destructif=false, texte rappelant
  que le plateau doit être référencé -- envoie `BED_MESH_CALIBRATE`) et
  « Clear » (envoie `BED_MESH_CLEAR`, sans confirmation : non destructif et
  réversible par re-calibrate... NON : avec confirmation quand même, il
  jette une mesure de plusieurs minutes).
- Rafraîchi par génération du store (canal externe habillage déjà en place :
  ajouter bed_mesh_generation() à la somme de app_main).

## Input Shaper (remplace le stub ECRAN_INPUT_SHAPER)

### Données
- Objet d'abonnement : `"input_shaper":null`.
- Champs : `shaper_type_x/y` (chaîne courte), `shaper_freq_x/y` (float).
- Petits (≈24 o) : intégrés à `etat_klipper_t` (dérogation documentée à la
  règle « ne pas grossir l'état » -- 24 o sur 1840, négligeable sur les
  piles, contrairement à la matrice du mesh), parsés dans
  rpc_fusionner_* comme les limites (nombre_fini, poison par champ).
- Types affichés/proposés : zv, mzv, zvd, ei, 2hump_ei, 3hump_ei.

### Écran
- Deux blocs X / Y : type courant + fréquence courante ; tap type →
  selecteur_choix (6 types) ; tap fréquence → clavier numérique (bornes
  10-150 Hz, hors bornes refusé/notifié).
- Application : `SET_INPUT_SHAPER SHAPER_TYPE_X=<t>` /
  `SHAPER_FREQ_X=<f>` (idem _Y) via le chemin gcode standard
  (ui_commander, patron ecran_retraction/limites). Volatile côté Klipper
  (perdu au redémarrage firmware) : mention à l'écran « not saved to
  config », comme KlipperScreen.
- Pas de test de résonance (accéléromètre requis) : hors périmètre.

## Menu / stubs
ECRAN_BED_MESH et ECRAN_INPUT_SHAPER sortent de STUBS() (ecran_stub.c) et
deviennent de vrais écrans ; ECRAN_SPOOLMAN reste stub. Le menu
Configuration ne change pas (symboles identiques).

## Tests
- Host : parseur bed_mesh (fixtures : matrice 3×3 réaliste, champs absents,
  matrice trop grande → troncature) ; store (bornes, génération) ;
  écrans via libellés (mesh vide, min/max affichés ; input shaper types).
- Matériel : sur la CR-10 (bed mesh classique) et la U1 ; calibrate réel.

## Hors périmètre
Profils multiples du mesh (charger/sauver par nom), vue 3D, test de
résonance, Spoolman (guide d'installation serveur fourni à part).
