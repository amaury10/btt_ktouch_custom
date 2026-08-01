# Intégration Imprimer — Plan d'implémentation

> **Pour agents :** SOUS-SKILL REQUISE : superpowers:subagent-driven-development.

**But :** rendre le statut d'impression (`ECRAN_ACCUEIL`) atteignable + bien
dimensionné (T1), puis affiché automatiquement en impression (T2).

**Architecture :** conversion 742 de `ecran_accueil.c` + case menu Imprimer
(T1) ; primitive `navigation_remplacer_base` + chooser injecté dans
`habillage_pomper` enveloppant `accueil_impression_actif` (T2).

**Pile :** C, LVGL 9.2, ESP-IDF ; host-test via WSL.

## Contraintes globales

- Contrat `ecran.h`, C3, réseau-libre. `habillage.c` reste GÉNÉRIQUE (le choix
  repos/impression est Klipper : injecté par setter, comme le rail/les
  réglages — jamais d'`#include` klipper dans habillage.c).
- Bascule vivante UNIQUEMENT à profondeur 1 (jamais arracher un sous-écran) ;
  no-op si le fond est déjà le bon (anti-thrash).
- `LARGEUR_CONTENU 742` pour les écrans du conteneur nav.
- Thème sombre ; cibles ≥44px ; FR ; aucune donnée personnelle.

---

## Task 1 : Conversion 742 d'ECRAN_ACCUEIL + câblage menu Imprimer

**Files :**
- Modify : `firmware/main/apps/klipper/ecrans/ecran_accueil.c`
- Modify : `firmware/main/apps/klipper/ecrans/ecran_accueil_hub.c` (case [4])
- Test : `host-test/tests/test_ecran_accueil_hub.c` (étendre).

**Interfaces :**
- Consumes : `ECRAN_ACCUEIL` (`ecran_accueil.h`, id `"accueil"`), `navigation_empiler`.

- [ ] **Step 1 — test qui échoue** (`test_ecran_accueil_hub.c`) : clic
  `menu_boutons[ECRAN_ACCUEIL_HUB_MENU_IMPRIMER]` → `VERIFIER(profondeur==2 &&
  strcmp(navigation_id_courant(),"accueil")==0)` ; libellé `"Imprimer"` (sans « A venir »).
- [ ] **Step 2 — lancer, voir échouer**.
- [ ] **Step 3 — implémenter** : dans `ecran_accueil.c`, `LARGEUR_CONTENU 800→742`
  et `TUILE_LARGEUR 370→341` (garde `3*MARGE + 2*TUILE_LARGEUR == 742`, `MARGE=20`),
  vérifier tous les `_Static_assert` (ajuster au besoin les autres constantes
  dérivées), maj commentaires d'en-tête (« 742x436 ») ; REBUILD pour confirmer
  les asserts. Dans `ecran_accueil_hub.c`, rappel `ouvrir_imprimer_cb` →
  `navigation_empiler(&ECRAN_ACCUEIL)` (idiome `ouvrir_temperatures_cb`,
  `#include "ecran_accueil.h"`), `MENU_DEFS[IMPRIMER].sous_titre=""`, commentaire
  à jour ; assertion test `"Imprimer\nA venir"` → `"Imprimer"`.
- [ ] **Step 4 — lancer, voir passer** (host-test vert) + **build firmware**.
- [ ] **Step 5 — commit** : `git commit -am "feat(klipper): statut impression 742 + case menu Imprimer"`.

---

## Task 2 : Bascule vivante repos↔impression

**Files :**
- Modify : `firmware/main/ui/navigation.h`, `.c` (primitive `navigation_remplacer_base`)
- Modify : `firmware/main/ui/habillage.h`, `.c` (chooser injecté + appel dans pomper)
- Modify : `firmware/main/app_main.c`, `simulateur/main.c` (enregistrer le chooser)
- Test : `host-test/tests/test_navigation.c` (primitive), + intégration via
  `habillage_pomper` (`test_commandes.c` ou dédié).

**Interfaces :**
- Produces : `void navigation_remplacer_base(const ecran_desc_t *desc);` ;
  `void habillage_definir_choix_accueil(const ecran_desc_t *(*choisir)(const void *etat, void *ctx), void *ctx);`
- Consumes : `accueil_impression_actif` (`accueil_choix.h`), `ECRAN_ACCUEIL`,
  `ECRAN_ACCUEIL_HUB`.

- [ ] **Step 1 — test primitive qui échoue** (`test_navigation.c`) :
  empiler A ; `navigation_remplacer_base(&B)` → profondeur 1, id B ; empiler C
  (prof 2) puis `navigation_remplacer_base(&D)` → profondeur 1, id D (dépile
  d'abord) ; `navigation_remplacer_base(&D)` alors que le fond est déjà D →
  no-op (profondeur/séquence inchangées SAUF le premier changement) ; séquence
  incrémentée sur un vrai remplacement.
- [ ] **Step 2 — voir échouer**.
- [ ] **Step 3 — implémenter la primitive** dans `navigation.c` : `navigation_accueil()`
  (ramène à prof 1), puis si l'id du fond diffère de `desc->id`, détruire le fond
  (même séquence que `depiler` : `detruire`+conteneur+contexte) et empiler `desc`
  à sa place (nouveau conteneur plein cadre + `construire`), incrémenter la séquence.
  Garde `desc==NULL`/pile vide.
- [ ] **Step 4 — test chooser qui échoue** (intégration `habillage_pomper`) :
  enregistrer un chooser (impression→ECRAN_ACCUEIL, repos→ECRAN_ACCUEIL_HUB) ;
  état impression + `habillage_pomper` → fond devient `"accueil"` ; retour repos
  → fond redevient `"accueil-hub"` ; en profondeur 2 (sous-écran empilé) →
  `habillage_pomper` NE bascule PAS le fond.
- [ ] **Step 5 — voir échouer, implémenter** : `habillage_definir_choix_accueil`
  (stocke pointeur+ctx, comme `habillage_definir_ecran_reglages`) ; dans
  `habillage_pomper`, après le bloc de mise à jour, SI `navigation_profondeur()==1`
  ET chooser non NULL ET état disponible : `const ecran_desc_t *voulu =
  choisir(&g_etat, ctx); if (voulu && strcmp(navigation_id_courant(), voulu->id)
  != 0) navigation_remplacer_base(voulu);`. `app_main.c`/`simulateur/main.c` :
  `habillage_definir_choix_accueil(choix_accueil_klipper, NULL)` où
  `choix_accueil_klipper` rend `accueil_impression_actif(etat) ? &ECRAN_ACCUEIL : &ECRAN_ACCUEIL_HUB`.
- [ ] **Step 6 — voir passer** (host-test vert) + **build firmware**.
- [ ] **Step 7 — commit** : `git commit -am "feat(ui): bascule vivante repos<->impression de l'ecran de fond"`.

## Self-Review

- **Couverture spec** : conversion+câblage (T1), primitive+chooser+bascule (T2),
  garde profondeur 1 + anti-thrash (T2). ✓
- **Placeholders** : aucun — valeurs (742/341) et logique explicites. ✓
- **Cohérence types** : `navigation_remplacer_base`/`habillage_definir_choix_accueil`
  T2 ; `ECRAN_ACCUEIL` id `"accueil"` ; `ECRAN_ACCUEIL_HUB_MENU_IMPRIMER` (=4). ✓
- **Habillage générique** : le couplage Klipper (accueil_choix, ECRAN_*) reste
  dans app_main.c/sim via le chooser injecté — habillage.c n'inclut rien de klipper. ✓
