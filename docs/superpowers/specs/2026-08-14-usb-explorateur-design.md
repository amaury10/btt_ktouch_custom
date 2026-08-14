# Explorateur de fichiers USB (remplace le scan récursif complet)

Date : 2026-08-14 (fin de session diagnostic USB/RAM interne — voir la
mémoire du projet `usb-ram-interne-diagnostic`).
Statut : design validé par l'utilisateur (dialogue du 14/08 au soir).

## Problème

Le scan récursif complet de la clé (34–55 s mesurées sur une clé de 29 Go
bien remplie, même après le passage en `readdir` direct + publication au fil
de l'eau) reste trop lent : le coût est la lecture brute de TOUS les
répertoires de la clé via USB-MSC. L'utilisateur a demandé un explorateur :
naviguer dossier par dossier, un niveau à la fois — chaque niveau est une
seule énumération, quasi instantanée.

## Décisions de cadrage (validées)

- **Remplace** l'écran USB actuel (liste à plat) — pas de double mode.
- Filtre par dossier : **sous-dossiers + `.gcode` seulement** (filtre
  `usb_est_gcode()` inchangé). Un dossier sans rien de pertinent s'affiche
  vide mais reste navigable.
- Remontée : **entrée « .. » en tête de liste** hors racine (un bouton de la
  grille existante, pas de widget dédié).

## Architecture (réutilise l'infrastructure durcie le 14/08)

### Store `usb_fichiers` → état du répertoire courant

- `usb_fichier_t` : + `bool est_dossier` ; `chemin` porte le **chemin
  complet** de l'entrée (comme aujourd'hui, requis par l'upload), l'écran
  n'affiche que le nom (dernier segment).
- `usb_fichiers_t` : + `char chemin_courant[USB_FICHIER_CHEMIN_MAX]` ;
  `scan_en_cours` → sémantique « listage en cours » (nom conservé pour
  limiter le diff, commentaire mis à jour).
- `USB_FICHIERS_MAX` : 32 → **64** (borne PAR DOSSIER désormais ; ~9 Ko en
  PSRAM, store déjà en PSRAM). `tronques` = dossier plus fourni que 64.
- `usb_fichiers_publier_partiel()` : **supprimée** (sans objet, un listage
  est court). `usb_fichiers_definir()` gagne le paramètre `chemin_courant`.
- Nouvelle fonction pure host-testée `usb_listing_trier()` : dossiers
  d'abord, puis alphabétique insensible à la casse (`strcasecmp`), « .. »
  jamais trié (posé par l'écran, pas par le store — voir plus bas).

### Tâche de listage (ex-tâche de scan, `usb_scan.c`)

Conservés : création unique au démarrage paresseux (RAM saine), pile 8 Kio
en PSRAM (`xTaskCreateWithCaps`), réveil par sémaphore binaire (coalescence),
gardes d'éjection (re-check du montage avant/après publication), démarrage
paresseux à la première ouverture de l'écran.

Changements :
- Plus de récursion, plus de deux passes racine : UNE énumération du chemin
  demandé, filtre, `stat` sur les seuls `.gcode` retenus, tri, publication.
- **Demande de chemin** : tampon `s_chemin_demande[USB_FICHIER_CHEMIN_MAX]`
  protégé par le même patron portMUX que le store, écrit par
  `usb_scan_demander(chemin)` (appelée par l'écran et par le callback de
  montage avec « /usb »), lu par la tâche au réveil. Deux demandes
  rapprochées : la dernière gagne (écrasement + sémaphore déjà levé).
- `System Volume Information` toujours sauté ; entrées cachées (`.`)
  toujours sautées ; chemin trop long pour le store : entrée ignorée.

### Écran (`ecran_usb.c`)

- Grille 5/page + pagination + rangée de statut : inchangées.
- Rangée de statut (hors upload) : **chemin courant** (`LV_LABEL_LONG_DOT`
  si trop long) au lieu du texte vide ; les états SUCCES/ECHEC d'upload et
  la troncature gardent la priorité (mêmes règles qu'aujourd'hui).
- Libellés : nom seul ; dossiers préfixés `LV_SYMBOL_DIRECTORY " "`.
- « .. » : injectée par l'ÉCRAN en tête de sa copie locale quand
  `chemin_courant != "/usb"` — le store reste un miroir brut du répertoire.
- Tap dossier (ou « .. ») → `usb_scan_demander(chemin cible)` ; tap fichier
  → confirmation + upload (inchangé). Pendant un listage
  (`scan_en_cours`) : « Reading USB key... » seulement si la liste est
  vide ; grille non désactivée (un tap pendant un listage est licite, la
  dernière demande gagne).
- Retour à la page 0 à chaque changement de répertoire.

### Inchangé

Habillage/canal de génération externe, upload streamé (`usb_upload_http`),
BSP (`pandatouch_msc.c` n'est plus utilisé par le listage que pour
`pt_usb_is_mounted()`), écran « Insert a USB key » clé absente.

## Tests

- Host : contrat du store réécrit (chemin courant, `est_dossier`,
  troncature à 64) ; `usb_listing_trier()` (ordre dossiers/fichiers, casse,
  stabilité) ; écran si praticable via les labels LVGL (facultatif).
- Matériel (une passe au prochain flash) : racine en ~1 s, descente/remontée
  dans les dossiers, dossier > 64 entrées → « liste tronquée », éjection en
  cours de navigation → « Insert a USB key », upload d'un fichier en
  sous-dossier, noms accentués.

## Hors périmètre

Vue « tous les .gcode à plat », miniatures dans la liste, tri configurable,
suppression/renommage de fichiers.
