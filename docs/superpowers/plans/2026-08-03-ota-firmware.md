# OTA du firmware K-Touch — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Mettre à jour NOTRE firmware over-WiFi (upload HTTP), avec rollback A/B
via `rescue.c` et sauvegarde restaurable de BTT — le seul pas irréversible
(écraser app0) en dernier, gardé par un backup BTT valide.

**Architecture:** Helpers purs dans `ota_image.{h,c}` (testables hôte).
Orchestration flash dans un NOUVEAU `ota.c` (jamais dans `web.c` — son en-tête
interdit `esp_ota_begin/write`). Routes httpd dans `web.c` qui DÉLÈGUENT à
`ota.c`. Rollback = `rescue.c` réutilisé tel quel.

**Tech Stack:** ESP-IDF 5.5.5 (esp_ota_ops, esp_partition, esp_app_desc,
mbedtls SHA-256), httpd, host-test.

## Global Constraints (invariants de sûreté — repris du spec)

- **Jamais effacer app0/BTT sans sauvegarde BTT VÉRIFIÉE** en spiffs (gate dans
  le commit). **Jamais `esp_ota_set_boot_partition` sans `esp_ota_end` réussi.**
  La cible d'écriture est TOUJOURS `esp_ota_get_next_update_partition()` (slot
  inactif), jamais le slot courant.
- **Rollback = `rescue.c` inchangé** (compteur de boot RTC + `rescue_arm` +
  reset sur GOT_IP). NE PAS activer le rollback IDF natif. NE PAS réécrire rescue.
- Aucune écriture flash sur la pile httpd/timer : le commit et le restore
  s'exécutent sur une **tâche dédiée** (motif `rescue_switch_now()`), jamais dans
  le handler httpd.
- `/log`, `/revert`, `/status` et le serveur HTTP doivent survivre à tout — ne
  rien casser dans `web.c` qui les porte. Toute route ajoutée est ADDITIVE.
- La plupart du code est ESP-only (non testable hôte). Seuls les helpers PURS
  (`ota_image`) sont testés hôte ; le reste est **compile idf + validé au
  matériel** dans l'ordre sûr. Commentaires FR ; jamais de `*/` dans un commentaire.

**Templates :** `web.c` (routes httpd, `/revert`/`/status`, `esp_ota_get_running_partition`),
`rescue.{h,c}` (rollback, tâche dédiée, `rescue_switch_now`), `ecran_stub.c`
(le stub Updater à remplacer par un écran d'état).

---

### Task 1 : Helpers purs `ota_image.{h,c}` + tests hôte

**Files:** Create `firmware/main/ota_image.{h,c}` ; Modify `firmware/main/CMakeLists.txt`,
`host-test/CMakeLists.txt`, `host-test/tests/main.c` ; Test `host-test/tests/test_ota_image.c`.

**Produces (fonctions pures, aucune dépendance ESP) :**
```c
/* Valide l'en-tete d'une image applicative ESP : premier octet == 0xE9 (magic),
   et taille dans [taille_min_plausible, taille_partition]. Rend false sinon. */
bool ota_image_entete_valide(const uint8_t *debut, size_t n, size_t taille_image, size_t taille_partition);

/* En-tete de sauvegarde BTT ecrit en tete du blob spiffs : magic + taille + sha256.
   Serialise/parse dans un tampon fixe. */
typedef struct { uint32_t magic; uint32_t taille; uint8_t sha256[32]; } ota_backup_entete_t;
#define OTA_BACKUP_MAGIC 0x4B544241u /* "ABTK" */
bool ota_backup_entete_serialiser(const ota_backup_entete_t *e, uint8_t *sortie, size_t taille);
bool ota_backup_entete_parser(const uint8_t *src, size_t taille, ota_backup_entete_t *sortie);

/* Compare deux SHA-256 en temps constant. */
bool ota_sha256_egal(const uint8_t a[32], const uint8_t b[32]);
```
(Le calcul SHA-256 lui-même = mbedtls côté ESP ; côté hôte, tester le
format/parse/compare, pas le hash matériel.)

- [ ] Step 1 : tests — magic 0xE9 accepté / 0x00 rejeté ; taille hors bornes rejetée ;
  serialiser puis parser rend l'identique ; parser sur tampon trop court → false ;
  `ota_sha256_egal` égal/différent.
- [ ] Step 2 : lancer, voir échouer. Step 3 : implémenter. Step 4 : host-test + idf verts.
- [ ] Step 5 : commit `feat(ota): helpers purs de validation d'image et d'en-tete de backup`.

---

### Task 2 : `/status` enrichi + écran Updater d'état (ZÉRO risque)

**Files:** Modify `firmware/main/web.c` (`/status`) ; remplacer le stub `ECRAN_UPDATER`
(dans `ecran_stub.c` ou un nouveau `ecran_updater.{h,c}`) par un écran d'ÉTAT.

- `web.c` `/status` : ajouter `version` (`esp_app_get_description()->version`),
  `slot` (déjà présent), `backup_btt` (présent/valide/absent — via une fonction
  `ota.c` `ota_backup_etat()` que Task 3 fournira ; en Task 2, exposer un stub
  qui rend « absent » pour ne pas bloquer — OU repousser le champ backup à T3).
  Pour éviter la dépendance : Task 2 ajoute `version` seulement ; le champ
  `backup_btt` de `/status` est ajouté en Task 3.
- Écran Updater : lecture seule — slot courant, version, et un texte « Update via
  /ota (browser) ». Remplace le placeholder « Requires OTA... ». `mettre_a_jour`
  peut relire la version (constante) une fois. `detruire=NULL`. Contrat
  `ecran_desc_t`, ASCII/anglais, ≥44px si un élément est tactile (ici aucun).

- [ ] Step 1 : test hôte de l'écran (construit, affiche version/slot — via une
  façade testable ou `#ifdef ESP_PLATFORM` pour `esp_app_get_description`).
