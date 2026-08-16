/* Implémentation du store de fichiers gcode -- voir klipper_fichiers.h pour le
 * contrat et le POURQUOI (RAM interne : l'état est copié partout, une liste de
 * 2 Ko dupliquée dans chaque copie épuisait la RAM et empêchait la création de
 * la tâche WebSocket -- voir la mémoire du projet).
 *
 * UNE seule instance statique du store (g_store, ~2 Ko en RAM interne .bss),
 * contre les 3 copies ou plus qu'on retire de etat_klipper_t : gain net.
 *
 * Verrou : même politique que rescue_disarm() (rescue.c) et wifi_reconfigurer()
 * (wifi.c) -- un portMUX_TYPE, section critique COURTE réduite à la seule copie
 * mémoire (jamais d'appel bloquant ni de travail long sous le verrou). Le store
 * est écrit par la tâche WS (server.files.list) et lu par l'UI/httpd sur
 * d'autres tâches ; sur cet ESP32-S3 SMP elles peuvent réellement s'exécuter en
 * parallèle, la copie doit donc être atomique vis-à-vis d'elles. Hors cible
 * (host-test / simulateur, pas d'ESP_PLATFORM), il n'y a pas de FreeRTOS et le
 * code est mono-thread : le verrou retombe sur un no-op, même politique que
 * journal.h. */
#include "klipper_fichiers.h"

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

static klipper_fichiers_t g_store;

void klipper_fichiers_definir(const klipper_fichiers_t *src)
{
    if (src == NULL) {
        return;
    }
    VERROU_PRENDRE();
    g_store = *src;
    VERROU_RENDRE();
}

void klipper_fichiers_lire(klipper_fichiers_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *dest = g_store;
    VERROU_RENDRE();
}
