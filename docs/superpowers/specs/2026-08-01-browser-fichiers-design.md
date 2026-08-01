# Navigateur de fichiers gcode (démarrer une impression) — sous-projet 6

Date : 2026-08-01
Statut : design (autonomie, demande utilisateur « browser de fichier sur USB » ;
suit KlipperScreen ; à relire — touche le backend matériel-critique).

## 1. Contexte & objectif

Permettre de **démarrer une impression depuis l'écran** en choisissant un
fichier gcode local (y compris sur clé USB — sur une config Klipper typique la
clé est montée dans le dossier `gcodes`, donc les fichiers USB apparaissent dans
la même liste `server.files.list`). C'est le bouton « Print » de KlipperScreen.

Vérifié en direct contre un vrai Moonraker (vkp) :
- `GET/WS server.files.list?root=gcodes` → tableau PLAT (récursif) d'objets
  `{"path":"nom.gcode","modified":...,"size":...}` (un seul appel liste tout,
  sous-dossiers inclus dans `path` — pas de navigation de dossiers à coder).
- Démarrage : `SDCARD_PRINT_FILE FILENAME=<path>` (gcode Klipper standard, passe
  par le chemin gcode déjà validé) — pas de nouvelle méthode backend requise.

## 2. Architecture

Le backend calque EXACTEMENT le mécanisme des **macros** (déjà en place, testé) :
requête WS corrélée par id → parseur pur → remplace un tableau dans l'état →
déclenchement à la demande. L'UI calque l'**écran Macros** (`ecran_macros.c`),
déjà une grille paginée de noms → tap → action.

## 3. Composants

### 3.1 État (`etat_klipper.h`)
- `char fichiers[KLIPPER_FICHIERS_MAX][KLIPPER_FICHIER_NOM_MAX]` (32 × 64),
  `uint8_t nb_fichiers`, `bool fichiers_tronques`. (~2 Ko ajoutés à l'état,
  même approche que `macros[]`.) Un chemin ≥ `..._NOM_MAX` est IGNORÉ (jamais
  tronqué — un chemin tronqué désignerait un autre fichier, même leçon que
  `rpc_lire_macros`). Au-delà de `..._FICHIERS_MAX`, `fichiers_tronques=true`.

### 3.2 Parseur pur (`moonraker_rpc.{h,c}`)
- `bool rpc_lire_fichiers(etat_klipper_t *etat, const char *json, size_t longueur)` :
  extrait `result` (tableau d'objets), prend `.path` de chacun, REMPLACE
  entièrement `fichiers`/`nb_fichiers`/`fichiers_tronques`. Rend false et ne
  touche RIEN si JSON illisible ou `result` absent/pas un tableau. Modèle exact :
  `rpc_lire_macros`.

### 3.3 Requête WS (`moonraker_ws.c`)
- `g_id_fichiers`, `envoyer_requete_fichiers()` : `rpc_construire_requete(...,
  "server.files.list", "{\"root\":\"gcodes\"}")`. Corrélation dans le handler de
  réponse → `rpc_lire_fichiers`.
- **Récupérée au connect**, EXACTEMENT comme les macros (dans la séquence
  `envoyer_identify_et_abonnement()` après la requête macros, et re-demandée à
  chaque (re)connexion) — PAS de nouvelle action backend ni de drapeau à la
  demande : bien plus sûr, mécanisme déjà éprouvé. Limitation assumée : la liste
  est rafraîchie au (re)connect, pas en direct si un fichier est uploadé pendant
  que l'écran est ouvert (le browser affiche l'instantané du dernier connect ;
  un rafraîchissement live — `notify_filelist_changed` ou re-requête à
  l'ouverture — est un suivi possible, hors MVP).
- `web.c` expose `fichiers`/`nb_fichiers`/`fichiers_tronques` dans `/state`
  (comme `macros`), pour le diagnostic.

### 3.4 Écran browser (`ecran_fichiers.{h,c}`)
Modèle : `ecran_macros.c`. Grille paginée de noms de fichiers (742 px, thème
sombre). `mettre_a_jour` : peuple la grille depuis `etat->fichiers[]`, grise sur
`donnees_perimees`. Un tap sur un fichier → **confirmation** (« Print
\<nom\> ? », destructif=false) → `SDCARD_PRINT_FILE FILENAME=<path>` via
`ui_commander(BACKEND_ACTION_GCODE, {"script":...})`. Liste vide → message
« Aucun fichier » (pas une erreur — la liste est celle du dernier connect).

### 3.5 Câblage hub
La case menu **Imprimer** (index 4) est RECÂBLÉE : `ECRAN_ACCUEIL` (statut) →
`ECRAN_FICHIERS` (browser). Sémantique KlipperScreen : « Imprimer » démarre une
impression ; le STATUT s'affiche automatiquement (bascule vivante, sous-projet 5)
quand un job tourne. (`ECRAN_ACCUEIL` reste atteint par la bascule auto.)

## 4. Nouveau gcode

`bool klipper_gcode_imprimer_fichier(char* sortie, size_t taille, const char* nom)` :
`SDCARD_PRINT_FILE FILENAME=<nom>`. `nom` non NULL/non vide, ≤ borne ; les
caractères sont recopiés tels quels DANS le gcode — le nom vient de la liste
Moonraker (déjà un chemin de fichier valide), mais borne la longueur et rejette
un nom vide.

## 5. Gestion d'erreurs

`SDCARD_PRINT_FILE` sur un fichier inexistant/pendant une impression → Klipper
rejette → notification (chemin `ui_commande_echec` existant). Liste périmée →
grisée. Backend hors ligne → pas de liste (grisée), pas de plantage.

## 6. Tests (host-test)

- Parseur (`test_moonraker_rpc.c`, JSON écrits main comme pour les macros) :
  `rpc_lire_fichiers` sur une réponse `server.files.list` RÉELLE (forme captée
  sur vkp) → bons noms/compte ; `result` absent → false, état intact ; > MAX →
  `fichiers_tronques` ; nom trop long → ignoré.
- gcode (`test_klipper_gcode.c`) : `imprimer_fichier("a.gcode")` →
  `"SDCARD_PRINT_FILE FILENAME=a.gcode"` ; nom vide/NULL → false.
- Écran (`test_ecran_fichiers.c`, modèle `test_ecran_macros.c`) : peuple la
  grille depuis un état simulé ; tap → confirmation → `SDCARD_PRINT_FILE` émis
  (via `source_etat_sim_derniere_commande`) ; liste vide → message « Aucun
  fichier » ; grisage.
- Hub : case Imprimer → empile `ECRAN_FICHIERS` (id `"fichiers"`).

## 7. Périmètre & hors-scope

- **Dans** : liste plate des gcodes (USB inclus via montage `gcodes`),
  démarrage par tap+confirmation.
- **Hors** : navigation de sous-dossiers dédiée (les chemins sont affichés à
  plat), miniatures, tri/recherche, suppression de fichiers, upload depuis
  l'écran. Démarrer suffit pour le besoin exprimé.
