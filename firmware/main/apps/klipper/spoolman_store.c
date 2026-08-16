/* Implémentation de spoolman_store.h -- voir ce header. Patron bed_mesh_store
 * EXACT : instance PSRAM paresseuse (publication CAS sous verrou, revue du
 * 2026-08-15 L3) sans repli RAM interne, verrou portMUX court, no-op host. */
#include "spoolman_store.h"

#include <string.h>

#include "journal.h"

#ifdef ESP_PLATFORM
static const char *TAG = "spoolman"; /* seul le chemin ESP journalise (echec PSRAM) */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)

static spoolman_liste_t *g_liste;
static bool              g_echec;

static spoolman_liste_t *liste_obtenir(void)
{
    if (g_liste != NULL) {
        return g_liste;
    }
    if (g_echec) {
        return NULL;
    }
    spoolman_liste_t *tampon =
        (spoolman_liste_t *)heap_caps_malloc(sizeof(spoolman_liste_t), MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour la liste Spoolman (%u octets)",
                       (unsigned)sizeof(spoolman_liste_t));
        g_echec = true;
        return NULL;
    }
    memset(tampon, 0, sizeof(*tampon));
    /* Publication SOUS verrou (revue du 2026-08-15, L3) : deux taches
       peuvent atteindre ce point ensemble a la toute premiere utilisation
       (tache WS qui publie pendant que la tache LVGL lit). Le perdant libere
       son tampon et repart sur celui du gagnant. */
    VERROU_PRENDRE();
    if (g_liste == NULL) {
        g_liste = tampon;
        tampon = NULL;
    }
    VERROU_RENDRE();
    if (tampon != NULL) {
        heap_caps_free(tampon);
    }
    return g_liste;
}
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
static spoolman_liste_t g_liste_hote;
static spoolman_liste_t *liste_obtenir(void)
{
    return &g_liste_hote;
}
#endif

static uint32_t g_generation;

/* L'etat est minuscule (12 octets) : une statique suffit, pas de detour
 * PSRAM -- meme raisonnement que la liste de profils de bed_mesh_store.c.
 * Initialise a "aucune bobine, rien de connu" : sans cela, un id_actif a 0
 * ferait passer la bobine 0 pour active avant le premier statut recu. */
static spoolman_etat_t g_etat = { .id_actif = SPOOLMAN_AUCUNE_BOBINE,
                                  .connecte = false,
                                  .statut_connu = false };

void spoolman_definir_liste(const spoolman_liste_t *liste)
{
    if (liste == NULL) {
        return;
    }
    spoolman_liste_t *store = liste_obtenir();
    if (store == NULL) {
        return; /* PSRAM indisponible (deja journalise) : no-op */
    }
    VERROU_PRENDRE();
    *store = *liste;
    if (store->nb > SPOOLMAN_BOBINES_MAX) {
        store->nb = SPOOLMAN_BOBINES_MAX; /* garde defensive */
    }
    store->connue = true;
    /* Terminaisons forcees : le store ne rend JAMAIS une chaine non
       terminee, quoi qu'ait produit l'appelant. */
    for (uint8_t i = 0; i < SPOOLMAN_BOBINES_MAX; i++) {
        store->bobines[i].filament[SPOOLMAN_TEXTE_MAX - 1] = '\0';
        store->bobines[i].fabricant[SPOOLMAN_TEXTE_MAX - 1] = '\0';
        store->bobines[i].matiere[SPOOLMAN_MATIERE_MAX - 1] = '\0';
    }
    g_generation++;
    VERROU_RENDRE();
}

void spoolman_lire_liste(spoolman_liste_t *dest)
{
    if (dest == NULL) {
        return;
    }
    spoolman_liste_t *store = liste_obtenir();
    if (store == NULL) {
        memset(dest, 0, sizeof(*dest)); /* liste vide et NON connue : lecture honnete */
        return;
    }
    VERROU_PRENDRE();
    *dest = *store;
    VERROU_RENDRE();
}

void spoolman_definir_actif(int32_t id)
{
    VERROU_PRENDRE();
    g_etat.id_actif = (id >= 0) ? id : SPOOLMAN_AUCUNE_BOBINE;
    g_generation++;
    VERROU_RENDRE();
}

void spoolman_definir_connecte(bool connecte)
{
    VERROU_PRENDRE();
    g_etat.connecte = connecte;
    g_etat.statut_connu = true;
    g_generation++;
    VERROU_RENDRE();
}

void spoolman_lire_etat(spoolman_etat_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *dest = g_etat;
    VERROU_RENDRE();
}

uint32_t spoolman_generation(void)
{
    uint32_t generation;
    VERROU_PRENDRE();
    generation = g_generation;
    VERROU_RENDRE();
    return generation;
}
