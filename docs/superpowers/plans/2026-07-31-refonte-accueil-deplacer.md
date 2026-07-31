# Refonte Accueil-hub + Déplacer + rail — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** restructurer l'accueil idle façon KlipperScreen — un rail d'accès rapide persistant à gauche, un accueil-hub (températures multitête + grille de menu), et un écran Déplacer dédié (jog gros + pas + vitesse + homing).

**Architecture :** l'écran racine LVGL est découpé en `[rail | conteneur-nav]`. Le rail (widget persistant) vit sur la racine, hors de la pile de navigation ; `navigation_init()` cible le conteneur de droite. Chaque écran (`ecran_desc_t`) est empilé/dépilé dans ce conteneur. Le rail déclenche des actions globales (Accueil, Home All, Macros, STOP). Le jog/home passent par les fonctions gcode pures existantes (`klipper_gcode_jog/home`) relayées à `ui_commander(BACKEND_ACTION_GCODE, …)`.

**Tech Stack :** C, ESP-IDF 5.5, LVGL 9.2, host-test (LVGL simulateur, macros `VERIFIER`).

## Global Constraints

- Aucun appel HTTP, aucun blocage, aucun `vTaskDelay` dans un rappel LVGL (contrainte du jalon, voir `ecran.h`).
- Un écran remplit `ecran_desc_t` (`id`, `titre`, `taille_contexte`, `construire`, `mettre_a_jour`, `detruire`). L'état par instance vit dans le `contexte` alloué par le socle — **jamais** dans une variable statique de fichier.
- Un widget = struct à champs publics, vivant dans le contexte de l'écran appelant, **jamais auto-alloué** (modèle `selecteur_pas_t`/`tuile_t`).
- gcode uniquement via les fonctions pures `klipper_gcode_*` (`KLIPPER_GCODE_MAX`=160) → `ui_commander(BACKEND_ACTION_GCODE, {"script":…})`. Chaque fonction rend `false` sans toucher la sortie si argument invalide.
- Cible tactile ≥ 44 px. Thème sombre : palette `COULEUR_*` de `ecran_accueil.c` (FOND 0x10161D, BOUTON 0x2A3644, TEXTE_BOUTON 0xFFFFFF, TEXTE_SECONDAIRE 0xC9D1D9, BORDURE, FOND_CELLULE).
- Multitête : jusqu'à 8 extrudeurs + plateau ; tuiles adaptatives (réutiliser les paliers `klipper_paliers.h`).
- Feedrates jog (mm·min⁻¹) : XY **600 / 3000 / 6000**, Z **300 / 600 / 1200**.
- TDD : test hôte AVANT implémentation ; assertions gcode via `source_etat_sim_derniere_commande(...)` ; construction/interaction LVGL sur le modèle de `host-test/tests/test_ecran_accueil_idle.c` et `test_clavier.c` (helpers `enfant_de_classe`, `lv_obj_send_event`, `VERIFIER`, `VERIFIER_TEXTE`).
- Commits fréquents (un par tâche minimum). Host-test vert à chaque commit.

---

## File Structure

