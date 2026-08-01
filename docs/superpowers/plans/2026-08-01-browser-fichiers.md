# Navigateur de fichiers gcode — Plan d'implémentation

> **Pour agents :** SOUS-SKILL REQUISE : superpowers:subagent-driven-development.

**But :** démarrer une impression depuis l'écran en choisissant un fichier gcode
local (USB inclus via le dossier `gcodes`).

**Architecture :** backend calqué sur les MACROS (requête WS `server.files.list`
au connect → parseur pur → `etat->fichiers[]`) ; écran calqué sur `ecran_macros.c`
(grille paginée → tap → confirmation → `SDCARD_PRINT_FILE`).

**Pile :** C, LVGL 9.2, ESP-IDF ; host-test via WSL. Réponse Moonraker réelle
(vkp) : `{"result":[{"path":"nom.gcode","modified":...,"size":...}, ...]}`.

## Contraintes globales

- Backend RPC = fonctions PURES (aucun réseau/alloc), testables host. Décision
  de poison par champ (voir `moonraker_rpc.h`). Un chemin ≥ borne est IGNORÉ
  (jamais tronqué). `moonraker_ws.c` = seul code réseau (matériel-critique) :
  calquer le mécanisme macros À L'IDENTIQUE, ne rien inventer.
- Contrat `ecran.h`, C3, réseau-libre dans les callbacks LVGL. `LARGEUR_CONTENU 742`.
- Thème sombre ; cibles ≥44px ; FR ; aucune donnée personnelle.

---

## Task 1 : État + parseur `rpc_lire_fichiers` + gcode `imprimer_fichier` (fonctions pures)

**Files :**
- Modify : `firmware/main/core/etat_klipper.h` (champs + constantes)
- Modify : `firmware/main/apps/klipper/moonraker_rpc.{h,c}` (parseur)
- Modify : `firmware/main/apps/klipper/klipper_gcode.{h,c}` (gcode)
- Test : `host-test/tests/test_moonraker_rpc.c`, `test_klipper_gcode.c` (étendre).

**Interfaces :**
- Produces :
  - état : `KLIPPER_FICHIERS_MAX (32)`, `KLIPPER_FICHIER_NOM_MAX (64)`,
    `char fichiers[KLIPPER_FICHIERS_MAX][KLIPPER_FICHIER_NOM_MAX]`,
    `uint8_t nb_fichiers`, `bool fichiers_tronques` dans `etat_klipper_t`.
  - `bool rpc_lire_fichiers(etat_klipper_t *etat, const char *json, size_t longueur);`
  - `bool klipper_gcode_imprimer_fichier(char *sortie, size_t taille, const char *nom);`

**Modèles :** `rpc_lire_macros` (`moonraker_rpc.c`) pour le parseur (mais lit
`result` = tableau d'OBJETS, champ `.path`, pas `result.objects` = tableau de
chaînes) ; `klipper_gcode_arret_urgence`/`_consigne_temp` pour le gcode.

- [ ] **Step 1 — tests qui échouent** :
  - `test_moonraker_rpc.c` : `rpc_lire_fichiers` sur `{"jsonrpc":"2.0","id":1,"result":[{"path":"a.gcode","size":10},{"path":"sub/b.gcode","size":20}]}` → `nb_fichiers==2`, `fichiers[0]=="a.gcode"`, `fichiers[1]=="sub/b.gcode"`, `fichiers_tronques==false` ; `result` absent / pas un tableau → false, état INTACT (sentinelle) ; un `.path` ≥ `KLIPPER_FICHIER_NOM_MAX` → ignoré (compté hors nb) ; > `KLIPPER_FICHIERS_MAX` entrées → `nb==MAX && fichiers_tronques==true`.
  - `test_klipper_gcode.c` : `klipper_gcode_imprimer_fichier(buf,n,"a.gcode")` → `"SDCARD_PRINT_FILE FILENAME=a.gcode"` ; `"sub/b.gcode"` → `"...FILENAME=sub/b.gcode"` ; nom NULL/vide → false ; tampon trop court → false.
- [ ] **Step 2 — voir échouer**.
- [ ] **Step 3 — implémenter** les 3 (état + parseur + gcode).
- [ ] **Step 4 — voir passer** (host-test vert).
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): etat fichiers + rpc_lire_fichiers + gcode SDCARD_PRINT_FILE"`.

---

## Task 2 : Requête WS au connect + /state + backend factice

**Files :**
- Modify : `firmware/main/apps/klipper/moonraker_ws.c` (requête au connect + corrélation)
- Modify : `firmware/main/web.c` (`fichiers` dans `/state`, comme `macros`)
- Modify : `firmware/main/core/backend_factice.c` (peupler quelques fichiers factices)
- Test : les suites existantes restent vertes ; ajouter au besoin dans `test_commandes.c`/`test_web` si un point précis est vérifiable host.

