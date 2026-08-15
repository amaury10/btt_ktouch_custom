# Bed Mesh + Input Shaper — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** remplacer les stubs Bed Mesh et Input Shaper par les vrais écrans
(spec : `docs/superpowers/specs/2026-08-15-bed-mesh-input-shaper-design.md`).

**Architecture :** store PSRAM dédié + parseur pur pour la matrice du mesh
(jamais dans etat_klipper_t) ; input_shaper (24 o) intégré à l'état ;
abonnement WS +2 objets (tampon 512→640) ; carte de chaleur en grille de
cellules LVGL ; commandes via le chemin gcode standard.

**Tech stack :** identique aux lots précédents (IDF 5.5.5, host-test WSL,
LVGL 9, idiomes du dépôt).

## Global Constraints

- Leçons RAM : matrice UNIQUEMENT en store PSRAM (BED_MESH_MAX 15/axe) ;
  aucun tampon >256 o sur pile ; scratchs PSRAM.
- Gates avant commit : host 0 échec, idf vert, sim vert.
- Français, POURQUOI, patrons existants cités par nom.

---

### Task 1 : store + parseur bed_mesh (TDD)

**Files:** Create `firmware/main/apps/klipper/bed_mesh_store.h/.c`,
`bed_mesh_parse.h/.c` ; CMakeLists ×3 ; Test : `host-test/tests/test_bed_mesh.c`
(+ main.c `suite_bed_mesh();`).

**Produces :**
```c
#define BED_MESH_MAX 15
typedef struct {
    bool  present;                      /* une matrice valide est chargée */
    char  profil[24];                   /* profile_name ("" si aucun) */
    float mesh_min_x, mesh_min_y, mesh_max_x, mesh_max_y;
    uint8_t nb_x, nb_y;                 /* colonnes (X) / lignes (Y), <= BED_MESH_MAX */
    bool  tronquee;                     /* matrice source plus grande que BED_MESH_MAX */
    float z[BED_MESH_MAX][BED_MESH_MAX];/* [ligne(Y)][colonne(X)] */
    float z_min, z_max;                 /* bornes calculées de la matrice retenue */
} bed_mesh_t;
void bed_mesh_definir(const bed_mesh_t *mesh);  /* copie sous verrou, +1 génération */
void bed_mesh_lire(bed_mesh_t *dest);           /* scratch appelant en PSRAM obligatoire : ~1 Ko */
uint32_t bed_mesh_generation(void);
/* Parseur PUR : `objet` = le sous-objet JSON "bed_mesh" (cJSON) ; rend vrai
 * si au moins un champ reconnu a été fusionné dans `mesh` (fusion par champ,
 * un notify partiel ne détruit pas le reste -- même politique que
 * rpc_fusionner_*). */
bool bed_mesh_fusionner_json(bed_mesh_t *mesh, const struct cJSON *objet);
```
z_min/z_max recalculés par le parseur quand la matrice change. Matrice
source > 15 : retenir les 15 premiers points par axe + tronquee=true.

- [ ] Tests rouges (fixture 3×3 réaliste : probed_matrix, mesh_min/max
  [x,y], profile_name ; champs absents tolérés ; matrice 20×20 →
  troncature + drapeau ; JSON sans "bed_mesh" → false) → implémenter →
  vert. Le store suit le patron usb_fichiers (PSRAM, verrou court,
  copies bornées à l'utile).

### Task 2 : abonnement + branchement WS + input_shaper dans l'état (TDD)

**Files:** Modify `moonraker_rpc.c` (PARAMS +"bed_mesh","input_shaper" ;
fusionner input_shaper dans rpc_fusionner_* comme les limites),
`moonraker_ws.c` (MOONRAKER_WS_REQUETE_OCTETS 640 ; appel
`bed_mesh_fusionner_json` sur le sous-objet bed_mesh aux DEUX points :
instantané d'abonnement + notify_status_update -- publier via
bed_mesh_definir avec un scratch statique PSRAM du fichier),
`core/etat_klipper.h` (+ `char shaper_type_x[12], shaper_type_y[12];
float shaper_freq_x, shaper_freq_y;` -- dérogation 24 o documentée),
Tests : fixtures moonraker existantes étendues (`test_moonraker_parse` ou
suite dédiée) pour input_shaper.

- [ ] Étendre les tests de parse (fixture input_shaper : type/freq X/Y ;
  absents = inchangés) → rouge → implémenter → vert. La longueur de la
  requête d'abonnement n'est vérifiable qu'à l'exécution :
  rpc_construire_requete rend déjà faux (journalisé) si le tampon déborde,
  et le test host de l'abonnement vérifie la présence des deux nouveaux
  objets dans la chaîne produite.

### Task 3 : écran Bed Mesh

**Files:** Create `ecrans/ecran_bed_mesh.h/.c` ; Modify `ecran_stub.c`
(retirer ECRAN_BED_MESH de STUBS), `ecran_menu_reglages.c` (include),
CMakeLists ×3, app_main.c (générations externes : + bed_mesh_generation()).

- Zone haute : « Profile: <nom>  Z: <min> .. <max> » (ou « No mesh — run
  calibrate » si !present ; « (truncated) » si tronquee).
- Grille : nb_x × nb_y cellules pleines (couleur = lerp bleu#2980B9 →
  vert#2ECC71 → rouge#E74C3C sur [z_min..z_max] ; plage nulle → tout vert),
  centrée, cellules carrées calculées pour tenir dans 742×~330.
- Bas : boutons « Calibrate » (confirmation : « Bed must be homed. Start
  BED_MESH_CALIBRATE? ») et « Clear » (confirmation : « Discard current
  mesh? ») → envoyer_gcode (idiome ecran_actions : construire_arguments_
  gcode/ui_commander local au fichier).
- mettre_a_jour : sur changement de bed_mesh_generation() -> relire via un
  scratch statique PSRAM (patron ecran_usb) et reconstruire les couleurs.
  Grille de lv_obj créés UNE fois (15×15 max, cachés au-delà de nb_x/nb_y).

- [ ] Test host : construction + libellés (mesh vide ; puis definir 3×3 →
  min/max affichés, cellules visibles = 9). Gates.

### Task 4 : écran Input Shaper + sortie des stubs

**Files:** Create `ecrans/ecran_input_shaper.h/.c` ; Modify `ecran_stub.c`
(retirer ECRAN_INPUT_SHAPER ; Spoolman RESTE), CMakeLists ×3.

- Deux blocs X/Y : « Type: <t> » (bouton → selecteur_choix 6 types : zv,
  mzv, zvd, ei, 2hump_ei, 3hump_ei) ; « Freq: <f> Hz » (bouton → clavier
  numérique, bornes 10-150, hors bornes → habillage_notifier). Application
  immédiate : SET_INPUT_SHAPER SHAPER_TYPE_<axe>=… / SHAPER_FREQ_<axe>=…
  (une commande par champ modifié). Mention fixe « Not saved to printer
  config » en bas (gris).
- mettre_a_jour : affiche l'état courant (etat->shaper_*), grisé si
  donnees_perimees (idiome des panneaux).

- [ ] Test host : libellés + tap type → selecteur ouvert → choix → gcode
  SET_INPUT_SHAPER dans la trace sim (idiome test_ecran_retraction). Gates.

### Task 5 : gates finaux, revue, commit, build, mémoire

- [ ] host/idf/sim verts ; `/code-review medium` ; constats confirmés
  traités ; commit unique ; reconfigure+build ; estampille + ELF archivé ;
  mémoire (playbook : mesh réel sur CR-10, calibrate, U1, shaper set).
