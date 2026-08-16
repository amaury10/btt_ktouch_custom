# Écran Extruder — Plan d'implémentation

> **Pour agents :** SOUS-SKILL REQUISE : superpowers:subagent-driven-development.

**But :** panneau Extruder (KlipperScreen) : extrude/rétracte du filament,
choisit l'outil actif sur machine multi-tête.

**Architecture :** nouvel `ECRAN_EXTRUDER` dans le conteneur nav 742px ;
sélecteur d'outil (si multi), tuile température buse active (lecture),
sélecteurs longueur + vitesse (`selecteur_choix`), boutons Extruder/Rétracter.
Deux nouvelles fonctions gcode pures. Câblé au hub.

**Pile :** C, LVGL 9.2, ESP-IDF ; host-test via WSL.

## Contraintes globales

- Contrat `ecran.h` : contexte alloué par le socle, JAMAIS de static
  d'instance. Règle C3 : griser les valeurs sur `donnees_perimees`, jamais de
  boîte d'erreur côté écran. Réseau-libre dans les callbacks LVGL.
- Noms chauffeurs `extruder`/`extruderN` (0 => `extruder`, pas `extruder0`).
- Longueurs 5/10/25/50 mm (défaut 10) ; vitesses 120/300/600 mm·min⁻¹
  (Lent/Moyen/Rapide, défaut Moyen) ; extrude = E positif, rétracte = E négatif.
- La garde à froid est **autoritative côté Klipper** (l'écran envoie, Klipper
  refuse si trop froid → notification existante) — pas de désactivation client.
- Thème sombre ; cibles ≥44px ; FR ; aucune donnée personnelle ;
  `LARGEUR_CONTENU 742`.

---

## Task 1 : gcode extrude + activer-outil (fonctions pures)

**Files :**
- Modify : `firmware/main/apps/klipper/klipper_gcode.h`, `.c`
- Test : `host-test/tests/test_klipper_gcode.c` (étendre).

**Interfaces :**
- Produces :
  - `bool klipper_gcode_extrude(char* sortie, size_t taille, float distance_mm, uint16_t vitesse_mm_min);`
  - `bool klipper_gcode_activer_outil(char* sortie, size_t taille, uint8_t indice);`

**Modèle :** `klipper_gcode_jog` (déjà dans le fichier) pour le format
SAVE/RESTORE + le formatage de distance à ≤2 décimales sans zéros superflus, et
`klipper_gcode_consigne_temp` pour le nommage `extruder`/`extruderN`.

- [ ] **Step 1 — tests qui échouent** (`test_klipper_gcode.c`) :
  `klipper_gcode_extrude(buf, n, 10.0f, 300)` → `SAVE_GCODE_STATE NAME=ktouch_extrude\nM83\nG1 E10 F300\nRESTORE_GCODE_STATE NAME=ktouch_extrude` ; `-10.0f` → `G1 E-10` ; distance 0/NaN/>200 ou vitesse 0/>6000 → `false` sans toucher `sortie` ; `klipper_gcode_activer_outil(buf,n,0)` → `ACTIVATE_EXTRUDER EXTRUDER=extruder` ; `(...,1)` → `EXTRUDER=extruder1` ; indice ≥ `KLIPPER_EXTRUDEURS_MAX` → `false`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** les deux fonctions dans `klipper_gcode.c`, déclarer dans le `.h` (avec le même style de commentaire que les fonctions voisines).
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): gcode extrude + activer-outil"`.

---

## Task 2 : Écran Extruder + câblage hub

**Files :**
- Create : `firmware/main/apps/klipper/ecrans/ecran_extruder.h`, `.c`
- Test : `host-test/tests/test_ecran_extruder.c`
- Modify : `firmware/main/CMakeLists.txt`, `host-test/CMakeLists.txt`,
  `host-test/tests/main.c`, `simulateur/CMakeLists.txt`,
  `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.c` (case menu [2]).

**Interfaces :**
- Consumes : `klipper_gcode_extrude`/`klipper_gcode_activer_outil` (T1),
  `selecteur_choix` (`selecteur_choix.h`), `tuile.h`/`ui_format_temperature`,
  `etat_klipper_t`, `ui_commander`, `navigation_empiler`.
- Produces : `extern const ecran_desc_t ECRAN_EXTRUDER;` (id `"extruder"`).

**Décision de périmètre V1 (contrôleur) :** la **sélection d'outil actif est
DIFFÉRÉE** à un suivi. Raison : `selecteur_choix` fige son nombre de boutons à
la création, alors que `nb_extrudeurs` n'est connu qu'à `mettre_a_jour`
(recréer le widget à chaud est fragile), et le multi-tête est spéculatif pour
les machines cibles (1-2 têtes) et intestable sur vkp (1 tête). V1 opère sur la
buse **active** rapportée par l'état ; `klipper_gcode_activer_outil` (T1) reste
disponible pour le suivi. La ligne d'état affiche « Actif : T\<outil_actif\> ».

- [ ] **Step 1 — test qui échoue** (`test_ecran_extruder.c`, modèle
  `test_ecran_deplacer.c`) : empiler avec 2 extrudeurs ; sélecteur longueur
  défaut 10, vitesse défaut Moyen ; clic Extruder → `SAVE.../M83/G1 E10 F300/RESTORE`
  (via `source_etat_sim_derniere_commande`) ; clic Rétracter → `G1 E-10` ;
  changer longueur=50 puis Extruder → `G1 E50` ; changer vitesse=Rapide puis
  Extruder → `F600` ; grisage sur `donnees_perimees` ; buse active affichée
  (nom + température, `Actif : T0`).
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** `ecran_extruder.{h,c}` (tuile temp buse active
  en lecture + ligne « Actif : T\<n\> », sélecteurs longueur/vitesse via
  `selecteur_choix`, boutons Extruder/Rétracter câblés au gcode T1 ;
  PAS de sélecteur d'outil en V1) ; câbler les 4 CMake/registres ; attacher un
  rappel à `menu_boutons[ECRAN_ACCUEIL_HUB_MENU_EXTRUDER]` du hub →
  `navigation_empiler(&ECRAN_EXTRUDER)` (même idiome que `ouvrir_temperatures_cb`),
  et retirer son sous-titre « A venir » (`MENU_DEFS[EXTRUDER].sous_titre = ""`)
  + l'assertion de test correspondante dans `test_ecran_accueil_hub.c`.
- [ ] **Step 4 — lancer, voir passer** (host-test vert).
- [ ] **Step 5 — build firmware** (`idf.py build`) vert.
- [ ] **Step 6 — commit** : `git commit -am "feat(klipper): ecran Extruder (extrude/retract + outil actif) + cablage hub"`.

## Self-Review

- **Couverture spec** : gcode extrude/outil (T1), écran extrude/retract +
  longueur/vitesse + outil (T2), câblage + retrait « A venir » (T2), C3 (T2). ✓
- **Placeholders** : aucun — valeurs et formats explicites. ✓
- **Cohérence des types** : `klipper_gcode_extrude`/`_activer_outil` signatures
  identiques T1↔T2 ; `ECRAN_EXTRUDER`/id `"extruder"` cohérent ;
  `ECRAN_ACCUEIL_HUB_MENU_EXTRUDER` (=2) déjà défini. ✓
