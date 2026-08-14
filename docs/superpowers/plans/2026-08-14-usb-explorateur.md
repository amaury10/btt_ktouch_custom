# Explorateur de fichiers USB — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** remplacer le scan récursif complet de la clé USB par un
explorateur dossier par dossier (spec :
`docs/superpowers/specs/2026-08-14-usb-explorateur-design.md`).

**Architecture :** le store `usb_fichiers` devient l'état du répertoire
courant (chemin + entrées dossiers/fichiers) ; la tâche pérenne de
`usb_scan.c` liste UN répertoire à la demande (tampon de chemin sous
portMUX + sémaphore existant) ; l'écran affiche noms seuls, dossiers
navigables, « .. » injectée en tête hors racine.

**Tech stack :** ESP-IDF 5.5.5 (cible), host-test C (WSL Debian, script
wrapper `run_host_tests.sh` du scratchpad), LVGL 9.

## Global Constraints

- Commentaires en français, style du dépôt (POURQUOI, pas le quoi).
- Aucune allocation en RAM interne ajoutée ; tout tampon nouveau en PSRAM ou
  statique PSRAM existant. Piles de tâches inchangées.
- `usb_est_gcode()` (usb_upload.h) reste le filtre fichier unique.
- Gates avant tout commit : host-test 0 échec, `idf.py build` vert ;
  simulateur relié en fin de lot.
- Jamais de port série ; la dalle ne se flashe que par l'utilisateur.

---

### Task 1 : store répertoire courant + tri pur

**Files:**
- Modify: `firmware/main/apps/klipper/usb_fichiers.h`
- Modify: `firmware/main/apps/klipper/usb_fichiers.c`
- Test: `host-test/tests/test_usb_upload.c`

**Interfaces (Produces) :**
```c
#define USB_FICHIERS_MAX 64
typedef struct { char chemin[USB_FICHIER_CHEMIN_MAX]; size_t taille; bool est_dossier; } usb_fichier_t;
typedef struct { bool monte; char chemin_courant[USB_FICHIER_CHEMIN_MAX];
                 usb_fichier_t fichiers[USB_FICHIERS_MAX]; uint8_t nb;
                 bool tronques; bool scan_en_cours; } usb_fichiers_t;
void usb_fichiers_definir(bool monte, const char *chemin_courant,
                          const usb_fichier_t *fichiers, uint8_t nb, bool tronques);
/* usb_fichiers_scan_demarre()/lire()/generation() : inchangées.
 * usb_fichiers_publier_partiel() : SUPPRIMÉE. */
const char *usb_chemin_nom(const char *chemin);      /* dernier segment, pur */
void usb_listing_trier(usb_fichier_t *entrees, uint8_t nb); /* dossiers d'abord, alpha (strcasecmp), pur */
```

- [ ] **Step 1 : réécrire les tests du store** — dans
  `test_usb_upload.c`, remplacer `section_store_scan_en_cours` et
  `section_store_publication_partielle` par :

