# Écran Températures — Plan d'implémentation

> **Pour agents :** SOUS-SKILL REQUISE : superpowers:subagent-driven-development.

**But :** un panneau Températures dédié (KlipperScreen) qui rétablit le réglage
des consignes au tactile (tuiles réglables + préréglages), supprimé avec
l'ancien idle.

**Architecture :** nouvel `ECRAN_TEMPERATURES` dans le conteneur de nav 742px,
tuiles de chauffe adaptatives (paliers) tappables → clavier numérique, rangée
de préréglages PLA/PETG/ABS/Off. Logique reprise à l'identique de l'ancien
`ecran_accueil_idle.c` (git `ce47e3a`). Câblé depuis le hub.

**Pile :** C, LVGL 9.2, ESP-IDF ; host-test via WSL.

## Contraintes globales

- Contrat `ecran.h` : contexte alloué par le socle (`taille_contexte`), JAMAIS
  d'état d'instance en static de fichier (seuls des `const` de portée fichier).
- Règle C3 : sur `donnees_perimees`, GRISER les valeurs affichées ; jamais de
  boîte d'erreur côté écran.
- Réseau-libre dans les callbacks LVGL (`ui_commander` met en file, ne bloque
  pas ; jamais de HTTP/`vTaskDelay`).
- Bornes température [0, 350] °C ; noms de chauffeurs `extruder`/`extruderN`/
  `heater_bed` ; préréglages PLA 210/60, PETG 240/80, ABS 250/100, Off 0/0 ;
  chaque préréglage = 2 gcodes (buse active relue au clic + plateau).
- Thème sombre ; cibles tactiles ≥44px ; identifiants/commentaires en
  français ; aucune donnée personnelle.
- Widths dérivées de `LARGEUR_CONTENU 742` (les écrans vivent à droite du rail).

---

## Task 1 : Écran Températures (tuiles réglables + préréglages)

**Files :**
- Create : `firmware/main/apps/klipper/ecrans/ecran_temperatures.h`, `.c`
- Test : `host-test/tests/test_ecran_temperatures.c`
- Modify : `firmware/main/CMakeLists.txt`, `host-test/CMakeLists.txt`,
  `host-test/tests/main.c` (enregistrer la suite), `simulateur/CMakeLists.txt`.

**Interfaces :**
- Consumes : `etat_klipper_t` (`nb_extrudeurs`, `extrudeurs[i].{presente,
  actuelle,consigne}`, `outil_actif`, `plateau.{presente,actuelle,consigne}`),
  `klipper_paliers.h`, `ui_format_temperature`/`tuile.h`,
  `klipper_gcode_consigne_temp(sortie,taille,chauffeur,cible_c)`,
  `clavier_ouvrir(titre,valeur_initiale,CLAVIER_NUMERIQUE,rappel,ctx)`,
  `habillage_notifier`, `ui_commander(BACKEND_ACTION_GCODE, {"script":...})`.
- Produces : `extern const ecran_desc_t ECRAN_TEMPERATURES;` (id `"temperatures"`).

**Réutilisation :** reprendre à l'identique, depuis `git show
ce47e3a:firmware/main/apps/klipper/ecrans/ecran_accueil_idle.c`, la SECTION
températures : structures `..._cellule_info_t {est_plateau, indice_extrudeur,
consigne_courante, ctx}` et `..._preset_info_t {cible_buse, cible_plateau,
ctx}`, `nom_chauffeur_extrudeur`, `consigne_u16`, `cellule_info_nom_chauffeur`,
`cellule_clavier_rappel`, `cellule_bouton_cb`, `preset_bouton_cb`,
`construire_arguments_gcode`/`envoyer_gcode` (cJSON), et la construction +
`mettre_a_jour` des tuiles par palier (géométrie du hub `ecran_accueil_hub.c`).
NE PAS reprendre le jog/homing/sélecteurs. Le contexte stocke `outil_actif_connu`
(relu au clic pour les presets) et `cellule_infos[]`.

- [ ] **Step 1 — test qui échoue** (`test_ecran_temperatures.c`, modèle =
  ancien `test_ecran_accueil_idle.c` section temp, git `ce47e3a`) : empiler
  l'écran avec 2 extrudeurs + plateau ; taper une tuile buse → le clavier
  s'ouvre pré-rempli à la consigne courante ; valider "210" → `VERIFIER` que la
  dernière commande sim est `SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210` ;
  valider "999"/"abc" → aucune commande + notification ; taper préréglage PLA →
  2 commandes (`extruder` TARGET=210 puis `heater_bed` TARGET=60) ; grisage sur
  `donnees_perimees`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** `ecran_temperatures.{h,c}` en revivant la logique
  ci-dessus ; câbler les 4 CMake/registres.
- [ ] **Step 4 — lancer, voir passer** (host-test vert).
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): ecran Temperatures (consignes reglables + prereglages)"`.

---

## Task 2 : Câblage depuis le hub (case menu + tap tuile)

**Files :**
- Modify : `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.{c,h}`
- Test : `host-test/tests/test_ecran_accueil_hub.c` (étendre).

**Interfaces :**
- Consumes : `ECRAN_TEMPERATURES` (Task 1), `navigation_empiler`.

- [ ] **Step 1 — test qui échoue** (étendre `test_ecran_accueil_hub.c`) : cliquer
  la case menu `ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES` → `VERIFIER(profondeur==2 &&
  id=="temperatures")` ; dépiler ; cliquer une tuile de température → même
  résultat.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** : attacher un rappel de clic à
  `menu_boutons[ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES]` → `navigation_empiler(
  &ECRAN_TEMPERATURES)` (même idiome que `menu_deplacer_cb`) ; rendre chaque
  tuile `cellules[i].racine` cliquable → même navigation (pas de ciblage par
  chauffe requis : la case comme la tuile ouvrent simplement le panneau). Les 4
  autres cases restent no-op scopé.
- [ ] **Step 4 — lancer, voir passer** (host-test vert).
- [ ] **Step 5 — build firmware** (`idf.py build`) vert, aucun symbole non
  résolu.
- [ ] **Step 6 — commit** : `git commit -am "feat(klipper): hub -> ecran Temperatures (case menu + tap tuile)"`.

## Self-Review

- **Couverture spec** : tuiles réglables (T1), presets (T1), navigation case menu
  + tap tuile (T2), grisage C3 (T1). ✓
- **Placeholders** : aucun — la logique est reprise d'un fichier réel (git
  `ce47e3a`). ✓
- **Cohérence des types** : `ECRAN_TEMPERATURES`/id `"temperatures"` cohérent T1↔T2 ;
  `ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES` (=1) déjà défini dans le hub. ✓