- `firmware/main/ui/widgets/selecteur_choix.{h,c}` — **créer** : sélecteur générique à N boutons exclusifs (libellés arbitraires). Généralise l'idée de `selecteur_pas` ; utilisé pour Pas ET Vitesse. `selecteur_pas` reste en place pour l'ancien écran jusqu'à son retrait.
- `firmware/main/ui/widgets/rail.{h,c}` — **créer** : rail d'accès rapide persistant (4 boutons + état actif + callbacks).
- `firmware/main/apps/klipper/ecrans/ecran_deplacer.{h,c}` — **créer** : écran Déplacer (`ecran_desc_t`).
- `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.{h,c}` — **créer** : accueil-hub (`ecran_desc_t`).
- `firmware/main/apps/klipper/klipper_gcode.{h,c}` — **modifier** : ajouter `klipper_gcode_arret_urgence()` (M112, pur).
- `firmware/main/ui/habillage.c` (ou le point d'entrée UI actuel) — **modifier** : construire le layout racine `[rail | conteneur-nav]`, `navigation_init()` sur le conteneur-nav, empiler `ecran_accueil_hub` au lieu de `ecran_accueil_idle`, câbler les actions du rail.
- `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.{h,c}` — **retirer** en fin de parcours (remplacé par le hub + Déplacer ; son code jog/home/pas est repris dans les nouveaux écrans, pas jeté).
- Tests : `host-test/tests/test_selecteur_choix.c`, `test_rail.c`, `test_ecran_deplacer.c`, `test_ecran_accueil_hub.c`. `test_ecran_accueil_idle.c` retiré avec l'écran.

Ordre de dépendance : selecteur_choix → gcode M112 → rail → Déplacer → hub → intégration racine → retrait de l'ancien idle.

---

## Task 1 : Widget sélecteur générique `selecteur_choix`

**Files :**
- Create : `firmware/main/ui/widgets/selecteur_choix.h`, `firmware/main/ui/widgets/selecteur_choix.c`
- Test : `host-test/tests/test_selecteur_choix.c`

**Interfaces :**
- Produces :
  ```c
  typedef struct { lv_obj_t *racine; lv_obj_t *boutons[8]; uint8_t nb; uint8_t index_actif; } selecteur_choix_t;
  /* Crée racine (FLEX_ROW) + `nb` boutons (2..8) aux libellés `libelles[i]`,
     exclusifs, thème sombre. index_actif = defaut (borné à nb-1). s/parent/
     libelles NULL ou nb hors [2,8] -> ne fait rien. */
  void selecteur_choix_creer(selecteur_choix_t *s, lv_obj_t *parent,
                             const char *const *libelles, uint8_t nb, uint8_t defaut);
  uint8_t selecteur_choix_index(const selecteur_choix_t *s); /* index_actif ; 0 si s NULL */
  ```

- [ ] **Step 1 — test qui échoue** (`test_selecteur_choix.c`) : créer un sélecteur à 3 libellés {"Lent","Moyen","Rapide"} défaut 1 ; `VERIFIER(s.nb == 3)`, `VERIFIER(selecteur_choix_index(&s) == 1)`, `VERIFIER(lv_obj_get_child_count(s.racine) == 3)`. Simuler un clic sur le bouton 2 (`lv_obj_send_event(s.boutons[2], LV_EVENT_CLICKED, NULL)`) → `VERIFIER(selecteur_choix_index(&s) == 2)`. Bornes : `nb=1` → racine reste NULL / no-op.
- [ ] **Step 2 — lancer, voir échouer** : `wsl -d Debian -- sh "/mnt/e/Dev/BTT KTouch Custom/host-test/run.sh"` → FAIL (symbole non défini).
- [ ] **Step 3 — implémenter** `selecteur_choix.c` : racine flex-row, boucle de création des `nb` boutons (style sombre = COULEUR_BOUTON, sélectionné = accent bleu, même idiome que `selecteur_pas.c`), event cb qui met `index_actif` et rafraîchit le surlignage. Header avec le contrat ci-dessus.
- [ ] **Step 4 — lancer, voir passer** : host-test → 0 échec pour ces tests.
- [ ] **Step 5 — commit** : `git add firmware/main/ui/widgets/selecteur_choix.* host-test/tests/test_selecteur_choix.c && git commit -m "feat(ui): widget selecteur_choix (N boutons exclusifs)"`

---

## Task 2 : gcode arrêt d'urgence (M112)

**Files :**
- Modify : `firmware/main/apps/klipper/klipper_gcode.h`, `firmware/main/apps/klipper/klipper_gcode.c`
- Test : `host-test/tests/test_klipper_gcode.c` (fichier existant — ajouter un cas)

**Interfaces :**
- Produces : `bool klipper_gcode_arret_urgence(char *sortie, size_t taille); /* écrit "M112" ; false si tampon trop court */`

- [ ] **Step 1 — test qui échoue** : `char b[KLIPPER_GCODE_MAX]; VERIFIER(klipper_gcode_arret_urgence(b, sizeof b)); VERIFIER_TEXTE(b, "M112");` + `VERIFIER(!klipper_gcode_arret_urgence(b, 2));` (tampon trop court, sortie inchangée).
- [ ] **Step 2 — lancer, voir échouer** : host-test → FAIL.
- [ ] **Step 3 — implémenter** : dans `klipper_gcode.c`, écrire "M112" via le même garde de longueur que les autres fonctions (jamais de troncature silencieuse).
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): gcode M112 (arret d'urgence)"`

---

## Task 3 : Widget rail persistant

**Files :**
- Create : `firmware/main/ui/widgets/rail.h`, `firmware/main/ui/widgets/rail.c`
- Test : `host-test/tests/test_rail.c`

**Interfaces :**
- Consumes : rien (widget autonome).
- Produces :
  ```c
  typedef enum { RAIL_ACCUEIL, RAIL_HOME, RAIL_MACROS, RAIL_STOP, RAIL_NB } rail_action_t;
  typedef struct { lv_obj_t *racine; lv_obj_t *boutons[RAIL_NB]; void (*sur_action)(rail_action_t, void*); void *ctx; } rail_t;
  /* Crée le rail (colonne ~58px) dans `parent` ; STOP en bas (rouge). Un clic
     appelle `sur_action(action, ctx)`. */
  void rail_creer(rail_t *r, lv_obj_t *parent, void (*sur_action)(rail_action_t, void*), void *ctx);
  void rail_marquer_actif(rail_t *r, rail_action_t action); /* surligne (ou RAIL_NB pour aucun) */
  ```

- [ ] **Step 1 — test qui échoue** (`test_rail.c`) : callback de trace `static rail_action_t g_dernier; static int g_appels; static void trace(rail_action_t a, void*){g_dernier=a;g_appels++;}`. Créer le rail, `VERIFIER(lv_obj_get_child_count(r.racine) == RAIL_NB)`. Cliquer `r.boutons[RAIL_STOP]` → `VERIFIER(g_appels==1 && g_dernier==RAIL_STOP)`. Cliquer `RAIL_ACCUEIL` → `VERIFIER(g_dernier==RAIL_ACCUEIL)`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** `rail.c` : colonne flex, 4 boutons (icône+libellé, style sombre ; STOP = rouge `0xE5484D`, `margin-top:auto`), event cb commun qui dispatch via `sur_action`. `rail_marquer_actif` change le fond du bouton visé.
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(ui): widget rail d'acces rapide persistant"`

---

## Task 4 : Écran Déplacer

**Files :**
- Create : `firmware/main/apps/klipper/ecrans/ecran_deplacer.h`, `.c`
- Test : `host-test/tests/test_ecran_deplacer.c`

**Interfaces :**
- Consumes : `selecteur_choix_*` (Task 1), `klipper_gcode_jog/home` (existant), `ui_commander` (existant).
- Produces : `extern const ecran_desc_t ECRAN_DEPLACER;` + `#define ECRAN_DEPLACER_JOG_NB 6` (ordre X-/X+/Y-/Y+/Z+/Z-).

Constantes de vitesse (dans le .c) : `static const uint16_t VITESSE_XY[3]={600,3000,6000}; static const uint16_t VITESSE_Z[3]={300,600,1200};`

- [ ] **Step 1 — test qui échoue** (`test_ecran_deplacer.c`, modèle = `test_ecran_accueil_idle.c` §jog) : empiler `ECRAN_DEPLACER`, récupérer le conteneur, `VERIFIER` présence de 6 boutons jog + le sélecteur Pas + le sélecteur Vitesse + la rangée Home. Sélectionner Pas=10, Vitesse=Rapide, cliquer `X+` → `VERIFIER(source_etat_sim_derniere_commande(...))` contient `SAVE_GCODE_STATE`… `G1 X10 F6000`. Cliquer Home `Y` → commande contient `G28 Y`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** `ecran_deplacer.c` : `construire` bâtit la croix jog (6 boutons, positions fixes, gros), la colonne Z, `selecteur_choix` Pas {0.1,1,10,100} défaut 1, `selecteur_choix` Vitesse {Lent,Moyen,Rapide} défaut 1, rangée Home (All/X/Y/Z). Callbacks jog : lire pas (`SELECTEUR_PAS_MM`-équivalent depuis l'index) + vitesse (VITESSE_XY/Z selon l'axe), appeler `klipper_gcode_jog(axe, ±pas, feedrate)` → `ui_commander(BACKEND_ACTION_GCODE, …)`. Callbacks home : `klipper_gcode_home(masque)`. `mettre_a_jour` : ligne position + outil actif (grise si `donnees_perimees`). Contexte = struct rangée dans `taille_contexte`.
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): ecran Deplacer (jog gros + pas + vitesse + home)"`