```c
static void section_store_repertoire(void)
{
    usb_fichiers_t lu;

    usb_fichiers_definir(false, "", NULL, 0, false);
    usb_fichiers_lire(&lu);
    VERIFIER(!lu.monte);
    VERIFIER(lu.chemin_courant[0] == '\0');

    uint32_t generation_avant = usb_fichiers_generation();
    usb_fichiers_scan_demarre();
    usb_fichiers_lire(&lu);
    VERIFIER(lu.scan_en_cours);
    VERIFIER(usb_fichiers_generation() == generation_avant + 1);

    usb_fichier_t entrees[2];
    memset(entrees, 0, sizeof(entrees));
    strcpy(entrees[0].chemin, "/usb/dossier");
    entrees[0].est_dossier = true;
    strcpy(entrees[1].chemin, "/usb/piece.gcode");
    entrees[1].taille = 1234;
    usb_fichiers_definir(true, "/usb", entrees, 2, false);
    usb_fichiers_lire(&lu);
    VERIFIER(lu.monte);
    VERIFIER(!lu.scan_en_cours);
    VERIFIER_TEXTE(lu.chemin_courant, "/usb");
    VERIFIER(lu.nb == 2);
    VERIFIER(lu.fichiers[0].est_dossier);
    VERIFIER(!lu.fichiers[1].est_dossier);

    /* Éjection : tout retombe, chemin compris. */
    usb_fichiers_scan_demarre();
    usb_fichiers_definir(false, "", NULL, 0, false);
    usb_fichiers_lire(&lu);
    VERIFIER(!lu.scan_en_cours && !lu.monte && lu.nb == 0);
    VERIFIER(lu.chemin_courant[0] == '\0');
}

static void section_listing_tri(void)
{
    VERIFIER_TEXTE(usb_chemin_nom("/usb/dossier/piece.gcode"), "piece.gcode");
    VERIFIER_TEXTE(usb_chemin_nom("sans-slash"), "sans-slash");

    usb_fichier_t e[4];
    memset(e, 0, sizeof(e));
    strcpy(e[0].chemin, "/usb/zeta.gcode");
    strcpy(e[1].chemin, "/usb/Beta");
    e[1].est_dossier = true;
    strcpy(e[2].chemin, "/usb/alpha.gcode");
    strcpy(e[3].chemin, "/usb/gamma");
    e[3].est_dossier = true;
    usb_listing_trier(e, 4);
    /* dossiers d'abord (alpha, insensible casse), puis fichiers alpha */
    VERIFIER_TEXTE(usb_chemin_nom(e[0].chemin), "Beta");
    VERIFIER_TEXTE(usb_chemin_nom(e[1].chemin), "gamma");
    VERIFIER_TEXTE(usb_chemin_nom(e[2].chemin), "alpha.gcode");
    VERIFIER_TEXTE(usb_chemin_nom(e[3].chemin), "zeta.gcode");

    usb_listing_trier(NULL, 0); /* garde NULL : ne crashe pas */
}
```
  et mettre à jour `suite_usb_upload()` (retirer les deux anciens appels,
  ajouter `section_store_repertoire(); section_listing_tri();`).

- [ ] **Step 2 : run tests → échec attendu** (`usb_chemin_nom` absent,
  signature `definir` différente). Wrapper WSL habituel.

- [ ] **Step 3 : implémenter** — `usb_fichiers.h` : nouvelles
  déclarations/champs ci-dessus (borne 64, commentaires : borne PAR DOSSIER,
  `scan_en_cours` = « listage en cours ») ; retirer
  `usb_fichiers_publier_partiel`. `usb_fichiers.c` : `definir` copie
  `chemin_courant` (strlcpy-like borné, "" si NULL ou monte=false) ;
  supprimer `publier_partiel` ; ajouter :

```c
const char *usb_chemin_nom(const char *chemin)
{
    if (chemin == NULL) { return ""; }
    const char *slash = strrchr(chemin, '/');
    return (slash != NULL) ? slash + 1 : chemin;
}

static int usb_listing_comparer(const void *a, const void *b)
{
    const usb_fichier_t *fa = (const usb_fichier_t *)a;
    const usb_fichier_t *fb = (const usb_fichier_t *)b;
    if (fa->est_dossier != fb->est_dossier) {
        return fa->est_dossier ? -1 : 1;
    }
    return strcasecmp(usb_chemin_nom(fa->chemin), usb_chemin_nom(fb->chemin));
}

void usb_listing_trier(usb_fichier_t *entrees, uint8_t nb)
{
    if (entrees == NULL || nb < 2) { return; }
    qsort(entrees, nb, sizeof(*entrees), usb_listing_comparer);
}
```
  (includes : `<stdlib.h>` pour qsort, `<strings.h>` pour strcasecmp —
  fonctions PURES, host-compilées, hors du bloc ESP_PLATFORM.)

- [ ] **Step 4 : adapter les appelants de `definir`** dans `usb_scan.c`
  (compilation seulement — comportement retravaillé en Task 2) : unmount →
  `usb_fichiers_definir(false, "", NULL, 0, false)` ; fin de scan →
  passer `PT_USB_MOUNT_PATH` comme chemin ; retirer l'appel à
  `usb_fichiers_publier_partiel` (supprimée).

