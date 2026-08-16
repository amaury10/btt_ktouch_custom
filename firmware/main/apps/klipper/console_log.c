/* Implémentation du store de scrollback de la console gcode -- voir
 * console_log.h pour le contrat et le POURQUOI (RAM interne, même leçon que
 * klipper_fichiers.c/power_devices.c : voir leurs commentaires de tête et la
 * mémoire du projet).
 *
 * UNE seule instance du store (g_store, ~2,3 Ko -- CONSOLE_LIGNES_MAX(24) *
 * CONSOLE_LIGNE_MAX(96)), EN PSRAM côté ESP (fix RAM interne, lot Power/
 * Console/Miniatures/USB : ce store vivait auparavant en .bss RAM interne, ce
 * qui contribuait à empêcher la tâche WebSocket -- 16 Ko de pile, RAM interne
 * elle aussi -- de s'allouer. Voir la mémoire du projet). Alloué UNE SEULE
 * FOIS via heap_caps_malloc(MALLOC_CAP_SPIRAM), avec garde NULL : si la PSRAM
 * est indisponible/épuisée, le scrollback se DÉSACTIVE proprement (les trois
 * fonctions publiques deviennent des no-op sûrs -- voir store_obtenir()) --
 * PAS de repli sur une seconde instance statique EN RAM INTERNE, ce qui
 * annulerait exactement le gain de ce fix (une structure pleine grandeur
 * réservée en permanence, utilisée ou non). Dégradé mais jamais un firmware
 * qui plante pour un scrollback (même politique défensive que le reste de ce
 * dépôt, aucun ESP_ERROR_CHECK sur une allocation non fatale). Côté
 * host-test/simulateur (pas d'ESP_PLATFORM), pas de PSRAM sur PC : une simple
 * instance statique suffit, comme avant ce fix.
 *
 * Verrou : même politique que klipper_fichiers.c/power_devices.c (elles-mêmes
 * alignées sur rescue_disarm()/wifi_reconfigurer()) -- un portMUX_TYPE,
 * section critique COURTE réduite à la seule copie mémoire (jamais d'appel
 * bloquant ni de travail long sous le verrou, snprintf() de bornage compris
 * -- copié dans une ligne de tampon LOCAL avant d'entrer sous verrou, jamais
 * calculé dedans -- ni l'allocation PSRAM elle-même, résolue AVANT d'entrer
 * sous verrou, voir store_obtenir()). Le store est écrit par la tâche
 * WebSocket ET par la tâche LVGL (écho local, voir console_log.h) et lu par
 * la tâche LVGL sur rafraîchissement d'écran ; sur cet ESP32-S3 SMP elles
 * peuvent réellement s'exécuter en parallèle, la copie doit donc être
 * atomique vis-à-vis d'elles. Hors cible (host-test / simulateur, pas
 * d'ESP_PLATFORM), il n'y a pas de FreeRTOS et le code est mono-thread : le
 * verrou retombe sur un no-op, même politique que journal.h. */
#include "console_log.h"

#include <stdio.h>
#include <string.h>

#include "journal.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)

static const char *TAG = "console_log";

static console_log_t *g_store;  /* PSRAM ; NULL tant que non allouee OU apres un echec definitif */
static bool           g_echec;  /* vrai apres un echec d'allocation -- pas de nouvel essai */

/* Resout l'instance a utiliser -- alloue la PSRAM au tout premier appel
 * (paresseux : aucune dependance d'ordre d'initialisation avec app_main.c),
 * jamais sous VERROU_PRENDRE() (heap_caps_malloc() n'a rien d'un travail
 * court adapte a une section critique portMUX). Rend NULL si la PSRAM est
 * indisponible/épuisée -- AUCUN repli sur une seconde instance statique EN
 * RAM INTERNE (voir le commentaire de tete : ce serait reserver en
 * permanence l'espace que ce fix cherche justement a liberer). Idempotente
 * cote echec : bascule DEFINITIVEMENT sur `g_echec`, jamais de nouvelle
 * tentative a chaque appel -- eviter de spammer /log a chaque ligne de
 * console si la PSRAM est indisponible. Les trois fonctions publiques
 * ci-dessous traitent un NULL comme "scrollback indisponible", jamais un
 * dereferencement. */
