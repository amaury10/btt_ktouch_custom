# Écran Ventilateurs — Plan d'implémentation

> **Pour agents :** SOUS-SKILL REQUISE : superpowers:subagent-driven-development.

**But :** panneau Ventilateurs (KlipperScreen) : régler le ventilateur de pièce
par slider + préréglages + saisie numérique.

**Architecture :** `ECRAN_VENTILATEURS` dans le conteneur nav 742px. `lv_slider`
0-100 % (commit au relâcher), `selecteur_choix` de préréglages, saisie clavier.
Tout → `M106 S<0-255>`. Une nouvelle fonction gcode pure. Câblé au hub.

**Pile :** C, LVGL 9.2, ESP-IDF ; host-test via WSL.

## Contraintes globales

- Contrat `ecran.h` : contexte alloué par le socle, JAMAIS de static
  d'instance. C3 : griser sur `donnees_perimees`, jamais de boîte d'erreur
  côté écran. Réseau-libre dans les callbacks LVGL.
- Ventilateur de pièce = `ventilateurs[0]` (seul mappé par le backend).
- `M106 S<val>`, `val = round(pct×255/100)`, pct ∈ [0,100] ; 0 → `S0`.
- **Slider : gcode envoyé UNIQUEMENT au `LV_EVENT_RELEASED`** ; `VALUE_CHANGED`
  ne met à jour QUE le label (aucun gcode) — anti-flood Klipper.
- Saisie bornée [0,100] ; hors borne/non numérique → notification, aucun gcode.
- Thème sombre ; cibles ≥44px ; FR ; aucune donnée personnelle ;
  `LARGEUR_CONTENU 742`.

---

## Task 1 : gcode ventilateur (fonction pure)

**Files :**
- Modify : `firmware/main/apps/klipper/klipper_gcode.h`, `.c`
- Test : `host-test/tests/test_klipper_gcode.c` (étendre).

**Interfaces :**
- Produces : `bool klipper_gcode_ventilateur(char* sortie, size_t taille, uint8_t pct);`

**Modèle :** `klipper_gcode_arret_urgence` (commande simple sans SAVE/RESTORE)
et `klipper_gcode_home` (formatage d'un entier) dans le même fichier.

- [ ] **Step 1 — tests qui échouent** (`test_klipper_gcode.c`) :
  `klipper_gcode_ventilateur(buf, n, 50)` → `"M106 S128"` (round(127.5)=128) ;
  `100` → `"M106 S255"` ; `0` → `"M106 S0"` ; `25` → `"M106 S64"` (round(63.75)) ;
  `101` → `false` sans toucher `sortie` ; tampon trop court → `false`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** dans `klipper_gcode.c` (arrondi entier :
  `(pct * 255 + 50) / 100`), déclarer dans le `.h` avec commentaire au style du
  fichier.
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): gcode ventilateur (M106)"`.

---

## Task 2 : Écran Ventilateurs + câblage hub

**Files :**
- Create : `firmware/main/apps/klipper/ecrans/ecran_ventilateurs.h`, `.c`
- Test : `host-test/tests/test_ecran_ventilateurs.c`
- Modify : `firmware/main/CMakeLists.txt`, `host-test/CMakeLists.txt`,
  `host-test/tests/main.c`, `simulateur/CMakeLists.txt`,
  `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.c` (case menu [3]).

**Interfaces :**
- Consumes : `klipper_gcode_ventilateur` (T1), `selecteur_choix.h`, `clavier.h`,
  `etat_klipper_t` (`ventilateurs[0].vitesse`), `habillage_notifier`,
  `ui_commander`, `navigation_empiler`.
- Produces : `extern const ecran_desc_t ECRAN_VENTILATEURS;` (id `"ventilateurs"`).

- [ ] **Step 1 — test qui échoue** (`test_ecran_ventilateurs.c`, modèles
  `test_ecran_deplacer.c` + `test_ecran_temperatures.c`) : empiler ; le slider
  existe ; `lv_slider_set_value(slider, 50, LV_ANIM_OFF)` puis
  `lv_obj_send_event(slider, LV_EVENT_RELEASED, NULL)` → `M106 S128`
  (`source_etat_sim_derniere_commande`) ; un `LV_EVENT_VALUE_CHANGED` seul
  n'émet RIEN (comparer `source_etat_sim_file_taille` avant/après) ; préréglage
  100 → `M106 S255` ; saisie clavier "25" → `M106 S64`, "150"/"x" → aucun gcode
  + notification ; `mettre_a_jour` avec `ventilateurs[0].vitesse=0.5` recale le
  slider à 50 ; grisage sur `donnees_perimees`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** `ecran_ventilateurs.{h,c}` : `lv_slider` 0-100
  (rappel `LV_EVENT_RELEASED` → gcode, `LV_EVENT_VALUE_CHANGED` → label seul),
  `selecteur_choix` de préréglages {Off,25,50,75,100} (→ gcode au choix),
  champ % tappable → clavier (borné [0,100] → gcode), label vitesse courante ;
  `mettre_a_jour` recale slider+label sur `ventilateurs[0].vitesse` et grise sur
  `donnees_perimees`. Câbler les 4 CMake/registres ; attacher un rappel à
  `menu_boutons[ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS]` du hub →
  `navigation_empiler(&ECRAN_VENTILATEURS)` (idiome `ouvrir_temperatures_cb`) ;
  passer `MENU_DEFS[VENTILATEURS].sous_titre` à `""` + commentaire à jour +
  l'assertion de test `test_ecran_accueil_hub.c` (`"Ventilateurs\nA venir"` →
  `"Ventilateurs"`) + test de navigation.
- [ ] **Step 4 — lancer, voir passer** (host-test vert).
- [ ] **Step 5 — build firmware** (`idf.py build`) vert.
- [ ] **Step 6 — commit** : `git commit -am "feat(klipper): ecran Ventilateurs (slider + prereglages + saisie) + cablage hub"`.

## Self-Review

- **Couverture spec** : gcode M106 (T1), slider/préréglages/saisie (T2),
  anti-flood au relâcher (T2), recalage `mettre_a_jour` (T2), câblage +
  « A venir » (T2), C3 (T2). ✓
- **Placeholders** : aucun — formats/valeurs explicites. ✓
- **Cohérence des types** : `klipper_gcode_ventilateur` T1↔T2 ;
  `ECRAN_VENTILATEURS`/id `"ventilateurs"` ;
  `ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS` (=3) déjà défini. ✓
