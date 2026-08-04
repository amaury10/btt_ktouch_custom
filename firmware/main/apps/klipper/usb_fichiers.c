/* Implémentation de usb_fichiers.h -- voir ce header pour le contrat et le
 * POURQUOI. Verrou : copie EXACTE du patron de klipper_fichiers.c (portMUX,
 * section critique COURTE réduite à la seule copie mémoire, jamais d'appel
 * bloquant sous le verrou ; no-op hors ESP_PLATFORM, mono-thread côté
 * host-test/simulateur).
 *
 * Stockage (g_store, ~4 Ko -- USB_FICHIERS_MAX(32) * sizeof(usb_fichier_t))
 * EN PSRAM côté ESP (fix RAM interne, lot Power/Console/Miniatures/USB : ce
 * store vivait auparavant en .bss RAM interne -- voir la mémoire du projet
 * pour le symptôme, "Error create websocket task"). Même patron que
 * console_log.c : allocation paresseuse via heap_caps_malloc(MALLOC_CAP_SPIRAM)
 * avec garde NULL -- si la PSRAM manque, le store se DÉSACTIVE proprement
 * (no-op sûr), PAS de repli sur une seconde instance statique EN RAM
 * INTERNE (ce qui annulerait le gain de ce fix). Côté host-test/simulateur
 * (pas d'ESP_PLATFORM), une simple instance statique suffit. */
#include "usb_fichiers.h"

#include <string.h>

#include "journal.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)

static const char *TAG = "usb_fichiers";

static usb_fichiers_t *g_store; /* PSRAM ; NULL tant que non allouee OU apres un echec definitif */
static bool            g_echec; /* vrai apres un echec d'allocation -- pas de nouvel essai */

/* Même raisonnement que store_obtenir() dans console_log.c : allocation
 * paresseuse HORS verrou ; rend NULL si heap_caps_malloc() échoue -- AUCUN
 * repli sur une seconde instance statique EN RAM INTERNE (annulerait le gain
 * de ce fix), `g_echec` évite seulement de retenter à chaque appel. Les
 * fonctions publiques ci-dessous traitent un NULL comme "store
 * indisponible", jamais un déréférencement. */
static usb_fichiers_t *store_obtenir(void)
{
    if (g_store != NULL) {
        return g_store;
    }
    if (g_echec) {
        return NULL;
    }
    usb_fichiers_t *tampon = (usb_fichiers_t *)heap_caps_malloc(sizeof(usb_fichiers_t), MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour la liste des fichiers USB (%u octets) ; "
                       "store indisponible", (unsigned)sizeof(usb_fichiers_t));
        g_echec = true;
        return NULL;
    }
    memset(tampon, 0, sizeof(*tampon));
    g_store = tampon;
    return g_store;
}
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
static usb_fichiers_t g_store_hote;
static usb_fichiers_t *store_obtenir(void)
{
    return &g_store_hote;
}
#endif

static uint32_t g_generation;

void usb_fichiers_definir(bool monte, const usb_fichier_t *fichiers, uint8_t nb, bool tronques)
{
    if (nb > USB_FICHIERS_MAX) {
        nb = USB_FICHIERS_MAX; /* garde défensive, ne devrait jamais arriver depuis usb_scan.c */
    }

    usb_fichiers_t *store = store_obtenir();
    if (store == NULL) {
        return; /* PSRAM indisponible (deja journalise par store_obtenir()) : no-op */
    }

    VERROU_PRENDRE();
    store->monte = monte;
    store->nb = nb;
    store->tronques = tronques;
    if (nb > 0 && fichiers != NULL) {
        memcpy(store->fichiers, fichiers, (size_t)nb * sizeof(usb_fichier_t));
    }
    if (nb < USB_FICHIERS_MAX) {
        /* Emplacements au-delà de nb remis à zéro -- jamais un débris du
           scan précédent (un chemin plus long, ex.) laissé visible si un
           futur lecteur bornait mal `nb`, même discipline défensive que le
           reste de ce dépôt. */
        memset(&store->fichiers[nb], 0, (size_t)(USB_FICHIERS_MAX - nb) * sizeof(usb_fichier_t));
    }
    g_generation++;
    VERROU_RENDRE();
}

void usb_fichiers_lire(usb_fichiers_t *dest)
{
    if (dest == NULL) {
        return;
    }
    usb_fichiers_t *store = store_obtenir();
    if (store == NULL) {
        /* PSRAM indisponible : un store vide (clé absente, aucun fichier)
           est la lecture la plus honnête -- jamais de contenu indéterminé. */
        memset(dest, 0, sizeof(*dest));
        return;
    }
    VERROU_PRENDRE();
    *dest = *store;
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