---

## Task 5 : Accueil-hub

**Files :**
- Create : `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.h`, `.c`
- Test : `host-test/tests/test_ecran_accueil_hub.c`

**Interfaces :**
- Consumes : tuiles température (réutiliser le code de tuile de `ecran_accueil_idle.c` + paliers `klipper_paliers.h`), `navigation_empiler` (pour les cases de menu).
- Produces : `extern const ecran_desc_t ECRAN_ACCUEIL_HUB;`

- [ ] **Step 1 — test qui échoue** (`test_ecran_accueil_hub.c`, modèle = `test_ecran_accueil_idle.c` §températures) : empiler le hub avec un état simulé à 2 extrudeurs + plateau ; `VERIFIER` 3 tuiles de température aux bonnes valeurs (actuelle/consigne) ; `VERIFIER` 6 cases de menu ; cliquer « Déplacer » → `VERIFIER(navigation_profondeur() == 2 && strcmp(navigation_id_courant(),"deplacer")==0)`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** `ecran_accueil_hub.c` : `construire` bâtit la grille de tuiles température (adaptative via paliers, une par chauffe, outil actif surligné) + la grille de 6 cases de menu (Déplacer→`ECRAN_DEPLACER`, Températures/Extruder/Ventilo/Imprimer/Réglages→écrans respectifs ou no-op « à venir » pour les sous-projets futurs). `mettre_a_jour` : rafraîchit les tuiles (grise si `donnees_perimees`). Reprendre le rendu de tuile de l'ancien idle (DRY).
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): accueil-hub (temperatures multitete + grille de menu)"`