- [ ] **Step 5 : run tests → 0 échec.** Pas de commit isolé (l'écran ne
  compile plus qu'après Task 3 ? NON — l'écran compile encore : il ne lit
  que des champs existants + nouveaux ; vérifier. Commit de lot en Task 4).

### Task 2 : tâche de listage à la demande

**Files:**
- Modify: `firmware/main/apps/klipper/usb_scan.h`
- Modify: `firmware/main/apps/klipper/usb_scan.c`

**Interfaces (Produces) :**
```c
void usb_scan_demander(const char *chemin); /* usb_scan.h ; no-op host ;
   chemin NULL/vide => PT_USB_MOUNT_PATH ("/usb") */
```
**Consumes :** Task 1 (`usb_fichiers_definir` 5 args, `usb_listing_trier`).

- [ ] **Step 1 : implémenter.** Dans `usb_scan.c` (bloc ESP_PLATFORM) :

```c
/* Répertoire demandé par l'écran (ou "/usb" au montage) -- écrit par
 * usb_scan_demander() (tâche LVGL ou msc_inst_w), lu par la tâche de
 * listage : section critique COURTE, même patron que le store. Deux
 * demandes rapprochées : la dernière écrase la première, le sémaphore
 * binaire coalesce -- exactement le comportement voulu (dernier tap gagne). */
static portMUX_TYPE s_verrou_demande = portMUX_INITIALIZER_UNLOCKED;
static char s_chemin_demande[USB_FICHIER_CHEMIN_MAX] = PT_USB_MOUNT_PATH;

void usb_scan_demander(const char *chemin)
{
    if (s_scan_reveil == NULL || s_scan_tache == NULL) {
        JOURNAL_ERREUR(TAG, "tache de listage absente, demande ignoree");
        return;
    }
    if (chemin == NULL || chemin[0] == '\0') {
        chemin = PT_USB_MOUNT_PATH;
    }
    portENTER_CRITICAL(&s_verrou_demande);
    size_t longueur = strlen(chemin);
    if (longueur >= sizeof(s_chemin_demande)) {
        longueur = sizeof(s_chemin_demande) - 1;
    }
    memcpy(s_chemin_demande, chemin, longueur);
    s_chemin_demande[longueur] = '\0';
    portEXIT_CRITICAL(&s_verrou_demande);
    usb_fichiers_scan_demarre();
    xSemaphoreGive(s_scan_reveil);
}
```
  Remplacer `usb_scan_recursif` + modes par UNE fonction plate
  `usb_lister_repertoire(chemin, nb, tronques)` : readdir une passe, skip
  `.`/`..`/cachés, skip `System Volume Information` (dossiers), filtre
  `usb_est_gcode` + octets de contrôle (fichiers), chemin trop long ignoré,
  `stat` sur fichiers retenus seulement, `dest->est_dossier` renseigné,
  `break` quand `*nb >= USB_FICHIERS_MAX` (tronques=true). Boucle de la
  tâche :

