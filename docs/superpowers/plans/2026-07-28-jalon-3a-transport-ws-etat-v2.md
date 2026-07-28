# Jalon 3a — Transport WebSocket, état v2, preuve macro — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** L'état riche (8 extrudeurs, position, vitesse/flux, macros) arrive poussé par le WebSocket Moonraker avec repli HTTP intact, et la preuve de bout en bout est la doléance n°1 de la communauté levée : lister les macros de la machine et en lancer une depuis l'écran.

**Architecture:** Le contrat `backend_desc_t` ne change pas (spec §4) : le client WS tourne dans sa propre tâche et dépose des états fusionnés dans une boîte aux lettres ; `rafraichir()` — toujours appelé par la boucle du socle — draine la boîte quand le WS est en ligne et retombe sur le GET HTTP existant sinon. Tout le parsing JSON-RPC est en fonctions pures compilables sur PC. La cadence de la boucle devient adaptative (250 ms en WS, 1 s en repli), pilotée par une valeur fournie par le backend.

**Tech Stack:** ESP-IDF v5.5.5 · `espressif/esp_websocket_client` (composant géré, via `idf_component.yml`) · cJSON · harnais hôte WSL (ASan/UBSan) · simulateur LVGL/SDL · Docker `mainsail-crew/virtual-klipper-printer` (niveau de test 3, dégradable).

## Global Constraints

- **Le contrat `backend_desc_t` ne change pas de forme incompatible.** Le critère 8 de la spec (le backend jouet du 2b compile inchangé) est vérifié mécaniquement à la fin : tout ajout au contrat se fait par champ optionnel à sémantique zéro = comportement d'aujourd'hui.
- **`etat_klipper_t` reste un POD à taille fixe, sans pointeur** (memcmp + double tampon). ~2,5 Ko acceptés.
- **La boucle HTTP 1 Hz du 2a reste intégralement fonctionnelle en repli.** WS en panne ⇒ sondage HTTP, sans autre dégradation que la latence (critère 2).
- **`core/` sans FreeRTOS/ESP-IDF hors `esp_err.h`, sans LVGL** — inchangé. Les fichiers ESP-only (WS, HTTP) vivent sous `apps/klipper/` et sont exclus des builds PC dans les trois CMakeLists.
- **La tâche WS ne touche jamais ni l'état partagé ni LVGL** : elle ne parle qu'à la boîte aux lettres (spec §4).
- **Jamais de mise à jour optimiste.** Le résultat d'une commande vient de la réponse RPC corrélée ou du prochain état, jamais d'une anticipation locale.
- Invariants hérités : NVS partagée jamais effacée, aucun `ESP_ERROR_CHECK` nouveau, ordre de boot intact, pas d'identifiants ni de chemins locaux dans les fichiers suivis, commentaires français, textes UI anglais (ASCII), `-Wall -Wextra -Werror`.
- Harnais : `VERIFIER(condition)` un argument, libellés en commentaire. Suite actuelle : **639 vérifications, 0 échec** — chaque tâche finit plus haut et vert.
- Commandes : suite hôte `wsl -d Debian -- "/mnt/e/Dev/BTT KTouch Custom/host-test/run.sh"` (PowerShell, jamais Git Bash) ; build ESP `. "<chemin-vers-esp-idf>\export.ps1"; idf.py -C firmware build` (même invocation) ; le chemin ESP-IDF réel ne s'écrit jamais dans un fichier suivi.

## Une note sur la forme de ce plan

Comme au 2b : contrats d'en-tête et tests donnés en entier (c'est là que les
jalons précédents ont trouvé leurs défauts) ; le code d'intégration ESP
(client WS, glue esp_websocket_client) est spécifié par contraintes précises
et pièges nommés plutôt que recopié ligne à ligne — l'implémenteur a le
compilateur et les sources du composant sous les yeux, pas moi au moment
d'écrire.

## Leçons des jalons précédents qui s'appliquent directement ici

- La cible `lvgl` et les bibliothèques tierces sont compilées sans
  sanitizers : un test mémoire doit discriminer par les effets observables.
