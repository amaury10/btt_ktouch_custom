/* Implémentation de usb_fichiers.h -- voir ce header pour le contrat et le
 * POURQUOI. Verrou : copie EXACTE du patron de klipper_fichiers.c (portMUX,
 * section critique COURTE réduite à la seule copie mémoire, jamais d'appel
 * bloquant sous le verrou ; no-op hors ESP_PLATFORM, mono-thread côté
 * host-test/simulateur). */
#include "usb_fichiers.h"

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

static usb_fichiers_t g_store;
static uint32_t       g_generation;

void usb_fichiers_definir(bool monte, const usb_fichier_t *fichiers, uint8_t nb, bool tronques)
{
    if (nb > USB_FICHIERS_MAX) {
        nb = USB_FICHIERS_MAX; /* garde défensive, ne devrait jamais arriver depuis app_main.c */
    }

    VERROU_PRENDRE();
    g_store.monte = monte;
    g_store.nb = nb;
    g_store.tronques = tronques;
    if (nb > 0 && fichiers != NULL) {
        memcpy(g_store.fichiers, fichiers, (size_t)nb * sizeof(usb_fichier_t));
    }
    if (nb < USB_FICHIERS_MAX) {
        /* Emplacements au-delà de nb remis à zéro -- jamais un débris du
           scan précédent (un chemin plus long, ex.) laissé visible si un
           futur lecteur bornait mal `nb`, même discipline défensive que le
           reste de ce dépôt. */
        memset(&g_store.fichiers[nb], 0, (size_t)(USB_FICHIERS_MAX - nb) * sizeof(usb_fichier_t));
    }
    g_generation++;
    VERROU_RENDRE();
}

void usb_fichiers_lire(usb_fichiers_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *dest = g_store;
    VERROU_RENDRE();
}

uint32_t usb_fichiers_generation(void)
{
    uint32_t g;
    VERROU_PRENDRE();
    g = g_generation;
    VERROU_RENDRE();
    return g;
}
