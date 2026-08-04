/* Implémentation du store de scrollback de la console gcode -- voir
 * console_log.h pour le contrat et le POURQUOI (RAM interne, même leçon que
 * klipper_fichiers.c/power_devices.c : voir leurs commentaires de tête et la
 * mémoire du projet).
 *
 * UNE seule instance statique du store (g_store, ~2,3 Ko en RAM interne
 * .bss -- CONSOLE_LIGNES_MAX(24) * CONSOLE_LIGNE_MAX(96)).
 *
 * Verrou : même politique que klipper_fichiers.c/power_devices.c (elles-mêmes
 * alignées sur rescue_disarm()/wifi_reconfigurer()) -- un portMUX_TYPE,
 * section critique COURTE réduite à la seule copie mémoire (jamais d'appel
 * bloquant ni de travail long sous le verrou, snprintf() de bornage compris
 * -- copié dans une ligne de tampon LOCAL avant d'entrer sous verrou, jamais
 * calculé dedans). Le store est écrit par la tâche WebSocket ET par la tâche
 * LVGL (écho local, voir console_log.h) et lu par la tâche LVGL sur
 * rafraîchissement d'écran ; sur cet ESP32-S3 SMP elles peuvent réellement
 * s'exécuter en parallèle, la copie doit donc être atomique vis-à-vis
 * d'elles. Hors cible (host-test / simulateur, pas d'ESP_PLATFORM), il n'y a
 * pas de FreeRTOS et le code est mono-thread : le verrou retombe sur un
 * no-op, même politique que journal.h. */
#include "console_log.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
#endif

static console_log_t g_store;

void console_log_ajouter(const char *ligne)
{
    if (ligne == NULL) {
        return;
    }

    /* snprintf() de bornage fait AVANT le verrou (jamais de travail non
     * trivial en section critique, voir commentaire de tête) : la ligne
     * tronquée est prête, il ne reste plus qu'à la recopier et avancer les
     * indices du ring sous verrou. */
    char tampon[CONSOLE_LIGNE_MAX];
    snprintf(tampon, sizeof(tampon), "%s", ligne);

    VERROU_PRENDRE();
    uint8_t index;
    if (g_store.nb < CONSOLE_LIGNES_MAX) {
        /* Encore de la place : la nouvelle ligne prend le prochain
         * emplacement libre après la dernière valide. */
        index = (uint8_t)((g_store.debut + g_store.nb) % CONSOLE_LIGNES_MAX);
        g_store.nb++;
    } else {
        /* Plein : éviction FIFO -- la plus ancienne ligne (à `debut`) est
         * écrasée par la nouvelle, et `debut` avance d'un cran pour que la
         * SUIVANTE plus ancienne devienne la nouvelle plus ancienne. */
        index = g_store.debut;
        g_store.debut = (uint8_t)((g_store.debut + 1) % CONSOLE_LIGNES_MAX);
    }
    memcpy(g_store.lignes[index], tampon, sizeof(tampon));
    g_store.generation++;
    VERROU_RENDRE();
}

void console_log_lire(console_log_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *dest = g_store;
    VERROU_RENDRE();
}

void console_log_effacer(void)
{
    VERROU_PRENDRE();
    g_store.debut = 0;
    g_store.nb = 0;
    g_store.generation++;
    VERROU_RENDRE();
}
