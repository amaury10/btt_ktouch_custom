# Gestion de parc — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** tableau de bord séquentiel du parc + bascule d'imprimante active
(spec : `docs/superpowers/specs/2026-08-15-gestion-parc-design.md`).

**Architecture :** store parc (config NVS 6 entrées + états sondés, PSRAM,
verrou court) ; sonde HTTP séquentielle (tâche pérenne pile PSRAM, active
seulement écran ouvert, timeout 1,5 s) ; écran Parc (tuiles, tap = bascule
via enregistrement NVS + esp_restart — LE chemin d'application existant,
cf. ecran_configuration.c « power-cycle pour l'instant ») ; ajout d'une
imprimante depuis l'écran Parc (2 claviers), édition de l'ACTIVE via
l'écran Configuration existant inchangé dans son rôle.

**Tech stack :** idem explorateur (IDF 5.5.5, host-test WSL, LVGL 9).

## Global Constraints

- Leçons RAM 14-15/08 : aucun tampon >256 o sur pile ; tampons/scratchs/
  stores en PSRAM ; piles de tâches via xTaskCreateWithCaps(SPIRAM).
- Commentaires français, POURQUOI ; gates host/idf/sim avant commit.
- Timeout sonde : 1500 ms. Pause inter-hôtes : 1000 ms. 6 entrées max.

---

### Task 1 : store du parc (`parc_imprimantes`)

**Files:** Create `firmware/main/apps/klipper/parc_imprimantes.h/.c` ;
Modify `firmware/main/CMakeLists.txt` (srcs), `host-test/CMakeLists.txt`,
`simulateur/CMakeLists.txt` ; Test : nouveau `host-test/tests/test_parc.c`
(+ `tests/main.c` : `suite_parc();` sans contrainte d'ordre).

**Produces (exact) :**
```c
#define PARC_MAX 6
#define PARC_NOM_MAX 24
#define PARC_HOTE_MAX 64
typedef struct { char nom[PARC_NOM_MAX]; char hote[PARC_HOTE_MAX]; } parc_entree_t;
typedef struct { parc_entree_t entrees[PARC_MAX]; uint8_t nb; uint8_t actif; } parc_config_t;
typedef struct { bool sonde; bool atteignable; char etat[16];
                 float buse; float lit; uint8_t progression_pct; } parc_etat_t;
void parc_config_lire(parc_config_t *dest);            /* copie sous verrou */
esp_err_t parc_config_definir(const parc_config_t *c); /* borne nb/actif, persiste NVS (ESP), +1 génération */
void parc_etat_publier(uint8_t indice, const parc_etat_t *etat); /* indice>=PARC_MAX = no-op ; +1 génération */
void parc_etats_lire(parc_etat_t dest[PARC_MAX]);
uint32_t parc_generation(void);
void parc_charger(void); /* NVS -> RAM au boot (app_main) ; si nb==0, migration :
    l'hôte historique (ui_reglages_lire_hote-équivalent) devient l'entrée 0 "Printer 1" */
```
Verrou portMUX ESP / no-op host (patron usb_fichiers.c EXACT, y compris
copies bornées à l'utile sous verrou). Config+états en UNE instance PSRAM
paresseuse. NVS : namespace `"parc"`, clés `nb`, `actif`, `n0..n5`,
`h0..h5` — section `#ifdef ESP_PLATFORM`, même style que reglages.c ;
host : la persistance est no-op (RAM seule).

- [ ] **Étapes TDD** : test rouge (`test_parc.c` : lire/def config avec
  bornes nb>6→6 et actif>=nb→0 ; publier/lire états + génération ;
  publier indice 9 = no-op ; état `sonde=false` par défaut) → run rouge →
  implémenter → run vert. Test migration : hors ESP la migration retombe
  sur nb=0 (documenté, la logique NVS n'est pas simulée).

### Task 2 : parseur de sonde (`parc_parse`, pur)

**Files:** Create `firmware/main/apps/klipper/parc_parse.h/.c` (host-compilé
partout) ; Test : section dans `test_parc.c`.

**Produces :** `bool parc_parse_reponse(const char *json, size_t longueur,
parc_etat_t *sortie);` — parse la réponse de
`/printer/objects/query?print_stats&extruder&heater_bed&display_status` :
`result.status.print_stats.state` → `etat` (borné 15+NUL),
`extruder.temperature` → `buse`, `heater_bed.temperature` → `lit`,
`display_status.progress` (0..1) → `progression_pct` (0..100, borné).
Champs absents tolérés (valeurs à zéro, `etat` vide) ; JSON invalide →
false. cJSON, aucune allocation conservée.

- [ ] **Étapes TDD** : fixture JSON réaliste (copiée d'une réponse Moonraker
  réelle, raccourcie) + cas champs absents + cas invalide → rouge →
  implémentation → vert.

### Task 3 : sonde séquentielle (`parc_sonde`, ESP-only)

**Files:** Create `firmware/main/apps/klipper/parc_sonde.h/.c` (no-op host
complet, patron usb_scan.h) ; Modify main/CMakeLists (déjà globbé ? non :
liste explicite, ajouter).

**Produces :** `void parc_sonde_demarrage_paresseux(void);`
`void parc_sonde_activer(bool active);`

Tâche pérenne (pile 6144 en PSRAM via xTaskCreateWithCaps, prio
tskIDLE_PRIORITY+5, créée par demarrage_paresseux — patron usb_scan EXACT,
échec = pas de démarrage, retenté). Boucle : si `!g_active` → attendre le
sémaphore (activer(true) le donne ; activer(false) met juste le drapeau à
faux). Sinon : lire parc_config, choisir la PROCHAINE entrée round-robin
(hote non vide, indice != actif), requête
`http://<hote>/printer/objects/query?print_stats&extruder&heater_bed&display_status`
via esp_http_client éphémère (timeout_ms 1500, buffer réponse : scratch
PSRAM 4096 persistant), `parc_parse_reponse()` → `parc_etat_publier()`
(échec HTTP/timeout → publier `{sonde=true, atteignable=false}`), puis
`vTaskDelay(1000 ms)`. Une seule imprimante par tour, jamais l'active.

- [ ] Implémenter + gate compilation idf (pas de test host possible :
  esp_http_client ; le parseur, lui, est couvert par Task 2).

### Task 4 : écran Parc + entrée menu + ajout + bascule

**Files:** Create `firmware/main/apps/klipper/ecrans/ecran_parc.h/.c` ;
Modify `ecran_menu_reglages.c/.h` (case « Printers », idiome X-macro
existant), `app_main.c` (`parc_charger()` au boot, avant la boucle ;
`#include`), CMakeLists (main + host + sim) ;
Test : `host-test/tests/test_ecran_menu_reglages.c` (table +1 case),
compile host de ecran_parc.

**Comportement :**
- construire() : `parc_sonde_demarrage_paresseux(); parc_sonde_activer(true);`
  — detruire() : `parc_sonde_activer(false);`. Grille 2 col × 3 lignes de
  tuiles-boutons (bouton_creer-like, libellé multi-ligne
  `"<nom>\n<etat> <buse>°/<lit>°[ <pct>%]"`), bordure accent (couleur
  liaison EN_LIGNE) sur l'ACTIVE, tuile grisée `"?"`/`"unreachable"` si
  `!atteignable` ou `!sonde`. L'ACTIVE affiche l'état temps réel du store
  etat_klipper (via mettre_a_jour(etat,...)), jamais la sonde.
- Si nb < PARC_MAX : dernière tuile « + Add printer » → clavier_ouvrir nom
  → clavier_ouvrir adresse (validée par le MÊME
  `ecran_configuration_valider()` déjà exporté — vérifier sa visibilité,
  l'exporter dans ecran_configuration.h si besoin) → parc_config_definir.
- Tap tuile non active : `confirmation_ouvrir("Switch printer?", nom,
  "Switch", false, ...)` → confirmé : config.actif = indice,
  parc_config_definir(), PUIS recopier l'hôte de l'entrée dans le réglage
  hôte historique (`ui_reglages_definir_hote()` — c'est LUI que la boucle
  lit au boot) et `esp_restart()` après 300 ms (répondre à l'UI d'abord).
  Le redémarrage EST le chemin d'application existant (cf.
  ecran_configuration.c) ; un « appliquer sans reboot » reste hors
  périmètre, note dans le code.
- mettre_a_jour : générations parc + copie état actif ; libellés
  reconstruits systématiquement (pas d'incrémental).

- [ ] Étapes : test menu (rouge : table attend la case « Printers ») →
  implémentation écran + menu → tests verts + gates.

### Task 5 : gates finaux, revue, commit, build

- [ ] host 0 échec, idf vert, sim vert (sources ajoutées PARTOUT).
- [ ] revue `/code-review medium`, traiter les constats confirmés.
- [ ] commit unique `feat(parc): tableau de bord sequentiel + bascule
  d'imprimante active`, reconfigure+build, estampille vérifiée, ELF archivé
  (`builds/<hash>/`), mémoire projet (playbook validation : 2 machines
  réelles, bascule aller-retour, machine débranchée → tuile injoignable
  ≤1,5 s sans gel, heap_interne stable écran Parc ouvert).