```c
        char chemin[USB_FICHIER_CHEMIN_MAX];
        portENTER_CRITICAL(&s_verrou_demande);
        memcpy(chemin, s_chemin_demande, sizeof(chemin));
        portEXIT_CRITICAL(&s_verrou_demande);

        uint8_t nb = 0;
        bool tronques = false;
        usb_lister_repertoire(chemin, &nb, &tronques);
        usb_listing_trier(s_usb_scan_tampon, nb);
        bool encore_monte = pt_usb_is_mounted();
        usb_fichiers_definir(encore_monte, encore_monte ? chemin : "",
                             encore_monte ? s_usb_scan_tampon : NULL,
                             encore_monte ? nb : 0, encore_monte ? tronques : false);
        if (encore_monte && !pt_usb_is_mounted()) {
            usb_fichiers_definir(false, "", NULL, 0, false);
        }
        JOURNAL_INFO(TAG, "listage %s : %u entree(s)%s", chemin, (unsigned)nb,
                     tronques ? " (tronque)" : "");
```
  Callback de montage : `usb_on_mount_cb()` devient
  `usb_scan_demander(PT_USB_MOUNT_PATH);` (la garde tâche-absente vit déjà
  dans demander()). Branche host (#else) : ajouter
  `void usb_scan_demander(const char *chemin) { (void)chemin; }`.
  `usb_scan.h` : déclarer `usb_scan_demander` (contrat : liste ce
  répertoire dès que possible, dernier appel gagne, no-op host).

- [ ] **Step 2 : gates locaux** — host-test (compil + suites vertes),
  `idf.py build` vert.

### Task 3 : écran explorateur

**Files:**
- Modify: `firmware/main/apps/klipper/ecrans/ecran_usb.h`
- Modify: `firmware/main/apps/klipper/ecrans/ecran_usb.c`

**Consumes :** Task 1 (`usb_chemin_nom`, champs `est_dossier`/
`chemin_courant`), Task 2 (`usb_scan_demander`).

- [ ] **Step 1 : contexte (`ecran_usb.h`).** Tableaux passés à
  `USB_FICHIERS_MAX + 1` (emplacement 0 potentiel de « .. ») ; ajouter
  `bool dossiers_copie[USB_FICHIERS_MAX + 1];` et
  `char chemin_courant[USB_FICHIER_CHEMIN_MAX];` ; renommer le commentaire
  de `chemins_copie` (chemins complets, affichage = nom seul).

- [ ] **Step 2 : copie du store (`ecran_usb_mettre_a_jour`).** Sur
  changement de génération : si `strcmp(fics.chemin_courant,
  ctx->chemin_courant) != 0` → `ctx->page = 0` + copier le chemin. Puis :
  hors racine (`fics.monte && strcmp(fics.chemin_courant, "/usb") != 0`),
  entrée 0 = « .. » : `dossiers_copie[0]=true`, `chemins_copie[0]` = chemin
  PARENT (copie de chemin_courant tronquée au dernier '/', plancher
  "/usb"), `tailles_copie[0]=0` ; puis recopier les entrées du store aux
  indices suivants (mêmes copies bornées qu'aujourd'hui + est_dossier).

- [ ] **Step 3 : affichage (`afficher_page`).** Libellé d'un emplacement :
  entrée « .. » → `LV_SYMBOL_DIRECTORY " .."` ; dossier →
  `LV_SYMBOL_DIRECTORY " "` + `usb_chemin_nom(chemin)` (snprintf dans un
  tampon local de USB_FICHIER_CHEMIN_MAX + 8) ; fichier →
  `usb_chemin_nom(chemin)` seul. Texte « vide » : `scan_en_cours` →
  "Reading USB key..." ; sinon monte → "No .gcode files in this folder" ;
  sinon "Insert a USB key".

- [ ] **Step 4 : statut (`mettre_a_jour_statut`).** Dernière branche (ni
  upload, ni troncature) : afficher `ctx->chemin_courant` (couleur
  COULEUR_GRISE) au lieu de la chaîne vide, seulement si `ctx->monte`.

- [ ] **Step 5 : tap (`bouton_fichier_cb`).** Si
  `ctx->dossiers_copie[indice]` → `usb_scan_demander(
  ctx->chemins_copie[indice]); return;` (aucune confirmation). Sinon flux
  upload inchangé (chemin_attente = chemin complet, déjà le cas).
  Ne PAS désactiver la grille pendant un listage (dernier tap gagne).

- [ ] **Step 6 : gates** — host-test verte (l'écran compile host),
  `idf.py build` vert.

### Task 4 : gates finaux, revue, commit, build estampillé

- [ ] **Step 1 :** host-test 0 échec ; `idf.py build` vert ; simulateur
  relié (`run_sim_build.sh`).
- [ ] **Step 2 :** revue du diff (skill code-review, effort medium) ;
  traiter les constats confirmés.
- [ ] **Step 3 :** commit unique du lot :
  `feat(usb): explorateur de fichiers (navigation par dossier, fin du scan complet)`
  avec le POURQUOI (34-55 s → listage à la demande) + gates dans le corps.
- [ ] **Step 4 :** `idf.py reconfigure build`, vérifier l'estampille
  (`strings ktouch-custom.bin`) = hash du commit, SANS `-dirty`.
- [ ] **Step 5 :** mémoire projet à jour (explorateur en attente de
  validation écran ; playbook : racine ~1 s, navigation, dossier > 64
  tronqué, éjection en cours de navigation, upload depuis sous-dossier,
  accents).