---

## Task 6 : Layout racine + intégration du rail + bascule de l'accueil

**Files :**
- Modify : point d'entrée UI (`firmware/main/ui/habillage.c` — vérifier où `navigation_init` + premier `navigation_empiler` sont appelés aujourd'hui).
- Modify : `firmware/main/ui/widgets/confirmation.*` — réutilisé pour la confirmation STOP (widget existant).
- Test : `host-test/tests/test_rail.c` (étendre) ou nouveau `test_integration_rail.c`.

**Interfaces :**
- Consumes : `rail_*` (Task 3), `navigation_*`, `klipper_gcode_arret_urgence` (Task 2), `ECRAN_ACCUEIL_HUB` (Task 5), l'écran Macros existant.

- [ ] **Step 1 — test qui échoue** : construire le layout racine `[rail | conteneur-nav]`, `navigation_init(conteneur-nav)`, empiler le hub. Vérifier : le rail reste présent après avoir empilé Déplacer (`VERIFIER` le rail existe toujours quand `navigation_id_courant()=="deplacer"`). Action `RAIL_ACCUEIL` depuis Déplacer → `navigation_profondeur()==1` et id « accueil-hub ». Action `RAIL_STOP` → ouvre une confirmation ; confirmer → `source_etat_sim_derniere_commande` contient `M112`.
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** : au point d'entrée UI, créer la racine en `[rail(58px) | conteneur(742px)]`, `rail_creer(..., sur_action, ...)`, `navigation_init(conteneur)`, empiler `ECRAN_ACCUEIL_HUB`. `sur_action` : `RAIL_ACCUEIL`→`navigation_accueil()` ; `RAIL_HOME`→`klipper_gcode_home(0)`+`ui_commander` ; `RAIL_MACROS`→empiler l'écran Macros ; `RAIL_STOP`→confirmation puis `klipper_gcode_arret_urgence`+`ui_commander`. `rail_marquer_actif` mis à jour au changement d'écran (via `navigation_sequence`/`navigation_id_courant`).
- [ ] **Step 4 — lancer, voir passer**.
- [ ] **Step 5 — commit** : `git commit -am "feat(ui): layout racine rail + nav, bascule accueil-hub"`

---

## Task 7 : Retrait de l'ancien accueil idle

**Files :**
- Delete : `firmware/main/apps/klipper/ecrans/ecran_accueil_idle.{h,c}`, `host-test/tests/test_ecran_accueil_idle.c`
- Modify : tout appelant restant de `ECRAN_ACCUEIL_IDLE` (grep), le CMake si nécessaire.

- [ ] **Step 1** — `grep -rn "ecran_accueil_idle\|ECRAN_ACCUEIL_IDLE" firmware host-test` : lister les références restantes.
- [ ] **Step 2** — supprimer les fichiers + retirer les références (le hub l'a remplacé partout).
- [ ] **Step 3 — build firmware + host-test** : les deux verts (aucune référence pendante).
- [ ] **Step 4 — commit** : `git commit -am "refactor(klipper): retire l'ancien accueil idle (remplace par hub + Deplacer)"`
- [ ] **Step 5 — flash + reboot + validation hardware** : flasher, rebooter (workflow), valider à l'écran (rail, hub multitête, jog gros + pas + vitesse, homing).

---

## Self-Review (rempli)

- **Couverture spec** : rail (T3+T6), accueil-hub multitête (T5), Déplacer jog+pas+vitesse+home (T1,T4), E-STOP (T2,T6), navigation par le rail (T6), tuiles adaptatives (T5). ✓
- **Placeholders** : les cases de menu vers écrans non encore construits (Températures/Extruder/…) sont explicitement « no-op à venir » (sous-projets suivants), pas des TODO cachés. ✓
- **Cohérence des types** : `selecteur_choix_index`/`selecteur_choix_creer` (T1) réutilisés en T4 ; `rail_action_t`/`rail_creer` (T3) réutilisés en T6 ; `klipper_gcode_arret_urgence` (T2) en T6 ; `ECRAN_DEPLACER`/`ECRAN_ACCUEIL_HUB` cohérents entre T4/T5/T6. ✓
- **À confirmer pendant l'exécution** (viennent du spec) : nb d'extrudeurs réels, valeurs de feedrate, rangée Home sur Déplacer, confirmation STOP — tous des défauts déjà posés, ajustables.