- Un test qui ne peut pas échouer est un défaut (RED réel exigé à chaque test
  nouveau ; mutation quand le RED naturel n'existe pas).
- Les structs de sortie s'écrivent ENTIÈREMENT sur tous les chemins.
- `sdkconfig.defaults` ne réécrit pas un `sdkconfig` existant ; les
  identifiants WiFi vivent dans `firmware/sdkconfig` (gitignoré) — toute
  régénération suit la danse documentée dans le rapport de tâche 10 du 2b.
- Les suites hôte à état singleton déclarent leurs dépendances d'ordre par
  les gardes nommées existantes (`habillage_est_construit()`…).

---

### Task 1: Branche + `etat_klipper_t` v2 + migration des consommateurs

**Files:**
- Modify: `firmware/main/core/etat_klipper.h`
- Modify: `firmware/main/core/backend_factice.c` (champs renommés)
- Modify: `firmware/main/apps/klipper/moonraker_parse.c` (mapping HTTP → v2)
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil.c` (buse → extrudeurs[0])
- Modify: `firmware/main/web.c` (`/state` expose la v2)
- Modify: `host-test/tests/test_moonraker_parse.c`, `test_ecran_accueil.c`, `test_backend_factice.c` (champs)
- Test: assertions de taille/POD dans `host-test/tests/test_contrat.c`

**Interfaces:**
- Consumes: l'existant du 2b.
- Produces: la structure que TOUTES les tâches suivantes consomment :

```c
#define KLIPPER_EXTRUDEURS_MAX 8
#define KLIPPER_VENTILATEURS_MAX 4
#define KLIPPER_MACROS_MAX 48
#define KLIPPER_MACRO_NOM_MAX 32

typedef struct { float actuelle; float consigne; bool presente; } klipper_chauffeur_t;
typedef struct { float vitesse; bool present; } klipper_ventilateur_t;

typedef struct {
    char  etat[KLIPPER_ETAT_TEXTE_MAX];

    klipper_chauffeur_t   extrudeurs[KLIPPER_EXTRUDEURS_MAX];
    uint8_t               nb_extrudeurs;    /* 0..8, nombre de `presente` */
    uint8_t               outil_actif;      /* index dans extrudeurs */
    klipper_chauffeur_t   plateau;          /* presente=false si pas de lit chauffant */
    klipper_ventilateur_t ventilateurs[KLIPPER_VENTILATEURS_MAX];

    float   position[3];                    /* X, Y, Z (mm) */
    uint8_t axes_references;                /* masque bit0=X bit1=Y bit2=Z */
    bool    deplacement_absolu;

    uint16_t vitesse_pct;                   /* M220, 100 = normal */
    uint16_t flux_pct;                      /* M221 */
    int32_t  babystep_z_um;                 /* µm signés (gcode offset Z) */

    char    macros[KLIPPER_MACROS_MAX][KLIPPER_MACRO_NOM_MAX];
    uint8_t nb_macros;
    bool    macros_tronquees;

    char     fichier[KLIPPER_FICHIER_MAX];
    float    progression;
    uint32_t temps_restant_s;
    bool     impression_en_cours;
    bool     impression_en_pause;
} etat_klipper_t;
```

Les champs `buse_*`/`plateau_*` du 2b disparaissent : `extrudeurs[0]` et
`plateau` les remplacent. C'est une migration, pas une couche de
compatibilité — le jouet du 2b a son propre type d'état et n'est pas touché
(le critère 8 tient par construction).

- [ ] **Step 1: Créer la branche**

```bash
git checkout -b jalon-3a-transport-ws jalon-2b-simulateur
```

- [ ] **Step 2: Écrire les tests de contrat d'abord**

Dans `test_contrat.c`, ajouter :

```c
/* v2 : toujours un POD memcmp-able. Pas de pointeur (verification par
 * inspection — un _Static_assert ne sait pas le dire en C11), taille bornee
 * et STABLE : si sizeof bouge, la personne qui l'a fait doit venir ici
 * l'assumer en connaissance de cause (double tampon + copie sous mutex
 * a chaque cycle). */
_Static_assert(sizeof(etat_klipper_t) < 3072, "etat v2 : budget ~2,5 Ko depasse");
_Static_assert(KLIPPER_EXTRUDEURS_MAX == 8, "dimensionnement acte au brainstorming jalon 3");
```

Dans `test_backend_factice.c` : adapter chaque lecture `buse_actuelle` →
`extrudeurs[0].actuelle` etc., et ajouter :

```c
/* Un scenario mono-extrudeur remplit nb_extrudeurs et presente. */
VERIFIER(etat.nb_extrudeurs == 1);
VERIFIER(etat.extrudeurs[0].presente == true);
VERIFIER(etat.extrudeurs[1].presente == false);
```

- [ ] **Step 3: RED** — la suite ne compile plus (champs disparus). C'est le
  RED attendu d'une migration ; le consigner tel quel.

- [ ] **Step 4: Migrer** `etat_klipper.h` puis chaque consommateur, dans
  l'ordre : `backend_factice.c` (remplit `extrudeurs[0]` + compteurs),
  `moonraker_parse.c` (le mapping HTTP existant remplit `extrudeurs[0]`,
  `plateau`, et les nouveaux champs qu'il sait déjà lire : `print_stats`
  inchangé ; les champs que le GET actuel ne connaît pas — position,
  vitesse/flux, macros — restent à zéro ici, ils arrivent en tâche 3),
  `ecran_accueil.c` (tuiles sur `extrudeurs[0]`/`plateau`),
  `web.c` (`/state` sérialise la v2 : tableau `extrudeurs` des seuls
  présents, macros en tableau de chaînes).

- [ ] **Step 5: Vert + build ESP + commit**

Suite hôte verte (au-dessus de 639), `idf.py -C firmware build` vert.

```bash
git commit -m "feat(core): etat_klipper_t v2 - 8 extrudeurs, position, macros"
```

---

### Task 2: Backend factice — scénarios paliers (CR-10, U1, 8 têtes)

**Files:**
- Modify: `firmware/main/core/backend_factice.{c,h}`
- Test: `host-test/tests/test_backend_factice.c`
- Modify: `simulateur/README.md` (numérotation)

**Interfaces:**
- Consumes: état v2 (tâche 1).
- Produces: scénarios `10` (« CR-10 » : 1 extrudeur, grand plateau, 4 macros
  simples), `11` (« U1 » : 4 extrudeurs, outil actif qui tourne, 8 macros
  dont `_CACHEE` — filtrée par l'UI plus tard —, `PURGE_PARAM` à paramètres
  et `MACRO_ECHEC` qui échoue), `12` (8 têtes synthétiques, 48 macros +
  `macros_tronquees` levé). Les scénarios 0-9 du 2b sont inchangés.

- [ ] **Step 1: Tests d'abord** — pour chaque scénario : `nb_extrudeurs`
  attendu, présence/absence, `outil_actif` qui évolue entre cycles (U1),
  liste de macros exacte (`VERIFIER_TEXTE` sur macros[0] et la dernière),
  `macros_tronquees` vrai seulement au scénario 12, et
  `commande(BACKEND_ACTION_MACRO, "{\"nom\":\"MACRO_ECHEC\"}")` rend
  `ESP_FAIL` quand `commande(.., "PURGE_PARAM")` rend `ESP_OK`.
- [ ] **Step 2: RED observé, implémentation, vert.**
- [ ] **Step 3: Commit** `feat(core): scenarios paliers du backend factice (CR-10, U1, 8 tetes)`

---

### Task 3: `moonraker_rpc.c` — le protocole en fonctions pures

**Files:**
- Create: `firmware/main/apps/klipper/moonraker_rpc.{c,h}`
- Test: `host-test/tests/test_moonraker_rpc.c`
- Modify: `host-test/CMakeLists.txt`, `simulateur/CMakeLists.txt`, `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: cJSON (déjà dans le harnais), état v2.
- Produces (consommé par les tâches 4, 5, 6) :

```c
/* Construit une requete JSON-RPC 2.0. `id` est fourni par l'appelant (le
 * correlateur de la tache 5 les genere) ; rend false si le tampon est trop
 * court (jamais de troncature silencieuse — lecon du 2b). */
bool rpc_construire_requete(char *sortie, size_t taille, uint32_t id,
                            const char *methode, const char *params_json);

/* Payload d'abonnement aux objets dont l'etat v2 a besoin (toolhead,
 * gcode_move, extruder..extruder7, heater_bed, fan, print_stats,
 * virtual_sdcard, webhooks, speed_factor/extrude_factor via gcode_move). */
bool rpc_construire_abonnement(char *sortie, size_t taille, uint32_t id);

typedef enum {
    RPC_MSG_REPONSE,          /* result ou error, avec id */
    RPC_MSG_STATUS_UPDATE,    /* notify_status_update */
    RPC_MSG_KLIPPY_READY,     /* notify_klippy_ready */
    RPC_MSG_KLIPPY_DECONNECTE,
    RPC_MSG_AUTRE,            /* notification ignoree */
    RPC_MSG_INVALIDE,
} rpc_message_type_t;

/* Classifie un message entrant sans le consommer. */
rpc_message_type_t rpc_classifier(const char *json, size_t longueur, uint32_t *id_sortie);

/* Fusionne un notify_status_update DANS un etat existant (mise a jour
 * partielle : Moonraker ne pousse que ce qui a change). Rend false et ne
 * touche RIEN si le JSON est invalide. C'est la fonction centrale du jalon :
 * elle est testee champ par champ. */
bool rpc_fusionner_status(etat_klipper_t *etat, const char *json, size_t longueur);

/* Extrait resultat/erreur d'une reponse correlee. `erreur_texte` recoit le
 * message Klipper (borne, jamais tronque silencieusement en plein UTF-8). */
bool rpc_lire_reponse(const char *json, size_t longueur, bool *succes,
                      char *erreur_texte, size_t taille_erreur);

/* Extrait la liste des macros depuis une reponse printer.objects/list ou
 * configfile (les `_prefixees` NE sont PAS filtrees ici : c'est un choix
 * d'affichage, pas de protocole — l'UI filtrera). Tronque a
 * KLIPPER_MACROS_MAX avec macros_tronquees. */
bool rpc_lire_macros(etat_klipper_t *etat, const char *json, size_t longueur);
```

- [ ] **Step 1: Tests d'abord, exhaustifs** — c'est ici que vivront les bugs
  du jalon. Cas minimum (chacun en RED observé d'abord) :
  - requête : forme exacte `{"jsonrpc":"2.0","method":...,"params":...,"id":N}`,
    params NULL ⇒ absent, tampon court ⇒ false ;
  - classifier : réponse avec id, status_update, klippy_ready/déconnecté,
    notification inconnue ⇒ AUTRE, JSON invalide ⇒ INVALIDE, id absent sur
    une réponse ⇒ INVALIDE ;
  - fusion : mise à jour partielle (un seul champ poussé ⇒ les autres
    INTACTS — le cœur du contrat), extruder2 présent ⇒ `presente` et
    `nb_extrudeurs` recalculés, `homed_axes:"xyz"` ⇒ masque 7, `""` ⇒ 0,
    speed_factor 1.5 ⇒ 150 %, valeurs non finies ⇒ champ inchangé (jamais un
    NaN dans l'état — leçon 2a), JSON hostile (imbrication 40 niveaux,
    troncature en plein milieu) ⇒ false et état INTACT (copie avant/memcmp
    après dans le test) ;
  - réponse : result ok, error avec message Klipper (`//` le texte revient
    borné), error sans message ;
  - macros : liste nominale, 50 macros ⇒ 48 + tronquées, `_CACHEE` bien
    PRÉSENTE (le filtre est ailleurs).
- [ ] **Step 2: Implémentation, vert, commit**
  `feat(klipper): moonraker_rpc - le protocole JSON-RPC en fonctions pures`

---

### Task 4: Fixtures réelles — virtual-klipper-printer

**Files:**
- Create: `tools/moonraker-record/enregistrer.py` (+ `README.md`)
- Create: `host-test/fixtures/moonraker/*.jsonl` (transcripts)
- Test: `host-test/tests/test_moonraker_rpc.c` (section rejeu)
- Modify: `docs/hardware/flashing.md` ou nouveau `docs/dev/klipper-simule.md`

**Pourquoi.** Les tests de la tâche 3 encodent MA lecture de la doc
Moonraker. Un transcript enregistré contre un vrai Moonraker encode la
réalité. Le rejeu des deux dans les mêmes fonctions est ce qui empêche le
jalon de découvrir le protocole réel sur l'appareil.

- [ ] **Step 1: Docker + vkp dans WSL.** `docker` est-il disponible dans la
  Debian WSL ? (`docker --version`). Sinon : **s'arrêter et demander** —
  l'installation demande sudo et une décision utilisateur (Docker Desktop vs
  docker-ce dans WSL). Si disponible :

```bash
git clone https://github.com/mainsail-crew/virtual-klipper-printer /tmp/vkp
cd /tmp/vkp && docker compose up -d
curl -s localhost:7125/server/info   # Moonraker repond
```

- [ ] **Step 2: L'enregistreur** — `enregistrer.py` (python3 + `websockets`
  dans un venv local à `tools/`) : se connecte à `ws://localhost:7125/websocket`,
  envoie identify + subscribe (les MÊMES payloads que
  `rpc_construire_abonnement` — généré en appelant le binaire de test ou
  recopié à l'octet près avec un commentaire de synchronisation), enregistre
  chaque message dans un `.jsonl` horodaté. Scénarios enregistrés : connexion
  + abonnement + 30 s d'idle ; un chauffage de buse ; un `printer.gcode.script`
  avec une macro existante ; une macro inexistante (l'erreur Klipper réelle).
- [ ] **Step 3: Rejeu dans le harnais** — nouvelle section de
  `test_moonraker_rpc.c` : lit chaque fixture ligne à ligne,
  `rpc_classifier` ne rend jamais INVALIDE, les status_update fusionnent
  sans jamais rendre false, l'état final du scénario chauffage montre la
  consigne montée. Les fixtures sont suivies par git (quelques dizaines de
  Ko, anonymes par construction — vérifier qu'aucun hostname/IP locale n'y
  figure avant commit).
- [ ] **Step 4: Documenter** le montage vkp (fichier docs), **commit**
  `test(klipper): fixtures enregistrees contre un vrai Moonraker (vkp)`.

**Dégradation assumée :** si Docker est indisponible et que l'utilisateur ne
tranche pas pendant l'exécution, la tâche livre l'enregistreur + la doc, et
les fixtures attendent — consigné au registre, jamais présenté comme fait.

---

### Task 5: Boîte aux lettres + backend Moonraker en mode WS + cadence adaptative

**Files:**
- Create: `firmware/main/apps/klipper/moonraker_ws.{c,h}` (ESP-only : esp_websocket_client)
- Create: `firmware/main/apps/klipper/moonraker_boite.{c,h}` (portable, testée sur PC)
- Modify: `firmware/main/apps/klipper/backend_moonraker.c` (drain + repli + commandes RPC)
- Modify: `firmware/main/core/backend.h` (champ optionnel `periode_ms`)
- Modify: `firmware/main/core/boucle.c` (cadence lue depuis le backend)
- Modify: `firmware/main/idf_component.yml` (`espressif/esp_websocket_client`)
- Test: `host-test/tests/test_moonraker_boite.c`, extension `test_boucle_cycle.c`

**Interfaces:**
- Produces :

```c
/* moonraker_boite.h — un slot, pas une file : un etat fusionne ECRASE le
 * precedent (on ne rejoue pas l'historique, spec §4). Portable : le verrou
 * est fourni par l'appelant cote ESP (la boite ne connait pas FreeRTOS),
 * les tests hote l'appellent sans verrou, mono-thread. */
void boite_deposer(moonraker_boite_t *b, const etat_klipper_t *etat);
bool boite_drainer(moonraker_boite_t *b, etat_klipper_t *sortie); /* false si vide */
bool boite_a_du_neuf(const moonraker_boite_t *b);

/* backend.h — ajout OPTIONNEL au contrat (zero = comportement actuel) : */
typedef struct {
    ...champs existants inchangés...
    /* Periode d'interrogation souhaitee en ms. 0 = defaut du socle (1000).
     * Le backend Moonraker rend 250 quand le WS est en ligne, 1000 en repli
     * HTTP. Lu par la boucle A CHAQUE cycle (la valeur peut changer). */
    uint32_t (*periode_ms)(void *etat);
} backend_desc_t;
```

- [ ] **Step 1: La boîte, tests d'abord** (portable, host) : dépôt puis
  drain rend le dernier état ; deux dépôts ⇒ un seul drain (écrasement) ;
  drain sur vide ⇒ false et sortie intacte ; `a_du_neuf` bascule.
- [ ] **Step 2: La cadence, tests d'abord** — dans `test_boucle_cycle.c` :
  un `backend_desc_t` avec `periode_ms = NULL` ⇒ la boucle garde 1000 (le
  jouet du 2b passe ce chemin : critère 8) ; un backend qui rend 250 ⇒ la
  valeur est consultée à chaque cycle. La partie FreeRTOS (`vTaskDelay`)
  reste dans le shell non portable ; la décision (quelle période) est une
  fonction pure extraite et testée.
- [ ] **Step 3: `moonraker_ws.c`** (ESP-only, exclu des builds PC) —
  contraintes :
  - `esp_websocket_client` configuré sur `ws://<adresse>:<port>/websocket`
    (l'adresse vient de `backend_hote_t`, crochets IPv6 comme le HTTP) ;
  - à `WEBSOCKET_EVENT_CONNECTED` : identify puis abonnement (payloads des
    fonctions pures de la tâche 3) ;
  - à `WEBSOCKET_EVENT_DATA` : réassemblage des trames fragmentées dans un
    tampon statique borné (4 Kio comme le HTTP ; dépassement ⇒ message
    ignoré + journal throttlé — jamais un débordement), puis
    `rpc_classifier` ; status_update ⇒ copie de l'état courant de la boîte,
    `rpc_fusionner_status`, `boite_deposer` (sous le verrou fourni par
    backend_moonraker) ; réponse corrélée ⇒ remise au corrélateur ;
  - reconnexion : backoff 1 s → 2 → 4 → … plafonné 30 s, ré-identify +
    ré-abonnement à chaque retour (critère 2), compteur exposé pour le
    journal ;
  - la tâche WS n'appelle JAMAIS `etat_store_*`, `boucle_*`, ni LVGL.
- [ ] **Step 4: `backend_moonraker.c`** — `rafraichir()` : WS en ligne
  ⇒ drainer la boîte (rien de neuf ⇒ garder l'état précédent en le
  recopiant dans le tampon zéroïsé — attention au contrat du tampon neuf,
  le backend garde sa dernière image en statique comme le factice garde sa
  progression) ; WS hors ligne ⇒ GET HTTP existant. `commande()` : WS en
  ligne ⇒ RPC corrélé (id généré, timeout borné, le résultat Klipper réel
  remonte par le seam d'échec du 2b) ; sinon POST HTTP existant.
  `periode_ms()` : 250 / 1000. La liaison : WS connecté ET klippy ready ⇒
  succès de cycle ; WS connecté mais klippy down ⇒ échec (l'existant HTTP
  fait déjà cette distinction — la conserver).
- [ ] **Step 5: Build ESP vert, suite hôte verte, commit**
  `feat(klipper): transport WebSocket avec repli HTTP et cadence adaptative`

---

### Task 6: L'écran macros — la preuve de bout en bout

**Files:**
- Create: `firmware/main/apps/klipper/ecrans/ecran_macros.{c,h}`
- Modify: `firmware/main/apps/klipper/ecrans/ecran_accueil.c` (bouton « Macros »)
- Modify: `firmware/main/apps/klipper/backend_moonraker.c` + `backend_factice.c` (action `BACKEND_ACTION_MACRO`)
- Modify: `simulateur/main.c` (scénarios 10-12 accessibles)
- Test: `host-test/tests/test_ecran_macros.c`

**Interfaces:**
- Consumes: état v2 (`macros`, `nb_macros`, `macros_tronquees`), navigation,
  habillage, `ui_commander`.
- Produces: `const ecran_desc_t ECRAN_MACROS;` et l'action
  `BACKEND_ACTION_MACRO` (arguments_json : `{"nom":"<macro>"}`) — Moonraker :
  `printer.gcode.script {script:"<nom>"}`.

- [ ] **Step 1: Tests d'abord** — liste rendue depuis l'état (une entrée par
  macro, `_préfixées` FILTRÉES ici — test : `_CACHEE` du scénario U1
  n'apparaît pas), `macros_tronquees` ⇒ une ligne d'avertissement honnête,
  tap ⇒ `ui_commander(BACKEND_ACTION_MACRO, ...)` avec le bon nom (trace du
  seam), échec (MACRO_ECHEC) ⇒ bannière rouge portant le nom, liste vide ⇒
  texte « No macros » (jamais un écran muet), grisage `donnees_perimees`
  intégral, boutons désactivés en style RÉSOLU (leçon de la revue finale 2b).
- [ ] **Step 2: RED, implémentation** — liste paginée simple (les 48 max
  tiennent en 3 pages de 16 ; le widget paresseux de 3d n'est PAS anticipé,
  YAGNI), bouton Macros sur l'accueil (visible si `nb_macros > 0`).
- [ ] **Step 3: Captures** — scénario 11 (U1) : liste avec macros, une
  lancée ⇒ notification succès ; MACRO_ECHEC ⇒ bannière rouge ; scénario 12 :
  l'avertissement de troncature. Ouvertes, décrites, laissées sur disque.
- [ ] **Step 4: Vert, build ESP, commit**
  `feat(klipper): ecran macros - la doleance n1 levee`

---

### Task 7: Vérification mécanique du critère 8 + `--hote` (dégradable)

**Files:**
- Modify: `simulateur/main.c` (+ `--hote <adresse:port>`, PC-HTTP seulement)
- Create: `simulateur/moonraker_pc.c` (transport HTTP PC minimal via sockets POSIX — PAS de libcurl : dépendance nouvelle refusée sans décision utilisateur)
- Test: rejeu des fixtures tâche 4 contre le parsing (déjà fait) + vérification manuelle contre vkp si Docker disponible

- [ ] **Step 1: Critère 8, littéralement** :

```bash
git diff --stat jalon-2b-simulateur..HEAD -- exemples/
# attendu : vide — le jouet n'a pas bougé
wsl -d Debian -- bash "/mnt/e/Dev/BTT KTouch Custom/simulateur/run.sh" --app jouet --capture <abs>/jouet-3a.png --cycles 5
# attendu : identique au 2b — le contrat n'a pas casse le fork
```

- [ ] **Step 2: `--hote` PC** — un GET `objects/query` en sockets POSIX nus
  (HTTP/1.0, pas de keep-alive, ~80 lignes) qui alimente le MÊME
  `moonraker_parse_status()` que la cible, à 1 Hz. Le WS PC n'est PAS
  implémenté (l'app ESP le fait ; sur PC les fixtures couvrent le parsing —
  YAGNI jusqu'à preuve du contraire). Si vkp tourne : capture de l'écran
  accueil branché sur le VRAI Moonraker, décrite au rapport — c'est la
  comparaison différée depuis le 2a, enfin exécutée (niveau 3 de la spec §8).
- [ ] **Step 3: Commit** `feat(sim): --hote branche le simulateur sur un vrai Moonraker`

---

## Ce que cette tranche ne fait pas

Aucun écran au-delà de `ecran_macros` (3b/3c), pas de fichiers ni miniatures
(3d), pas de panneaux (3e), pas de parc (3f), pas de macros à paramètres
(fin de 3b), pas de WS côté PC (fixtures + ESP suffisent), pas de libcurl ni
autre dépendance PC nouvelle sans décision utilisateur.