- [ ] Steps 2-4 : implémenter, host-test + sim + idf verts. Step 5 : commit
  `feat(ota): /status version + ecran Updater d'etat`.

---

### Task 3 : Sauvegarde BTT → spiffs + vérif (`ota.c`, `/backup-btt`) (ZÉRO risque exécutable)

**Files:** Create `firmware/main/ota.{h,c}` ; Modify `web.c` (routes), `CMakeLists.txt`.

**Produces (ota.h) :**
```c
typedef enum { OTA_BACKUP_ABSENT, OTA_BACKUP_VALIDE, OTA_BACKUP_CORROMPU } ota_backup_etat_t;
ota_backup_etat_t ota_backup_etat(void);          /* relit l'en-tete + verifie le SHA */
esp_err_t         ota_backup_btt(char *msg, size_t msg_taille); /* app0 -> spiffs (brut) + verif */
```
- `ota_backup_btt` : lit `app0` (le slot NON courant qui porte BTT ; identifié via
  `esp_partition_find`/label, PAS `get_next_update` car il faut spécifiquement app0)
  par blocs → écrit BRUT dans la partition `spiffs` (en-tête `ota_backup_entete_t`
  puis l'image) via `esp_partition_erase_range`+`esp_partition_write` → RELIT et
  vérifie le SHA-256 (mbedtls). N'écrit JAMAIS dans un slot app. Rend un message clair.
- `web.c` : `GET /backup-btt` (page + état via `ota_backup_etat()`), `POST /backup-btt`
  (déclenche `ota_backup_btt`, sur tâche dédiée si la durée l'exige, sinon
  synchrone borné). Ajoute `backup_btt` à `/status`.

- [ ] Step 1 : test hôte de `ota_backup_entete_*` déjà couvert (T1) ; ici, tester
  toute logique pure ajoutée (calcul d'offsets, découpage en blocs) si extraite.
  Le flux flash lui-même = validé matériel. Step 2-3 : implémenter.
- [ ] Step 4 : idf build vert ; **validation matériel** : `POST /backup-btt` puis
  `GET /status` montre `backup_btt: valide`. Step 5 : commit
  `feat(ota): sauvegarde de l'image BTT vers spiffs + verification SHA-256`.

---

### Task 4 : Dry-run `/ota?dry_run=1` — réception + vérif SANS commit (ZÉRO risque)

**Files:** Modify `ota.c`, `web.c`.

**Produces :** `esp_err_t ota_verifier_flux(httpd_req_t *req, ...);` — lit le corps
POST en flux, calcule le SHA-256 à la volée (mbedtls, SANS stocker), vérifie
magic 0xE9 sur les premiers octets + taille ≤ taille de partition, compare au
SHA fourni (query `?sha=` ou header) s'il est donné. **N'appelle JAMAIS
`esp_ota_begin`** → n'efface rien. Rend le SHA calculé + verdict.
- `web.c` : `POST /ota?dry_run=1` → `ota_verifier_flux`. `GET /ota` : page d'upload
  (formulaire multipart) + état (slot, version, backup).

- [ ] Step 1-3 : implémenter (SHA streaming, parse des premiers octets).
- [ ] Step 4 : idf vert ; **validation matériel** : POST un `.bin` en dry-run →
  SHA + « image valide », et confirmer qu'app0 est INTACT (aucune écriture).
- [ ] Step 5 : commit `feat(ota): dry-run de reception+verification d'image (sans ecriture)`.

---

### Task 5 : Commit OTA `/ota` — le pas irréversible, GARDÉ + rollback

**Files:** Modify `ota.c`, `web.c`.

**Produces :** `esp_err_t ota_appliquer_flux(httpd_req_t *req, char *msg, size_t n);`
sur **tâche dédiée** (motif `rescue_switch_now`). Séquence :
1. **GATE** : `cible = esp_ota_get_next_update_partition()`. Si `cible` == app0 (BTT
   encore présent, jamais écrasé) ET `ota_backup_etat() != OTA_BACKUP_VALIDE` →
   **REFUSER** avec message explicite. (Comment savoir si app0 porte encore BTT :
   heuristique — le label/version, ou un drapeau NVS « btt_ecrase » posé au 1er
   commit réussi. Utiliser un drapeau NVS `ota/btt_ecrase` : tant qu'il est faux
   et que la cible est app0, exiger le backup.)
2. `esp_ota_begin(cible, OTA_SIZE_UNKNOWN, &handle)` → `esp_ota_write` en flux
   (vérifie magic sur le 1er bloc AVANT de continuer) → `esp_ota_end(handle)`
   (validation image). Si échec à toute étape → abort, message, PAS de set_boot.
3. `esp_ota_set_boot_partition(cible)` → poser le drapeau NVS `btt_ecrase=1` si la
   cible était app0 → `rescue_arm(...)` (armer le filet) → `esp_restart()`.
- `web.c` : `POST /ota` (sans dry_run) → délègue à `ota_appliquer_flux` sur la
  tâche dédiée. Réponse HTTP renvoyée AVANT le restart.

- [ ] Step 1-3 : implémenter la gate + le flux + le drapeau NVS + le rollback arming.
- [ ] Step 4 : idf vert. **Validation matériel dans l'ordre** : d'abord backup
  (T3) valide, puis un `/ota` réel avec une image CONNUE-BONNE → reboot dans le
  nouveau slot → GOT_IP → `rescue_reset_boot_count`. Vérifier qu'un `/ota` SANS
  backup valide est bien REFUSÉ. (C'est le premier écrasement d'app0 — déclenché
  par l'utilisateur, quand il est prêt.)
- [ ] Step 5 : commit `feat(ota): commit OTA garde (refus sans backup BTT) + rollback rescue.c`.

---

### Task 6 : Restauration BTT `/restore-btt` (assurance)

**Files:** Modify `ota.c`, `web.c`.

**Produces :** `esp_err_t ota_restaurer_btt(char *msg, size_t n);` — relit la
sauvegarde spiffs (en-tête + vérif SHA), puis l'écrit dans le slot inactif via
le MÊME chemin OTA (`esp_ota_begin/write/end/set_boot`), `rescue_arm`, restart →
retour à BTT. `web.c` : `GET/POST /restore-btt`.
- [ ] Step 1-3 : implémenter (réutilise le chemin de commit + la lecture spiffs).
- [ ] Step 4 : idf vert ; **validation matériel** : `POST /restore-btt` → reboot
  sur BTT. Step 5 : commit `feat(ota): restauration de BTT depuis la sauvegarde spiffs`.

---

## Self-review (couverture spec)
- Helpers purs + tests : T1 ✅. `/status`+écran état : T2 ✅. Backup BTT : T3 ✅.
  Dry-run : T4 ✅. Commit gardé + rollback : T5 ✅. Restore : T6 ✅.
- Ordre sûr (irréversible en dernier, gardé) : T1-T4 zéro risque, T5 seul écrase
  app0 et est gardé par le backup. ✅
- `esp_ota_begin/write` uniquement dans `ota.c`, jamais `web.c` ; rescue.c réutilisé ;
  routes additives ; tâche dédiée pour le flash. ✅
- Noms cohérents : `ota_image_*`, `ota_backup_*`, `ota_verifier_flux`,
  `ota_appliquer_flux`, `ota_restaurer_btt`, drapeau NVS `ota/btt_ecrase`. ✅
