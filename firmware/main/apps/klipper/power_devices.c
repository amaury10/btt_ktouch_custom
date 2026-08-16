/* Implémentation du store de prises Moonraker -- voir power_devices.h pour le
 * contrat et le POURQUOI (RAM interne, même leçon que klipper_fichiers.c :
 * voir son commentaire de tête et la mémoire du projet).
 *
 * UNE seule instance statique du store (g_store, quelques centaines d'octets
 * en RAM interne .bss).
 *
 * Verrou : même politique que klipper_fichiers.c (elle-même alignée sur
 * rescue_disarm()/wifi_reconfigurer()) -- un portMUX_TYPE, section critique
 * COURTE réduite à la seule copie mémoire (jamais d'appel bloquant ni de
 * travail long sous le verrou). Le store est écrit par la tâche WS
 * (réponse à machine.device_power.devices, notify_power_changed) et lu par
 * l'UI sur une autre tâche ; sur cet ESP32-S3 SMP elles peuvent réellement
 * s'exécuter en parallèle, la copie doit donc être atomique vis-à-vis
 * d'elles. Hors cible (host-test / simulateur, pas d'ESP_PLATFORM), il n'y a
 * pas de FreeRTOS et le code est mono-thread : le verrou retombe sur un
 * no-op, même politique que journal.h. */
#include "power_devices.h"

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

static power_devices_t g_store;

void power_devices_definir(const power_devices_t *src)
{
    if (src == NULL) {
        return;
    }
    VERROU_PRENDRE();
    /* `generation` du store est monotone à travers les appels -- celle
     * portée par `*src` (généralement une locale non initialisée côté
     * appelant, voir rpc_lire_power_devices) est délibérément ignorée ; sans
     * cette précaution, copier `*src` tel quel écraserait le compteur du
     * store à chaque appel au lieu de le faire progresser. */
    uint32_t generation = g_store.generation + 1;
    g_store = *src;
    g_store.generation = generation;
    VERROU_RENDRE();
}

void power_devices_lire(power_devices_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *dest = g_store;
    VERROU_RENDRE();
}

void power_devices_maj_un(const char *nom, bool allumee)
{
    if (nom == NULL) {
        return;
    }
    VERROU_PRENDRE();
    for (uint8_t i = 0; i < g_store.nb; i++) {
        if (strcmp(g_store.devices[i].nom, nom) == 0) {
            g_store.devices[i].allumee = allumee;
            g_store.devices[i].connue = true;
            g_store.generation++;
            break;
        }
        /* Prise inconnue (pas trouvée dans les `nb` entrées existantes) :
         * no-op silencieux -- cette fonction met à jour une prise DÉJÀ
         * connue du store (issue du fetch initial), elle n'en crée jamais.
         * `generation` n'est alors pas touchée : rien n'a changé dans le
         * store, aucune raison de faire croire à l'UI qu'il y a du neuf. */
    }
    VERROU_RENDRE();
}
