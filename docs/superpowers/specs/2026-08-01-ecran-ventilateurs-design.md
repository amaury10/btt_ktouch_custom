# Écran Ventilateurs (panneau KlipperScreen) — sous-projet 4

Date : 2026-08-01
Statut : design (validé avec l'utilisateur : préréglages + saisie numérique +
slider ; suit le découpage KlipperScreen).

## 1. Contexte & objectif

Suite du découpage KlipperScreen. Panneau **Ventilateurs** : régler la vitesse
du ventilateur de refroidissement pièce. Atteint par la case menu
`ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS` (index 3) du hub.

Contrainte de l'état actuel (vérifiée) : le backend ne suit QUE le ventilateur
de pièce `fan` → `ventilateurs[0]` (`moonraker_rpc.c:514`). Les `heater_fan`/
`controller_fan` sont auto-gérés par Klipper (non pilotables, KlipperScreen non
plus). Ce panneau contrôle donc **le seul ventilateur de pièce**.

## 2. Architecture / navigation

- Nouvel `ECRAN_VENTILATEURS` (id `"ventilateurs"`), empilé par la case menu.
  Conteneur nav 742px, contrat `ecran.h`, C3, réseau-libre.

## 3. Composants (742×436, thème sombre)

Trois moyens de régler la MÊME valeur (0-100 %), tous → `M106` :

### 3.1 Slider (contrôle principal)
`lv_slider` horizontal 0-100 %, natif LVGL (pas de wrapper, YAGNI ; usage
unique). **Le gcode n'est envoyé qu'au relâcher** (`LV_EVENT_RELEASED`), jamais
à chaque pas de drag (sinon on inonde Klipper). Un label `NN %` suit la valeur
en direct pendant le drag (`LV_EVENT_VALUE_CHANGED`, purement visuel).

### 3.2 Préréglages
`selecteur_choix` : **Off / 25 / 50 / 75 / 100** (défaut sans présélection
imposée). Un choix envoie `M106` à ce pourcentage.

### 3.3 Saisie numérique
Un champ/bouton `NN %` tappable → `clavier_ouvrir(..., CLAVIER_NUMERIQUE, ...)`
pré-rempli à la valeur courante ; saisie bornée [0, 100] (hors borne / non
numérique → notification, aucun gcode), puis `M106`.

## 4. Flux de données

- **Klipper → UI** : `ventilateurs[0].vitesse` (0.0-1.0) → affiché en % ;
  la position du slider et le label sont recalés à `mettre_a_jour`. Périmé →
  grisé (slider + labels).
- **Réglage** (slider relâché / préréglage / saisie) : `M106 S<0-255>` où
  `S = round(pct × 255 / 100)` → `ui_commander(BACKEND_ACTION_GCODE, ...)`.
  `pct = 0` → `S0` (ventilateur éteint).

## 5. Nouveau gcode (fonction pure)

`bool klipper_gcode_ventilateur(char* sortie, size_t taille, uint8_t pct)` :
`M106 S<valeur>` avec `valeur = (pct × 255 + 50) / 100` (arrondi). `pct > 100`
→ `false` sans toucher `sortie`. `pct = 0` → `M106 S0`. (M106 S0 éteint le
ventilateur, équivalent M107 ; un seul format pour un seul chemin de test.)

## 6. Réutilisation

`selecteur_choix` (préréglages), `clavier` + parsing borné (comme
`ecran_temperatures.c`), l'idiome `construire_arguments_gcode`/`envoyer_gcode`
(cJSON), l'ossature d'écran (`ecran_deplacer.c`/`ecran_temperatures.c`).

## 7. Tests (host-test)

Gcode pur (`test_klipper_gcode.c`) : `klipper_gcode_ventilateur(50)` →
`M106 S128` (round(127.5)=128) ; `100` → `M106 S255` ; `0` → `M106 S0` ;
`101` → `false`. Écran (`test_ecran_ventilateurs.c`) : slider à 50 % +
`LV_EVENT_RELEASED` → `M106 S128` ; un drag SANS relâcher (`VALUE_CHANGED`
seul) N'envoie RIEN ; préréglage 100 → `M106 S255` ; saisie "25" → `M106 S64`,
saisie "150"/"x" → aucun gcode + notification ; `mettre_a_jour` recale le
slider sur `ventilateurs[0].vitesse` ; grisage C3 ; navigation depuis le hub
(case menu → profondeur 2, id `"ventilateurs"`).

## 8. Périmètre & hors-scope

- **Dans** : `ECRAN_VENTILATEURS` (slider + préréglages + saisie, ventilateur
  pièce), gcode `M106`, câblage case menu Ventilateurs.
- **Hors** : ventilateurs nommés `[fan_generic]` (le backend ne les suit pas —
  évolution backend séparée) ; Imprimer (job_status), panneau suivant.
