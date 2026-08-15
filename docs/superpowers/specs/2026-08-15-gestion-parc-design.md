# Gestion de parc d'imprimantes (tableau de bord séquentiel)

Date : 2026-08-15. Statut : design validé par l'utilisateur (dialogue du
15/08 au matin — « le 2 mais pas simultané : séquentiel, timeout court »).
Dernière feature du backlog post-refonte (voir la mémoire du projet).

## Problème

La dalle ne connaît qu'un hôte Moonraker (Configuration → une adresse, NVS).
L'utilisateur a deux machines réelles (CR-10 S5, Snapmaker U1) et veut les
voir et basculer de l'une à l'autre depuis l'écran.

## Décisions de cadrage (validées)

- **Vue d'ensemble du parc, interrogation SÉQUENTIELLE** : jamais plus d'une
  connexion d'interrogation à la fois (la RAM interne interdit N clients WS
  simultanés — leçon des sessions des 14-15/08). Timeout **court : 1,5 s**
  par hôte (l'utilisateur a explicitement refusé les longs timeouts).
- **Une imprimante ACTIVE** à la fois : WS + HTTP existants, inchangés pour
  tout le reste de l'UI. La bascule réutilise le chemin « changement
  d'adresse » existant.
- **6 emplacements max** (nom + adresse:port, NVS), actif mémorisé.

## Architecture

### Store du parc (`parc_imprimantes.h/.c`, nouveau, host-testé)

- Config : `parc_entree_t { char nom[24]; char hote[64]; }` × 6, nombre
  d'entrées, indice de l'active — chargé/écrit NVS via le module réglages
  existant (mêmes idiomes que l'hôte actuel, qui DEVIENT l'entrée 0 à la
  migration : au premier boot avec parc vide, l'hôte historique est recopié
  en entrée 0 « Printer 1 », rien ne casse).
- État interrogé : `parc_etat_t { atteignable, etat[16] (idle/printing/...),
  buse, lit, progression_pct, age_ms }` × 6, écrit par l'interrogateur, lu
  par l'écran — verrou court + génération, patron usb_fichiers/klipper_
  fichiers. Instance en PSRAM (allocation paresseuse, patron console_log).

### Interrogateur séquentiel (`parc_sonde.h/.c`, ESP-only)

- Tâche pérenne créée paresseusement à la PREMIÈRE ouverture de l'écran
  Parc (patron usb_scan : pile en PSRAM via xTaskCreateWithCaps, réveil par
  sémaphore, jamais de création au pire moment).
- Actif seulement écran Parc ouvert : l'écran arme (`parc_sonde_activer(true)`)
  à construire(), désarme à détruire(). Désarmée = la tâche dort.
- Un TOUR = une seule imprimante non-active interrogée (HTTP GET
  `/printer/objects/query?...extruder,heater_bed,print_stats,display_status`,
  client esp_http_client dédié éphémère, timeout 1500 ms, tampon de réponse
  en PSRAM), parse minimal (cJSON, champs de parc_etat_t seulement — PAS le
  parseur etat_klipper_t complet), publication, puis 1 s de pause avant
  l'imprimante suivante, round-robin. Hôte vide/malformé : sauté.
- L'imprimante ACTIVE n'est jamais sondée : son état vient du store temps
  réel existant (l'écran le lit directement).

### Écran Parc (`ecran_parc.h/.c`)

- Accès : case « Printers » dans le sous-menu Configuration (emplacements
  libres, idiome X-macro existant).
- Une tuile par entrée configurée : nom, pastille d'état (couleurs liaison
  existantes ; « ? » grisé si timeout/jamais sondée), buse/lit, % si
  impression. L'active marquée (bordure accent). Rafraîchi par génération
  (parc + store temps réel pour l'active).
- Tap sur une tuile non active → `confirmation_ouvrir("Switch printer?",
  nom, "Switch", destructif=false)` → écrit l'actif en NVS + déclenche le
  chemin de reconnexion existant (celui du changement d'adresse de
  Configuration). Tap sur l'active : rien.
- Écran Configuration : passe à la liste des 6 (nom + adresse par ligne,
  clavier existant), l'ancienne saisie unique devient l'édition de
  l'entrée 0. Périmètre minimal : ajouter/éditer ; la suppression = vider
  l'adresse.

## Contraintes RAM (héritées des diagnostics 14-15/08)

Aucun tampon > 256 o sur une pile ; scratchs/tampons/store en PSRAM ;
aucune allocation par tour au-delà du client HTTP éphémère ; surveiller
`/status.heap_interne` à la validation.

## Tests

- Host : store du parc (config + états + générations), parseur de la
  réponse légère (fixtures JSON Moonraker), migration entrée 0.
- Matériel : deux machines réelles, bascule dans les deux sens, débranchée
  = tuile « injoignable » en ≤1,5 s sans geler l'écran, heap_interne stable.

## Hors périmètre

Connexions simultanées, notifications croisées (« la CR-10 a fini »),
actions sur une imprimante non active, renommage riche.
