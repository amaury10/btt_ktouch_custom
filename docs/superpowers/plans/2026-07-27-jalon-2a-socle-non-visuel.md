# Jalon 2a — Socle non visuel — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un firmware qui interroge une vraie machine Klipper, interprète sa réponse dans un état typé, et l'expose sur `/state` — le tout sans une seule ligne d'interface graphique.

**Architecture:** Le socle possède la boucle : il instancie le backend configuré, l'interroge périodiquement dans une tâche dédiée, range le résultat dans un double tampon, et tient une machine à états de connexion. Un backend fournit `demarrer`/`rafraichir`/`arreter`/`commande` et rien d'autre. Toute la logique — analyse JSON, détection de changement, transitions de connexion — est écrite en C portable et testée sur PC, sans matériel.

**Tech Stack:** ESP-IDF v5.5.5 · cJSON (fourni par IDF) · esp_http_client · WSL Debian 13 pour les tests hôte · Python 3.14 + pytest pour l'outillage existant.

## Global Constraints

Ces contraintes s'appliquent à **toutes** les tâches, sans rappel.

- **Le socle alloue l'état, jamais le backend.** `backend_desc_t.taille_etat` déclare la taille ; le socle appelle `calloc` une fois. Un backend qui alloue dans `rafraichir` est un défaut, pas un choix.
- **La tâche réseau ne touche jamais LVGL.** Ce jalon n'a pas d'interface, mais la règle est posée dès maintenant : aucune inclusion de `lvgl.h` sous `core/`, hors du sous-jalon 2b.
- **La partition `nvs` est partagée avec le firmware d'origine.** Les réglages vivent dans l'espace de noms `ktouch`, et **`nvs_flash_erase()` ne doit apparaître nulle part**. Aucune configuration WiFi n'est jamais persistée (`WIFI_STORAGE_RAM` reste posé après `esp_wifi_init`, acquis au jalon 1).
- **Aucune écriture en partition applicative.** Le moteur de mise à jour est hors périmètre de ce sous-jalon.
- **Aucune défaillance locale n'est fatale.** Pas d'`ESP_ERROR_CHECK` sur le réseau, le backend ou le serveur : on journalise et on continue, pour qu'une panne reste diagnosticable à distance.
- **Le sauvetage du jalon 1 reste armé et intact** : minuteur, compteur de démarrages en mémoire RTC, `/revert`.
- **Aucun identifiant ni chemin local dans un fichier suivi.** `firmware/sdkconfig` est exclu et contient les identifiants WiFi.
- Commentaires et docstrings en français ; identifiants publics en anglais dans les nouveaux fichiers de `core/`.
- ESP-IDF s'active en sourçant `export.ps1` **dans la même invocation** que toute commande `idf.py` ; le chemin d'installation est propre à la machine, ne pas l'écrire en dur dans un fichier suivi.

---

### Task 1: Environnement de test hôte

**Files:**
- Create: `host-test/CMakeLists.txt`
- Create: `host-test/README.md`
- Create: `host-test/tests/test_harnais.c`
- Create: `host-test/unity/unity_config.h`

**Interfaces:**
- Consumes: rien.
- Produces: la commande `./host-test/run.sh` (exécutée sous WSL) qui compile et lance la suite de tests C, et rend un code de sortie non nul en cas d'échec. Toutes les tâches suivantes y ajoutent leurs tests.

**Pourquoi ce n'est pas un détail.** Les analyseurs JSON sont l'endroit où vivront la plupart des bugs de ce jalon, parce que c'est là qu'on interprète le JSON parfois surprenant de Moonraker. Les tester sur PC prend une seconde ; les tester sur l'appareil demande un flash, un redémarrage et une lecture de journal à distance. La différence de vitesse décide de la qualité du résultat.

> **Cette tâche demande une action de l'utilisateur.** L'installation des paquets exige `sudo`, qui réclame un mot de passe. L'agent ne peut pas la faire seul et doit s'arrêter pour la demander.

- [ ] **Step 1: Faire installer les outils de compilation dans WSL**

Demander à l'utilisateur d'exécuter, depuis son shell :

```
! wsl -d Debian -- sudo apt update
! wsl -d Debian -- sudo apt install -y build-essential cmake ninja-build pkg-config
```

Vérification, sans `sudo` :

```powershell
wsl -d Debian -- bash -c "gcc --version | head -1; cmake --version | head -1"
```

Expected: une version de `gcc` et une de `cmake`. Tant que ce n'est pas le cas, **ne pas continuer** : les tâches suivantes sont toutes bâties dessus.

- [ ] **Step 2: Écrire le harnais**