static console_log_t *store_obtenir(void)
{
    if (g_store != NULL) {
        return g_store;
    }
    if (g_echec) {
        return NULL;
    }
    console_log_t *tampon = (console_log_t *)heap_caps_malloc(sizeof(console_log_t), MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour le scrollback console (%u octets) ; "
                       "scrollback indisponible", (unsigned)sizeof(console_log_t));
        g_echec = true;
        return NULL;
    }
    memset(tampon, 0, sizeof(*tampon));
    g_store = tampon;
    return g_store;
}

/* Rend le store SANS jamais l'allouer -- NULL tant que rien n'a ete journalise.
 * Existe pour console_log_generation() seule : cette fonction est appelee a
 * chaque pompe d'habillage (~200 ms, voir generation_externe_klipper() dans
 * app_main.c), donc passer par store_obtenir() ferait allouer le scrollback
 * AU DEMARRAGE meme si l'utilisateur n'ouvre jamais la Console -- precisement
 * la reservation permanente que l'allocation paresseuse ci-dessus existe pour
 * eviter. */
static console_log_t *store_existant(void)
{
    return g_store;
}
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
static console_log_t g_store_hote;
static console_log_t *store_obtenir(void)
{
    return &g_store_hote;
}
static console_log_t *store_existant(void)
{
    return &g_store_hote; /* instance statique : toujours la, rien a allouer */
}
#endif

void console_log_ajouter(const char *ligne)
{
    if (ligne == NULL) {
        return;
    }

    console_log_t *store = store_obtenir();
    if (store == NULL) {
        return; /* PSRAM indisponible (deja journalise par store_obtenir()) : no-op */
    }

    /* snprintf() de bornage fait AVANT le verrou (jamais de travail non
     * trivial en section critique, voir commentaire de tête) : la ligne
     * tronquée est prête, il ne reste plus qu'à la recopier et avancer les
     * indices du ring sous verrou. */
    char tampon[CONSOLE_LIGNE_MAX];
    snprintf(tampon, sizeof(tampon), "%s", ligne);

    VERROU_PRENDRE();
    uint8_t index;
    if (store->nb < CONSOLE_LIGNES_MAX) {
        /* Encore de la place : la nouvelle ligne prend le prochain
         * emplacement libre après la dernière valide. */
        index = (uint8_t)((store->debut + store->nb) % CONSOLE_LIGNES_MAX);
        store->nb++;
    } else {
        /* Plein : éviction FIFO -- la plus ancienne ligne (à `debut`) est
         * écrasée par la nouvelle, et `debut` avance d'un cran pour que la
         * SUIVANTE plus ancienne devienne la nouvelle plus ancienne. */
        index = store->debut;
        store->debut = (uint8_t)((store->debut + 1) % CONSOLE_LIGNES_MAX);
    }
    memcpy(store->lignes[index], tampon, sizeof(tampon));
    store->generation++;
    VERROU_RENDRE();
}

void console_log_lire(console_log_t *dest)
{
    if (dest == NULL) {
        return;
    }
    console_log_t *store = store_obtenir();
    if (store == NULL) {
        /* PSRAM indisponible : un scrollback vide (jamais ecrit) est la
           lecture la plus honnete -- jamais de contenu indetermine renvoye. */
        memset(dest, 0, sizeof(*dest));
        return;
    }
    VERROU_PRENDRE();
    *dest = *store;
    VERROU_RENDRE();
}

void console_log_effacer(void)
{
    console_log_t *store = store_obtenir();
    if (store == NULL) {
        return;
    }
    VERROU_PRENDRE();
    store->debut = 0;
    store->nb = 0;
    store->generation++;
    VERROU_RENDRE();
}

uint32_t console_log_generation(void)
{
    /* store_existant(), PAS store_obtenir() : n'alloue jamais -- voir son
     * commentaire. Store absent = rien n'a jamais ete journalise = 0, une
     * valeur STABLE. Rendre autre chose ferait bouger la somme de
     * generation_externe_klipper() sans qu'aucun store n'ait change. */
    console_log_t *store = store_existant();
    if (store == NULL) {
        return 0;
    }
    uint32_t generation;
    VERROU_PRENDRE();
    generation = store->generation;
    VERROU_RENDRE();
    return generation;
}