**Interfaces :**
- Consumes : `rpc_lire_fichiers` (T1), `rpc_construire_requete`.

- [ ] **Step 1** — `moonraker_ws.c` : ajouter `g_id_fichiers` (comme `g_id_macros`) ; `envoyer_requete_fichiers()` : `rpc_construire_requete(tampon, taille, id, "server.files.list", "{\"root\":\"gcodes\"}")`, poser `g_id_fichiers=id`, mêmes gardes/logs que `envoyer_requete_macros()` ; l'appeler dans `envoyer_identify_et_abonnement()` JUSTE APRÈS `envoyer_requete_macros()` ; dans le handler de réponse, `else if (id == g_id_fichiers && g_id_fichiers != 0)` → `rpc_lire_fichiers(...)`. **Calque EXACT du bloc macros** (lis-le en entier d'abord).
- [ ] **Step 2** — `web.c` : émettre `fichiers` (tableau de noms) + `fichiers_tronques` dans `/state`, exactement comme `macros`/`macros_tronquees` sont émis (cherche le bloc macros dans web.c).
- [ ] **Step 3** — `backend_factice.c` : peupler `etat.fichiers[]` avec 2-3 noms factices (ex. `"benchy.gcode"`, `"calibration/cube.gcode"`) + `nb_fichiers`, pour que le simulateur et host-test aient une liste à afficher (regarde comment `macros` y sont peuplées, si elles le sont, et fais pareil ; sinon ajoute-les au même endroit que le reste de l'état factice).
- [ ] **Step 4 — build** : host-test vert + firmware `idf.py build` propre. (Le WS lui-même n'est pas exercé en host-test — c'est du code réseau ; sa correction repose sur le calque exact du bloc macros + le parseur testé en T1. Signale-le.)
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): requete server.files.list au connect + /state + fichiers factices"`.

---

## Task 3 : Écran browser `ECRAN_FICHIERS` + câblage hub

**Files :**
- Create : `firmware/main/apps/klipper/ecrans/ecran_fichiers.{h,c}`
- Test : `host-test/tests/test_ecran_fichiers.c`
- Modify : les 4 CMake/`main.c`, `ecran_accueil_hub.c` (case Imprimer recâblée).

**Interfaces :**
- Consumes : `etat->fichiers[]` (T1), `klipper_gcode_imprimer_fichier` (T1),
  `confirmation.h`, `ui_commander`, `navigation_empiler`.
- Produces : `extern const ecran_desc_t ECRAN_FICHIERS;` (id `"fichiers"`).

- [ ] **Step 1 — test qui échoue** (`test_ecran_fichiers.c`, modèle
  `test_ecran_macros.c`) : empiler avec un état simulé de 2 fichiers ; la grille
  affiche les noms ; tap sur `"a.gcode"` → une confirmation s'ouvre → confirmer →
  `source_etat_sim_derniere_commande` = gcode `SDCARD_PRINT_FILE FILENAME=a.gcode` ;
  décliner → aucun gcode ; liste vide → message « Aucun fichier » ; grisage sur
  `donnees_perimees`.
- [ ] **Step 2 — voir échouer**.
- [ ] **Step 3 — implémenter** `ecran_fichiers.{h,c}` (grille paginée modèle
  `ecran_macros.c`, tap → `confirmation_ouvrir("Print?", "<nom>", "Print",
  false, rappel, &info)` → `klipper_gcode_imprimer_fichier` → `envoyer_gcode`) ;
  câbler 4 CMake/`main.c` ; **RECÂBLER** `ecran_accueil_hub.c` : la case
  `ECRAN_ACCUEIL_HUB_MENU_IMPRIMER` (index 4) empile désormais `&ECRAN_FICHIERS`
  (au lieu de `&ECRAN_ACCUEIL`), `#include "ecran_fichiers.h"` ; le sous-titre
  reste `""` (déjà fonctionnelle). `ECRAN_ACCUEIL` reste atteint par la bascule
  vivante (sous-projet 5) — inchangée. Mets à jour le test hub si l'id attendu
  change (`"accueil"` → `"fichiers"`).
- [ ] **Step 4 — voir passer** (host-test vert) + **build firmware** + capture sim.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): ecran navigateur de fichiers + demarrage impression"`.

## Self-Review

- **Couverture spec** : état+parseur+gcode (T1), WS connect+state+factice (T2),
  écran+confirmation+démarrage+câblage (T3). ✓
- **Placeholders** : aucun — forme JSON réelle + gcode explicites. ✓
- **Cohérence types** : `rpc_lire_fichiers`/`klipper_gcode_imprimer_fichier`/
  `fichiers[]` T1↔T2↔T3 ; `ECRAN_FICHIERS` id `"fichiers"` ;
  `ECRAN_ACCUEIL_HUB_MENU_IMPRIMER` (=4). ✓