`host-test/CMakeLists.txt` — compile Unity (fourni par ESP-IDF, mais on ne dépend pas d'ESP-IDF ici : on le vendorise pas, on écrit un harnais minimal autonome pour rester simple) :

```cmake
cmake_minimum_required(VERSION 3.16)
project(ktouch_host_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g")

# Les sources testées vivent sous firmware/main/core et doivent rester
# compilables hors ESP-IDF : c'est la contrainte qui garantit qu'elles ne
# dependent pas du materiel.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main/core)

add_library(cjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_executable(tests
    tests/main.c
    tests/test_harnais.c
)
target_link_libraries(tests PRIVATE cjson)
target_include_directories(tests PRIVATE ${CORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_test(NAME suite COMMAND tests)
```

`host-test/tests/petit_test.h` — un micro-cadre de test, une quarantaine de lignes, plutôt que de traîner Unity :

```c
/* Micro-cadre de test : suffisant pour des fonctions pures, et sans dépendance
 * externe — le harnais doit rester trivial à faire fonctionner chez un
 * contributeur qui découvre le dépôt. */
#pragma once

#include <stdio.h>
#include <string.h>

extern int tests_echoues;
extern int tests_lances;

#define VERIFIER(condition)                                                   \
    do {                                                                      \
        tests_lances++;                                                       \
        if (!(condition)) {                                                   \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                     \
    } while (0)

#define VERIFIER_FLOAT(obtenu, attendu, tolerance)                            \
    do {                                                                      \
        tests_lances++;                                                       \
        float _d = (obtenu) - (attendu);                                      \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tolerance)) {                                               \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s = %f, attendu %f\n",                   \
                   __FILE__, __LINE__, #obtenu, (double)(obtenu),             \
                   (double)(attendu));                                        \
        }                                                                     \
    } while (0)

#define VERIFIER_TEXTE(obtenu, attendu)                                       \
    do {                                                                      \
        tests_lances++;                                                       \
        if (strcmp((obtenu), (attendu)) != 0) {                               \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s = \"%s\", attendu \"%s\"\n",           \
                   __FILE__, __LINE__, #obtenu, (obtenu), (attendu));         \
        }                                                                     \
    } while (0)
```

`host-test/tests/main.c` :

```c
#include <stdio.h>

int tests_echoues = 0;
int tests_lances = 0;

void suite_harnais(void);

int main(void)
{
    suite_harnais();

    printf("\n%d verification(s), %d echec(s)\n", tests_lances, tests_echoues);
    return tests_echoues == 0 ? 0 : 1;
}
```

`host-test/tests/test_harnais.c` — prouve que le harnais détecte bien un échec, ce qui est la seule chose qu'un harnais neuf doit prouver :

```c
#include "petit_test.h"

void suite_harnais(void)
{
    printf("suite : harnais\n");
    VERIFIER(1 + 1 == 2);
    VERIFIER_FLOAT(0.1f + 0.2f, 0.3f, 0.0001f);
    VERIFIER_TEXTE("k-touch", "k-touch");
}
```

`host-test/run.sh` :

```sh
#!/bin/sh
# Compile et lance la suite de tests hôte. À exécuter sous WSL.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja >/dev/null
cmake --build build >/dev/null
./build/tests
```

- [ ] **Step 3: Récupérer cJSON pour l'hôte**

L'implémentation testée utilisera cJSON, qu'ESP-IDF fournit sur cible mais pas sur PC. Copier les deux fichiers depuis l'installation ESP-IDF vers `host-test/vendor/` :

```powershell
$idf = "<chemin-vers-esp-idf>"
New-Item -ItemType Directory -Force "host-test\vendor" | Out-Null
Copy-Item "$idf\components\json\cJSON\cJSON.c","$idf\components\json\cJSON\cJSON.h" "host-test\vendor\"
```

> cJSON est sous licence MIT et son fichier `LICENSE` existe réellement — contrairement au BSP de BTT. La copie est donc licite. Ajouter `host-test/vendor/LICENSE` en le copiant depuis la même source.

- [ ] **Step 4: Lancer la suite**

```powershell
wsl -d Debian -- sh /mnt/e/.../host-test/run.sh
```

Expected: `3 verification(s), 0 echec(s)` et un code de sortie nul.

> Le chemin exact dépend de l'emplacement du dépôt ; le construire à partir de `wsl -d Debian -- wslpath "$PWD"` plutôt que de l'écrire en dur.

- [ ] **Step 5: Vérifier que le harnais sait échouer**

Ajouter temporairement `VERIFIER(1 == 2);` dans `suite_harnais`, relancer, constater `1 echec(s)` et un code de sortie non nul, puis retirer la ligne. **Un harnais qui ne sait pas échouer est pire qu'aucun harnais** : il donne une confiance non fondée à toutes les tâches suivantes.

- [ ] **Step 6: Documenter et commiter**

`host-test/README.md` — explique à quoi sert le harnais, quels paquets installer sous WSL, comment le lancer, et surtout **pourquoi** les sources de `core/` doivent rester compilables hors ESP-IDF.

```bash
git add host-test/
git commit -m "test(host): harnais de test C sur PC"
```

---

### Task 2: Le contrat de backend et le modèle d'état Klipper

**Files:**
- Create: `firmware/main/core/backend.h`
- Create: `firmware/main/core/etat_klipper.h`
- Test: `host-test/tests/test_contrat.c`
- Modify: `host-test/CMakeLists.txt`, `host-test/tests/main.c`

**Interfaces:**
- Consumes: le harnais de la tâche 1.
- Produces: `backend_hote_t`, `backend_desc_t`, `etat_klipper_t`, et les constantes `BACKEND_ACTION_*`. Toutes les tâches suivantes en dépendent.

Cette tâche ne contient presque pas de logique : elle fixe les types. C'est volontaire — ce sont les signatures que le fork astro devra respecter, et les changer plus tard coûtera cher.

- [ ] **Step 1: Écrire les en-têtes**

`firmware/main/core/backend.h` :

```c
/* Contrat que remplit une application pour parler à sa machine.
 *
 * Le socle possède la boucle : il alloue l'état, appelle `rafraichir`
 * périodiquement, et range le résultat. Un backend ne connaît ni l'affichage,
 * ni la navigation, ni la persistance — seulement son protocole. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define BACKEND_HOTE_LONGUEUR_MAX 64

typedef struct {
    char     adresse[BACKEND_HOTE_LONGUEUR_MAX]; /* nom ou IPv4, sans schéma */
    uint16_t port;
} backend_hote_t;

typedef struct {
    const char *nom;          /* "moonraker", "factice" — stocké dans les réglages */
    size_t      taille_etat;  /* le socle alloue ; le backend n'alloue jamais */

    esp_err_t (*demarrer)(void *etat, const backend_hote_t *hote);
    esp_err_t (*rafraichir)(void *etat);
    void      (*arreter)(void *etat);

    /* `arguments_json` vaut NULL quand l'action n'en prend pas. */
    esp_err_t (*commande)(void *etat, const char *action, const char *arguments_json);
} backend_desc_t;

/* Actions communes. Un backend qui n'en gère pas une rend ESP_ERR_NOT_SUPPORTED
 * plutôt que d'échouer silencieusement — l'interface doit pouvoir griser un
 * bouton en le sachant. */
#define BACKEND_ACTION_PAUSE      "pause"
#define BACKEND_ACTION_REPRENDRE  "reprendre"
#define BACKEND_ACTION_ANNULER    "annuler"
#define BACKEND_ACTION_URGENCE    "arret_urgence"
```

`firmware/main/core/etat_klipper.h` :

```c
/* État d'une machine Klipper tel que l'interface le consomme.
 *
 * Structure POD, taille fixe, sans pointeur : c'est ce qui permet au socle de
 * détecter un changement par simple comparaison mémoire (voir etat_store.h) et
 * d'allouer une fois pour toutes. Ajouter un champ est sans danger ; ajouter un
 * pointeur casserait la détection de changement. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KLIPPER_ETAT_TEXTE_MAX  24
#define KLIPPER_FICHIER_MAX     64

typedef struct {
    char  etat[KLIPPER_ETAT_TEXTE_MAX];   /* "ready", "printing", "paused", "error" */

    float buse_actuelle;
    float buse_consigne;
    float plateau_actuel;
    float plateau_consigne;

    char     fichier[KLIPPER_FICHIER_MAX];
    float    progression;                  /* 0.0 à 1.0 */
    uint32_t temps_restant_s;              /* 0 si inconnu */

    bool impression_en_cours;
    bool impression_en_pause;
} etat_klipper_t;
```

- [ ] **Step 2: Écrire le test des invariants du modèle**

`host-test/tests/test_contrat.c` — ces tests paraissent triviaux ; ils gardent en réalité l'invariant qui rend la détection de changement possible :

```c
#include <string.h>

#include "etat_klipper.h"
#include "petit_test.h"

void suite_contrat(void)
{
    printf("suite : contrat\n");

    /* La comparaison mémoire du magasin d'état n'est valable que si la
     * structure est comparable octet à octet : pas de pointeur, taille stable. */
    etat_klipper_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    VERIFIER(memcmp(&a, &b, sizeof(a)) == 0);

    a.buse_actuelle = 210.0f;
    VERIFIER(memcmp(&a, &b, sizeof(a)) != 0);

    /* Deux structures remplies identiquement champ par champ doivent être
     * indistinguables : si ce test échoue un jour, c'est qu'un champ non
     * initialisé ou un pointeur s'est glissé dans le modèle. */
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    snprintf(a.fichier, sizeof(a.fichier), "piece.gcode");
    snprintf(b.fichier, sizeof(b.fichier), "piece.gcode");
    a.progression = b.progression = 0.5f;
    VERIFIER(memcmp(&a, &b, sizeof(a)) == 0);
}
```

- [ ] **Step 3: Enregistrer la suite**

Ajouter `void suite_contrat(void);` et son appel dans `host-test/tests/main.c`, et `tests/test_contrat.c` dans le `add_executable` du `CMakeLists.txt`.

- [ ] **Step 4: Lancer**

Run: `wsl -d Debian -- sh <chemin>/host-test/run.sh`
Expected: les vérifications du harnais **et** du contrat passent, 0 échec.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/core/backend.h firmware/main/core/etat_klipper.h host-test/
git commit -m "feat(core): contrat de backend et modele d'etat Klipper"
```

---

### Task 3: L'analyseur Moonraker

**Files:**
- Create: `firmware/main/core/../apps/klipper/moonraker_parse.c`, `moonraker_parse.h` (chemin exact : `firmware/main/apps/klipper/`)
- Test: `host-test/tests/test_moonraker_parse.c`
- Modify: `host-test/CMakeLists.txt`, `host-test/tests/main.c`

**Interfaces:**
- Consumes: `etat_klipper_t` de la tâche 2.
- Produces: `bool moonraker_parse_status(const char *json, size_t longueur, etat_klipper_t *sortie)` — rend `false` si le JSON est inexploitable, et **ne modifie alors pas** `sortie`.

C'est le cœur du jalon, et la raison d'être du harnais : tout se teste ici, sans matériel.

**La requête** que le backend émettra :

```
GET /printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard&webhooks
```

**La réponse**, dans sa forme nominale :

```json
{"result":{"status":{
  "extruder":{"temperature":210.4,"target":210.0},
  "heater_bed":{"temperature":60.1,"target":60.0},
  "print_stats":{"filename":"piece.gcode","state":"printing","print_duration":600.0},
  "virtual_sdcard":{"progress":0.42,"is_active":true},
  "webhooks":{"state":"ready"}
}}}
```

**Le temps restant n'est pas fourni par Moonraker** et doit être estimé. La formule retenue est celle du temps écoulé rapporté à la progression : `restant = ecoule * (1 - p) / p`. Elle est grossière en début d'impression, ce qui impose de la neutraliser sous 1 % de progression plutôt que d'afficher une valeur absurde.

- [ ] **Step 1: Écrire les tests**

`host-test/tests/test_moonraker_parse.c` :

```c
#include <string.h>

#include "moonraker_parse.h"
#include "petit_test.h"

static const char *REPONSE_IMPRESSION =
    "{\"result\":{\"status\":{"
    "\"extruder\":{\"temperature\":210.4,\"target\":210.0},"
    "\"heater_bed\":{\"temperature\":60.1,\"target\":60.0},"
    "\"print_stats\":{\"filename\":\"piece.gcode\",\"state\":\"printing\","
    "\"print_duration\":600.0},"
    "\"virtual_sdcard\":{\"progress\":0.5,\"is_active\":true},"
    "\"webhooks\":{\"state\":\"ready\"}"
    "}}}";

static const char *REPONSE_REPOS =
    "{\"result\":{\"status\":{"
    "\"extruder\":{\"temperature\":24.5,\"target\":0.0},"
    "\"heater_bed\":{\"temperature\":23.1,\"target\":0.0},"
    "\"print_stats\":{\"filename\":\"\",\"state\":\"standby\",\"print_duration\":0.0},"
    "\"virtual_sdcard\":{\"progress\":0.0,\"is_active\":false},"
    "\"webhooks\":{\"state\":\"ready\"}"
    "}}}";

void suite_moonraker_parse(void)
{
    printf("suite : analyseur moonraker\n");
    etat_klipper_t e;

    /* --- cas nominal, impression en cours --- */
    memset(&e, 0xAA, sizeof(e));   /* rempli de bruit : l'analyseur doit tout écrire */
    VERIFIER(moonraker_parse_status(REPONSE_IMPRESSION, strlen(REPONSE_IMPRESSION), &e));
    VERIFIER_FLOAT(e.buse_actuelle, 210.4f, 0.01f);
    VERIFIER_FLOAT(e.buse_consigne, 210.0f, 0.01f);
    VERIFIER_FLOAT(e.plateau_actuel, 60.1f, 0.01f);
    VERIFIER_FLOAT(e.plateau_consigne, 60.0f, 0.01f);
    VERIFIER_TEXTE(e.fichier, "piece.gcode");
    VERIFIER_TEXTE(e.etat, "printing");
    VERIFIER_FLOAT(e.progression, 0.5f, 0.001f);
    VERIFIER(e.impression_en_cours);
    VERIFIER(!e.impression_en_pause);
    /* 600 s ecoulees a 50 % => 600 s restantes */
    VERIFIER(e.temps_restant_s == 600);

    /* --- machine au repos --- */
    memset(&e, 0xAA, sizeof(e));
    VERIFIER(moonraker_parse_status(REPONSE_REPOS, strlen(REPONSE_REPOS), &e));
    VERIFIER_TEXTE(e.etat, "standby");
    VERIFIER_TEXTE(e.fichier, "");
    VERIFIER(!e.impression_en_cours);
    VERIFIER(e.temps_restant_s == 0);
    VERIFIER_FLOAT(e.buse_consigne, 0.0f, 0.01f);

    /* --- progression trop faible : l'estimation doit etre neutralisee --- */
    static const char *DEBUT =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"printing\",\"print_duration\":5.0},"
        "\"virtual_sdcard\":{\"progress\":0.001,\"is_active\":true}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(DEBUT, strlen(DEBUT), &e));
    VERIFIER(e.temps_restant_s == 0);   /* plutot qu'une valeur absurde */

    /* --- champs absents : ne pas planter, laisser les valeurs a zero --- */
    static const char *PARTIEL = "{\"result\":{\"status\":{\"webhooks\":{\"state\":\"ready\"}}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(PARTIEL, strlen(PARTIEL), &e));
    VERIFIER_FLOAT(e.buse_actuelle, 0.0f, 0.01f);
    VERIFIER_TEXTE(e.fichier, "");

    /* --- pause --- */
    static const char *PAUSE =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"paused\",\"print_duration\":100.0},"
        "\"virtual_sdcard\":{\"progress\":0.25,\"is_active\":false}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(PAUSE, strlen(PAUSE), &e));
    VERIFIER(e.impression_en_pause);
    VERIFIER(!e.impression_en_cours);

    /* --- entrees invalides : rendre false sans toucher la sortie --- */
    etat_klipper_t temoin;
    memset(&temoin, 0x5A, sizeof(temoin));
    e = temoin;
    VERIFIER(!moonraker_parse_status("pas du json", 11, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    e = temoin;
    VERIFIER(!moonraker_parse_status("{\"error\":\"koko\"}", 16, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    e = temoin;
    VERIFIER(!moonraker_parse_status("", 0, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    e = temoin;
    VERIFIER(!moonraker_parse_status(NULL, 0, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    /* --- nom de fichier plus long que le tampon : tronquer sans deborder --- */
    static const char *LONG_NOM =
        "{\"result\":{\"status\":{\"print_stats\":{\"filename\":"
        "\"0123456789012345678901234567890123456789012345678901234567890123456789.gcode\","
        "\"state\":\"printing\",\"print_duration\":10.0}}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(LONG_NOM, strlen(LONG_NOM), &e));
    VERIFIER(strlen(e.fichier) == KLIPPER_FICHIER_MAX - 1);
}
```

- [ ] **Step 2: Lancer pour vérifier l'échec**

Run: `wsl -d Debian -- sh <chemin>/host-test/run.sh`
Expected: erreur de compilation — `moonraker_parse.h` n'existe pas.

- [ ] **Step 3: Écrire l'implémentation**

`firmware/main/apps/klipper/moonraker_parse.h` :

```c
/* Interprétation de la réponse de /printer/objects/query en état typé.
 *
 * Fonction pure, sans allocation persistante ni accès réseau : c'est ce qui
 * permet de la tester entièrement sur PC (voir host-test/). */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "etat_klipper.h"

/* Rend false si le JSON est absent, malformé, ou ne contient pas
 * result.status. Dans ce cas `sortie` n'est pas modifiée — l'appelant peut
 * ainsi conserver le dernier état connu. */
bool moonraker_parse_status(const char *json, size_t longueur, etat_klipper_t *sortie);
```

`firmware/main/apps/klipper/moonraker_parse.c` :

```c
#include "moonraker_parse.h"

#include <string.h>

#include "cJSON.h"

/* Lit un nombre optionnel ; laisse `defaut` si le champ manque ou n'en est pas un. */
static float nombre_ou(const cJSON *parent, const char *cle, float defaut)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(parent, cle);
    return cJSON_IsNumber(n) ? (float)n->valuedouble : defaut;
}

/* Copie une chaîne optionnelle en tronquant proprement. */
static void texte_ou_vide(const cJSON *parent, const char *cle, char *sortie, size_t taille)
{
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(parent, cle);
    if (cJSON_IsString(s) && s->valuestring != NULL) {
        snprintf(sortie, taille, "%s", s->valuestring);
    } else {
        sortie[0] = '\0';
    }
}

bool moonraker_parse_status(const char *json, size_t longueur, etat_klipper_t *sortie)
{
    if (json == NULL || longueur == 0 || sortie == NULL) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL) {
        return false;
    }

    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *statut = cJSON_GetObjectItemCaseSensitive(resultat, "status");
    if (!cJSON_IsObject(statut)) {
        cJSON_Delete(racine);
        return false;
    }

    /* À partir d'ici l'analyse réussit : on écrit dans une structure locale puis
     * on la copie d'un bloc, pour ne jamais laisser `sortie` à moitié remplie si
     * un champ manque. */
    etat_klipper_t e;
    memset(&e, 0, sizeof(e));

    const cJSON *extrudeur = cJSON_GetObjectItemCaseSensitive(statut, "extruder");
    e.buse_actuelle = nombre_ou(extrudeur, "temperature", 0.0f);
    e.buse_consigne = nombre_ou(extrudeur, "target", 0.0f);

    const cJSON *plateau = cJSON_GetObjectItemCaseSensitive(statut, "heater_bed");
    e.plateau_actuel = nombre_ou(plateau, "temperature", 0.0f);
    e.plateau_consigne = nombre_ou(plateau, "target", 0.0f);

    const cJSON *stats = cJSON_GetObjectItemCaseSensitive(statut, "print_stats");
    texte_ou_vide(stats, "filename", e.fichier, sizeof(e.fichier));
    texte_ou_vide(stats, "state", e.etat, sizeof(e.etat));
    float ecoule = nombre_ou(stats, "print_duration", 0.0f);

    const cJSON *sdcard = cJSON_GetObjectItemCaseSensitive(statut, "virtual_sdcard");
    e.progression = nombre_ou(sdcard, "progress", 0.0f);

    e.impression_en_cours = (strcmp(e.etat, "printing") == 0);
    e.impression_en_pause = (strcmp(e.etat, "paused") == 0);

    /* Moonraker ne fournit pas de temps restant : on l'estime à partir du temps
     * écoulé rapporté à la progression. Sous 1 %, le rapport explose et
     * produirait une valeur absurde — mieux vaut ne rien afficher. */
    if (e.progression > 0.01f && e.progression < 1.0f && ecoule > 0.0f) {
        float restant = ecoule * (1.0f - e.progression) / e.progression;
        e.temps_restant_s = (uint32_t)(restant + 0.5f);
    }

    *sortie = e;
    cJSON_Delete(racine);
    return true;
}
```

- [ ] **Step 4: Enregistrer et lancer**

Ajouter la suite dans `main.c` et la source dans le `CMakeLists.txt`, avec `${CORE_DIR}/../apps/klipper` dans les chemins d'inclusion.

Run: `wsl -d Debian -- sh <chemin>/host-test/run.sh`
Expected: toutes les vérifications passent, 0 échec.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/apps/klipper/ host-test/
git commit -m "feat(klipper): analyseur de la reponse Moonraker, teste sur hote"
```

---

### Task 4: Le magasin d'état à double tampon

**Files:**
- Create: `firmware/main/core/etat_store.c`, `etat_store.h`
- Test: `host-test/tests/test_etat_store.c`
- Modify: `host-test/CMakeLists.txt`, `host-test/tests/main.c`

**Interfaces:**
- Consumes: rien des tâches précédentes (générique sur `void *`).
- Produces: `etat_store_t`, `etat_store_init(etat_store_t *, size_t taille)`, `void *etat_store_tampon_arriere(etat_store_t *)`, `bool etat_store_valider(etat_store_t *)` (rend `true` si l'état a changé et a été permuté), `const void *etat_store_lire(etat_store_t *)`, `uint32_t etat_store_generation(etat_store_t *)`, `void etat_store_liberer(etat_store_t *)`.

Le magasin est générique : il ne connaît pas `etat_klipper_t`. C'est ce qui permettra au fork astro de le réutiliser tel quel avec sa propre structure.

- [ ] **Step 1: Écrire les tests**

`host-test/tests/test_etat_store.c` :

```c
#include <string.h>

#include "etat_store.h"
#include "petit_test.h"

typedef struct { int a; float b; char c[8]; } exemple_t;

void suite_etat_store(void)
{
    printf("suite : magasin d'etat\n");

    etat_store_t s;
    VERIFIER(etat_store_init(&s, sizeof(exemple_t)));

    /* Au depart, tout est a zero et la generation vaut 0. */
    const exemple_t *lu = etat_store_lire(&s);
    VERIFIER(lu->a == 0 && lu->b == 0.0f && lu->c[0] == '\0');
    VERIFIER(etat_store_generation(&s) == 0);

    /* Ecrire la meme chose ne doit PAS declencher de changement. */
    exemple_t *arriere = etat_store_tampon_arriere(&s);
    memset(arriere, 0, sizeof(exemple_t));
    VERIFIER(!etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 0);

    /* Un vrai changement permute et incremente la generation. */
    arriere = etat_store_tampon_arriere(&s);
    arriere->a = 42;
    VERIFIER(etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 1);
    lu = etat_store_lire(&s);
    VERIFIER(lu->a == 42);

    /* Le tampon arriere est remis a zero avant chaque remplissage : sans ca, le
     * remplissage laisse par l'alignement de la structure ferait echouer la
     * comparaison au hasard. */
    arriere = etat_store_tampon_arriere(&s);
    VERIFIER(arriere->a == 0);

    /* Rejouer la meme valeur qu'en facade ne change rien. */
    arriere->a = 42;
    VERIFIER(!etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 1);

    /* Un changement dans une chaine est detecte. */
    arriere = etat_store_tampon_arriere(&s);
    arriere->a = 42;
    snprintf(arriere->c, sizeof(arriere->c), "bonjour");
    VERIFIER(etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 2);
    lu = etat_store_lire(&s);
    VERIFIER_TEXTE(lu->c, "bonjour");

    etat_store_liberer(&s);

    /* Une taille nulle est refusee plutot que de produire un magasin inutile. */
    etat_store_t vide;
    VERIFIER(!etat_store_init(&vide, 0));
}
```

- [ ] **Step 2: Lancer pour vérifier l'échec**

Expected: erreur de compilation, `etat_store.h` absent.

- [ ] **Step 3: Écrire l'implémentation**

`firmware/main/core/etat_store.h` :

```c
/* Magasin d'état à double tampon.
 *
 * La tâche réseau remplit le tampon arrière ; l'interface lit le tampon avant.
 * Ils ne sont jamais les mêmes, donc un écran ne peut pas lire une structure à
 * moitié réécrite. La permutation n'a lieu que si le contenu a réellement
 * changé, ce qui évite de redessiner l'écran à chaque interrogation.
 *
 * Générique sur `void *` et une taille : le magasin ne connaît aucun modèle
 * d'état, ce qui le rend réutilisable tel quel par une autre application. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void    *avant;
    void    *arriere;
    size_t   taille;
    uint32_t generation;
} etat_store_t;

bool        etat_store_init(etat_store_t *store, size_t taille);
void        etat_store_liberer(etat_store_t *store);

/* Tampon à remplir, remis à zéro à chaque appel. */
void       *etat_store_tampon_arriere(etat_store_t *store);

/* Compare arrière et avant ; permute et incrémente la génération si différents.
 * Rend true en cas de changement. */
bool        etat_store_valider(etat_store_t *store);

const void *etat_store_lire(const etat_store_t *store);
uint32_t    etat_store_generation(const etat_store_t *store);
```

`firmware/main/core/etat_store.c` :

```c
#include "etat_store.h"

#include <stdlib.h>
#include <string.h>

bool etat_store_init(etat_store_t *store, size_t taille)
{
    if (store == NULL || taille == 0) {
        return false;
    }
    store->avant = calloc(1, taille);
    store->arriere = calloc(1, taille);
    if (store->avant == NULL || store->arriere == NULL) {
        free(store->avant);
        free(store->arriere);
        store->avant = store->arriere = NULL;
        return false;
    }
    store->taille = taille;
    store->generation = 0;
    return true;
}

void etat_store_liberer(etat_store_t *store)
{
    if (store == NULL) {
        return;
    }
    free(store->avant);
    free(store->arriere);
    store->avant = store->arriere = NULL;
    store->taille = 0;
}

void *etat_store_tampon_arriere(etat_store_t *store)
{
    /* La remise à zéro n'est pas une précaution de confort : sans elle, le
     * remplissage d'alignement de la structure garderait des valeurs aléatoires
     * et la comparaison de etat_store_valider() échouerait au hasard, faisant
     * clignoter l'interface sans raison. */
    memset(store->arriere, 0, store->taille);
    return store->arriere;
}

bool etat_store_valider(etat_store_t *store)
{
    if (memcmp(store->avant, store->arriere, store->taille) == 0) {
        return false;
    }
    void *echange = store->avant;
    store->avant = store->arriere;
    store->arriere = echange;
    store->generation++;
    return true;
}

const void *etat_store_lire(const etat_store_t *store)
{
    return store->avant;
}

uint32_t etat_store_generation(const etat_store_t *store)
{
    return store->generation;
}
```

- [ ] **Step 4: Enregistrer et lancer**

Expected: toutes les vérifications passent, 0 échec.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/core/etat_store.* host-test/
git commit -m "feat(core): magasin d'etat a double tampon"
```

---

### Task 5: La machine à états de connexion

**Files:**
- Create: `firmware/main/core/liaison.c`, `liaison.h`
- Test: `host-test/tests/test_liaison.c`
- Modify: `host-test/CMakeLists.txt`, `host-test/tests/main.c`

**Interfaces:**
- Consumes: rien.
- Produces: `liaison_etat_t` (`LIAISON_CONNEXION`, `LIAISON_EN_LIGNE`, `LIAISON_DEGRADEE`, `LIAISON_HORS_LIGNE`), `liaison_t`, `liaison_init(liaison_t *, uint8_t seuil_degrade, uint8_t seuil_hors_ligne)`, `liaison_succes(liaison_t *)`, `liaison_echec(liaison_t *)`, `liaison_etat_t liaison_etat(const liaison_t *)`, `const char *liaison_nom(liaison_etat_t)`, `uint32_t liaison_echecs_consecutifs(const liaison_t *)`.

C'est la logique qui alimentera la barre d'état du sous-jalon 2b. La tester ici, isolée, évite d'avoir à débrancher une imprimante pour vérifier une transition.

- [ ] **Step 1: Écrire les tests**

`host-test/tests/test_liaison.c` :

```c
#include "liaison.h"
#include "petit_test.h"

void suite_liaison(void)
{
    printf("suite : liaison\n");

    liaison_t l;
    liaison_init(&l, 2, 5);   /* degradee a 2 echecs, hors ligne a 5 */

    /* Au demarrage, on est en cours de connexion : ni en ligne ni hors ligne. */
    VERIFIER(liaison_etat(&l) == LIAISON_CONNEXION);

    /* Un premier succes fait passer en ligne. */
    liaison_succes(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);
    VERIFIER(liaison_echecs_consecutifs(&l) == 0);

    /* Un echec isole ne doit pas alarmer : le reseau local perd des paquets. */
    liaison_echec(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);

    /* Au seuil, on passe en degradee. */
    liaison_echec(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_DEGRADEE);
    VERIFIER(liaison_echecs_consecutifs(&l) == 2);

    /* Un succes efface tout, immediatement. */
    liaison_succes(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);
    VERIFIER(liaison_echecs_consecutifs(&l) == 0);

    /* Au second seuil, hors ligne. */
    for (int i = 0; i < 5; i++) {
        liaison_echec(&l);
    }
    VERIFIER(liaison_etat(&l) == LIAISON_HORS_LIGNE);

    /* Et l'on en sort des le premier succes, sans etape intermediaire :
     * l'utilisateur qui rebranche veut voir l'etat revenir tout de suite. */
    liaison_succes(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);

    /* Les noms servent a la barre d'etat et au journal : ils doivent exister
     * pour toutes les valeurs. */
    VERIFIER_TEXTE(liaison_nom(LIAISON_CONNEXION), "connexion");
    VERIFIER_TEXTE(liaison_nom(LIAISON_EN_LIGNE), "en ligne");
    VERIFIER_TEXTE(liaison_nom(LIAISON_DEGRADEE), "degradee");
    VERIFIER_TEXTE(liaison_nom(LIAISON_HORS_LIGNE), "hors ligne");

    /* Depuis l'etat initial, des echecs menent hors ligne sans jamais etre
     * passe par en ligne : une machine eteinte au demarrage doit le dire. */
    liaison_t neuve;
    liaison_init(&neuve, 2, 5);
    for (int i = 0; i < 5; i++) {
        liaison_echec(&neuve);
    }
    VERIFIER(liaison_etat(&neuve) == LIAISON_HORS_LIGNE);
}
```

- [ ] **Step 2: Lancer pour vérifier l'échec**

Expected: erreur de compilation, `liaison.h` absent.

- [ ] **Step 3: Écrire l'implémentation**

`firmware/main/core/liaison.h` :

```c
/* Santé de la liaison avec l'hôte, exprimée en quatre états.
 *
 * L'habillage est seul à afficher cet état : un écran ne montre jamais de boîte
 * d'erreur réseau, il grise ses données périmées. Cette règle évite que chaque
 * panneau invente sa propre façon de dire « je n'ai pas de nouvelles ». */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LIAISON_CONNEXION = 0,  /* jamais joint l'hôte depuis le démarrage */
    LIAISON_EN_LIGNE,
    LIAISON_DEGRADEE,       /* des échecs, pas encore de quoi renoncer */
    LIAISON_HORS_LIGNE,
} liaison_etat_t;

typedef struct {
    liaison_etat_t etat;
    uint32_t       echecs_consecutifs;
    uint8_t        seuil_degrade;
    uint8_t        seuil_hors_ligne;
} liaison_t;

void           liaison_init(liaison_t *l, uint8_t seuil_degrade, uint8_t seuil_hors_ligne);
void           liaison_succes(liaison_t *l);
void           liaison_echec(liaison_t *l);
liaison_etat_t liaison_etat(const liaison_t *l);
uint32_t       liaison_echecs_consecutifs(const liaison_t *l);
const char    *liaison_nom(liaison_etat_t etat);
```

`firmware/main/core/liaison.c` :

```c
#include "liaison.h"

void liaison_init(liaison_t *l, uint8_t seuil_degrade, uint8_t seuil_hors_ligne)
{
    l->etat = LIAISON_CONNEXION;
    l->echecs_consecutifs = 0;
    l->seuil_degrade = seuil_degrade;
    l->seuil_hors_ligne = seuil_hors_ligne;
}

void liaison_succes(liaison_t *l)
{
    /* Un succès efface l'historique et ramène en ligne sans étape
     * intermédiaire : quelqu'un qui vient de rebrancher sa machine veut voir
     * l'état revenir tout de suite. */
    l->echecs_consecutifs = 0;
    l->etat = LIAISON_EN_LIGNE;
}

void liaison_echec(liaison_t *l)
{
    if (l->echecs_consecutifs < UINT32_MAX) {
        l->echecs_consecutifs++;
    }
    if (l->echecs_consecutifs >= l->seuil_hors_ligne) {
        l->etat = LIAISON_HORS_LIGNE;
    } else if (l->echecs_consecutifs >= l->seuil_degrade) {
        l->etat = LIAISON_DEGRADEE;
    }
    /* Sous le premier seuil, l'état ne bouge pas : un échec isolé sur un réseau
     * local est banal et ne doit rien signaler. */
}

liaison_etat_t liaison_etat(const liaison_t *l) { return l->etat; }
uint32_t liaison_echecs_consecutifs(const liaison_t *l) { return l->echecs_consecutifs; }

const char *liaison_nom(liaison_etat_t etat)
{
    switch (etat) {
        case LIAISON_CONNEXION:  return "connexion";
        case LIAISON_EN_LIGNE:   return "en ligne";
        case LIAISON_DEGRADEE:   return "degradee";
        case LIAISON_HORS_LIGNE: return "hors ligne";
    }
    return "inconnu";
}
```

- [ ] **Step 4: Enregistrer et lancer**

Expected: toutes les vérifications passent, 0 échec.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/core/liaison.* host-test/
git commit -m "feat(core): machine a etats de la liaison avec l'hote"
```

---

### Task 6: Le backend factice

**Files:**
- Create: `firmware/main/core/backend_factice.c`, `backend_factice.h`
- Test: `host-test/tests/test_backend_factice.c`
- Modify: `host-test/CMakeLists.txt`, `host-test/tests/main.c`

**Interfaces:**
- Consumes: `backend_desc_t` et `etat_klipper_t` des tâches 2 et 3.
- Produces: `const backend_desc_t *backend_factice_desc(void)` et `void backend_factice_scenario(int numero)` (0 = repos, 1 = impression qui progresse, 2 = pause, 3 = valeurs extrêmes).

Ce backend n'est pas une commodité de test : il est **le second consommateur permanent de l'abstraction**. Le fork astro vivant hors du dépôt, sans lui le socle n'aurait qu'un seul client, et une abstraction cassée n'apparaîtrait qu'à la prochaine fusion du fork. Il fera aussi tourner le simulateur du sous-jalon 2b sans aucune machine.

- [ ] **Step 1: Écrire les tests**

`host-test/tests/test_backend_factice.c` :

```c
#include <string.h>

#include "backend_factice.h"
#include "petit_test.h"

void suite_backend_factice(void)
{
    printf("suite : backend factice\n");

    const backend_desc_t *d = backend_factice_desc();
    VERIFIER_TEXTE(d->nom, "factice");
    VERIFIER(d->taille_etat == sizeof(etat_klipper_t));
    VERIFIER(d->demarrer != NULL && d->rafraichir != NULL);
    VERIFIER(d->arreter != NULL && d->commande != NULL);

    /* Le socle alloue : on imite ce qu'il fera. */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    backend_hote_t hote = { .adresse = "factice", .port = 0 };
    VERIFIER(d->demarrer(&etat, &hote) == ESP_OK);

    /* Scenario repos. */
    backend_factice_scenario(0);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER_TEXTE(etat.etat, "standby");
    VERIFIER(!etat.impression_en_cours);

    /* Scenario impression : la progression doit avancer d'un appel a l'autre,
     * sinon le magasin d'etat ne detecterait aucun changement et l'interface
     * paraitrait figee. */
    backend_factice_scenario(1);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    float p1 = etat.progression;
    VERIFIER(etat.impression_en_cours);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.progression > p1);

    /* Scenario pause. */
    backend_factice_scenario(2);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.impression_en_pause);

    /* Scenario extreme : sert a verifier que l'interface ne deborde pas. */
    backend_factice_scenario(3);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.buse_actuelle > 300.0f);
    VERIFIER(strlen(etat.fichier) == KLIPPER_FICHIER_MAX - 1);

    /* Les actions connues sont acceptees, les inconnues refusees explicitement. */
    VERIFIER(d->commande(&etat, BACKEND_ACTION_PAUSE, NULL) == ESP_OK);
    VERIFIER(d->commande(&etat, "action_inexistante", NULL) == ESP_ERR_NOT_SUPPORTED);

    d->arreter(&etat);
}
```

- [ ] **Step 2: Lancer pour vérifier l'échec**

Expected: erreur de compilation.

- [ ] **Step 3: Écrire l'implémentation**

`firmware/main/core/backend_factice.h` :

```c
/* Backend synthétique : produit un état plausible sans aucune machine.
 *
 * Il sert à trois choses. Faire tourner le simulateur sur PC. Exercer les cas
 * pénibles qu'une vraie imprimante ne produit pas à la demande — température
 * nulle, impression à 99 %, valeurs aberrantes. Et surtout garantir que le
 * socle a en permanence deux consommateurs réels de son abstraction, puisque
 * l'application astro vit dans un fork. */
#pragma once

#include "backend.h"
#include "etat_klipper.h"

const backend_desc_t *backend_factice_desc(void);

/* 0 repos · 1 impression qui progresse · 2 pause · 3 valeurs extrêmes */
void backend_factice_scenario(int numero);
```

`firmware/main/core/backend_factice.c` : implémenter les quatre fonctions du descripteur au-dessus d'un compteur statique de scénario. `rafraichir` remplit `etat_klipper_t` selon le scénario courant ; en scénario 1, il incrémente la progression de 0,01 à chaque appel et la reboucle à 0 au-delà de 1,0, en recalculant `temps_restant_s` de façon cohérente. Le scénario 3 remplit `fichier` avec `KLIPPER_FICHIER_MAX - 1` caractères et met `buse_actuelle` à 350 °C. `commande` accepte les quatre `BACKEND_ACTION_*` en journalisant, et rend `ESP_ERR_NOT_SUPPORTED` pour tout le reste. `demarrer` et `arreter` ne font rien d'autre que journaliser.

> Ne pas utiliser `rand()` : deux exécutions successives doivent produire la même séquence, sinon un test devient instable et un écran clignote sans raison.

- [ ] **Step 4: Enregistrer et lancer**

Expected: toutes les vérifications passent, 0 échec.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/core/backend_factice.* host-test/
git commit -m "feat(core): backend factice, second consommateur de l'abstraction"
```

---

### Task 7: Les réglages persistants

**Files:**
- Create: `firmware/main/core/reglages.c`, `reglages.h`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `backend_hote_t` de la tâche 2.
- Produces: `esp_err_t reglages_charger(void)`, `bool reglages_hote(backend_hote_t *sortie)`, `esp_err_t reglages_definir_hote(const backend_hote_t *)`, `const char *reglages_backend(void)`, `esp_err_t reglages_definir_backend(const char *nom)`, `bool reglages_configures(void)`.

Cette tâche ne se teste pas sur PC : elle dépend de la NVS. Elle se vérifie sur l'appareil, à la tâche 9.

> **L'invariant le plus important du projet passe par ce fichier.** La partition `nvs` est **partagée avec le firmware d'origine**, dont elle contient les identifiants WiFi. Au jalon 1, un `nvs_flash_erase()` recopié d'un exemple ESP-IDF a failli rendre l'appareil définitivement injoignable. Ici : espace de noms `ktouch`, et **aucun appel destructif, jamais**.

- [ ] **Step 1: Écrire l'implémentation**

`firmware/main/core/reglages.h` :

```c
/* Réglages persistants du firmware.
 *
 * Rangés dans l'espace de noms NVS « ktouch ». La partition nvs est PARTAGÉE
 * avec le firmware d'origine, qui y garde ses identifiants WiFi : on n'écrit
 * que dans notre espace de noms, et on n'efface jamais la partition. */
#pragma once

#include <stdbool.h>

#include "backend.h"
#include "esp_err.h"

#define REGLAGES_ESPACE_NOMS "ktouch"

esp_err_t   reglages_charger(void);
bool        reglages_configures(void);   /* faux au tout premier démarrage */

bool        reglages_hote(backend_hote_t *sortie);
esp_err_t   reglages_definir_hote(const backend_hote_t *hote);

const char *reglages_backend(void);      /* "moonraker" par défaut */
esp_err_t   reglages_definir_backend(const char *nom);
```

`firmware/main/core/reglages.c` : ouvrir l'espace de noms en lecture/écriture avec `nvs_open(REGLAGES_ESPACE_NOMS, NVS_READWRITE, &handle)`, lire les clés `hote_adresse` (chaîne), `hote_port` (u16) et `backend` (chaîne) dans des variables statiques, et rendre les valeurs par défaut si absentes — port `7125`, backend `"moonraker"`, adresse vide. `reglages_configures()` rend vrai seulement si l'adresse est non vide. Les fonctions de définition écrivent puis appellent `nvs_commit`.

> Un échec d'ouverture de la NVS ne doit **pas** être fatal : journaliser et fonctionner avec les valeurs par défaut. L'appareil reste alors utilisable et diagnosticable à distance, ce qui vaut mieux qu'un redémarrage en boucle.

- [ ] **Step 2: Vérifier l'absence d'appel destructif**

```powershell
Select-String -Path firmware/main -Pattern "nvs_flash_erase|nvs_erase_all" -Recurse
```

Expected: aucune occurrence hors des commentaires qui expliquent pourquoi c'est interdit.

- [ ] **Step 3: Compiler**

```powershell
& "<chemin-vers-esp-idf>\export.ps1"; cd firmware; idf.py build
```

Expected: compilation réussie.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/core/reglages.* firmware/main/CMakeLists.txt
git commit -m "feat(core): reglages persistants dans un espace de noms NVS dedie"
```

---

### Task 8: Le backend Moonraker et la boucle d'interrogation

**Files:**
- Create: `firmware/main/apps/klipper/backend_moonraker.c`, `backend_moonraker.h`
- Create: `firmware/main/core/boucle.c`, `boucle.h`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `backend_desc_t`, `etat_klipper_t`, `moonraker_parse_status`, `etat_store_t`, `liaison_t`, `reglages_*`.
- Produces: `const backend_desc_t *backend_moonraker_desc(void)` ; `esp_err_t boucle_demarrer(const backend_desc_t *desc, const backend_hote_t *hote)`, `const void *boucle_etat(void)`, `uint32_t boucle_generation(void)`, `liaison_etat_t boucle_liaison(void)`, `esp_err_t boucle_commander(const char *action, const char *arguments_json)`.

- [ ] **Step 1: Écrire le backend Moonraker**

`backend_moonraker.c` s'appuie sur `esp_http_client`. `rafraichir` émet un `GET` sur
`http://<hote>:<port>/printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard&webhooks`,
lit la réponse dans un tampon statique de 4 Kio, et la passe à `moonraker_parse_status`. `commande` traduit les actions en `POST` :

| Action | Requête |
|---|---|
| `BACKEND_ACTION_PAUSE` | `POST /printer/print/pause` |
| `BACKEND_ACTION_REPRENDRE` | `POST /printer/print/resume` |
| `BACKEND_ACTION_ANNULER` | `POST /printer/print/cancel` |
| `BACKEND_ACTION_URGENCE` | `POST /printer/emergency_stop` |

Toute autre action rend `ESP_ERR_NOT_SUPPORTED`.

> Le tampon de réponse est **statique et de taille fixe**, jamais alloué à chaque appel : une allocation dans le chemin de rafraîchissement, exécutée toutes les secondes pendant des heures, est la façon la plus sûre de provoquer un redémarrage nocturne qu'on ne saura pas déboguer. Si la réponse dépasse le tampon, la tronquer et rendre une erreur plutôt que de grandir.

- [ ] **Step 2: Écrire la boucle**

`boucle.c` crée une tâche FreeRTOS dédiée qui, chaque seconde : prend le tampon arrière du magasin, appelle `rafraichir`, appelle `liaison_succes` ou `liaison_echec` selon le résultat, puis `etat_store_valider`. Les commandes passent par une file FreeRTOS de profondeur 4, dépilée par la même tâche.

> **Une commande n'est jamais exécutée depuis l'appelant.** `boucle_commander` ne fait qu'empiler et rend immédiatement ; c'est ce qui garantira au sous-jalon 2b qu'un rappel de bouton ne gèle pas l'interface pendant les secondes que peut prendre un `POST`. La règle est posée ici, avant que le premier bouton n'existe.

- [ ] **Step 3: Compiler**

Expected: compilation réussie, binaire toujours très en deçà des 4 718 592 octets du slot.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/apps/klipper/backend_moonraker.* firmware/main/core/boucle.* firmware/main/CMakeLists.txt
git commit -m "feat: backend Moonraker et boucle d'interrogation"
```

---

### Task 9: Intégration et route `/state`

**Files:**
- Modify: `firmware/main/app_main.c`, `firmware/main/web.c`, `firmware/main/web.h`
- Modify: `docs/hardware/flashing.md`

**Interfaces:**
- Consumes: tout ce qui précède.
- Produces: un firmware qui interroge une vraie machine et expose `GET /state`.

- [ ] **Step 1: Câbler dans `app_main`**

Après le démarrage du serveur web et **avant** l'écran — l'ordre du jalon 1 reste inchangé — charger les réglages, choisir le descripteur de backend par son nom (`"moonraker"` ou `"factice"`), et démarrer la boucle si l'hôte est configuré. Si `reglages_configures()` est faux, journaliser l'absence de configuration et ne pas démarrer la boucle : ce sera le rôle de l'écran de première configuration du sous-jalon 2b.

- [ ] **Step 2: Ajouter la route `/state`**

`GET /state` rend l'état courant en JSON, plus l'état de la liaison et la génération :

```json
{"liaison":"en ligne","generation":42,"etat":{
  "etat":"printing","buse":{"actuelle":210.4,"consigne":210.0},
  "plateau":{"actuel":60.1,"consigne":60.0},
  "fichier":"piece.gcode","progression":0.5,"temps_restant_s":600,
  "impression_en_cours":true,"impression_en_pause":false}}
```

> C'est la route qui remplace, pour ce jalon, l'interface qu'on n'a pas encore. Elle permet de vérifier à distance que l'analyseur interprète correctement une vraie machine — le seul point que les tests hôte ne peuvent pas couvrir, puisqu'ils travaillent sur du JSON écrit à la main.

- [ ] **Step 3: Compiler et installer**

Compiler, puis téléverser par le `/update` du firmware d'origine comme le décrit `docs/hardware/flashing.md`.

- [ ] **Step 4: Vérifier avec le backend factice**

Sans machine Klipper, régler le backend sur `"factice"` et lire :

```powershell
curl.exe -s http://<ip-de-la-k-touch>/state
```

Expected: un JSON cohérent, dont la génération augmente au fil des relevés et dont la progression avance.

- [ ] **Step 5: Vérifier contre une vraie machine Klipper**

Régler l'hôte sur une machine Moonraker réelle, puis comparer :

```powershell
curl.exe -s "http://<hote-klipper>:7125/printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard&webhooks"
curl.exe -s http://<ip-de-la-k-touch>/state
```

Expected: les températures et l'état concordent. C'est le critère de réussite du jalon.

> Si aucune machine Klipper n'est disponible, cette étape est reportée et le jalon reste validé par le backend factice — mais le noter explicitement dans le rapport plutôt que de laisser croire à une vérification qui n'a pas eu lieu.

- [ ] **Step 6: Vérifier la dégradation**

Éteindre l'hôte ou couper son réseau, puis relire `/state`. Expected : `liaison` passe par `degradee` puis `hors ligne`, la génération cesse d'augmenter, et le dernier état connu reste affiché — l'appareil ne redémarre pas et ne se bloque pas.

- [ ] **Step 7: Documenter et commiter**

Ajouter la route `/state` au tableau des routes de `docs/hardware/flashing.md`.

```bash
git add firmware/main/ docs/hardware/flashing.md
git commit -m "feat: integration de la boucle et route /state"
```

---

## Notes de revue

Relecture du plan face à la spec, effectuée après rédaction.

**Couverture.** Le contrat de backend et le modèle d'état viennent de la section 4 de la spec (tâche 2) ; le double tampon avec remise à zéro, de la section 5.1 (tâche 4) ; les quatre états de liaison, de la section 5.3 (tâche 5) ; le backend factice et son rôle de second consommateur, de la section 8 (tâche 6) ; l'espace de noms NVS et l'interdiction d'effacement, de la section 9 (tâche 7) ; la route `/state`, de la section 8 (tâche 9). Les tests hôte de la section 8 sont la tâche 1.

**Hors périmètre de ce sous-jalon, et traité au 2b** : le simulateur SDL, la navigation, la barre d'état, le clavier tactile, le dialogue de confirmation, les deux écrans, et le moteur de mise à jour de la section 7 de la spec.

**Cohérence des noms.** `etat_klipper_t` est défini en tâche 2 et consommé aux tâches 3, 6 et 8. `backend_desc_t` en tâche 2, implémenté aux tâches 6 et 8. `etat_store_*` en tâche 4, consommé en tâche 8. `liaison_*` en tâche 5, consommé aux tâches 8 et 9. `moonraker_parse_status` en tâche 3, consommé en tâche 8.

**Dépendance assumée à une action humaine.** La tâche 1 ne peut pas aboutir sans que l'utilisateur installe les paquets WSL, `sudo` réclamant un mot de passe. C'est signalé dans la tâche plutôt que découvert à l'exécution.

**Point le plus fragile.** L'analyseur de la tâche 3 est écrit contre la forme documentée des réponses de Moonraker, mais celles d'une vraie installation peuvent différer — champs supplémentaires, `filename` absent plutôt que vide, valeurs nulles. La tâche 9, étape 5, est la seule à pouvoir le révéler, et c'est pour cette raison qu'elle existe : les tests hôte valident la logique, pas la fidélité au monde réel.
