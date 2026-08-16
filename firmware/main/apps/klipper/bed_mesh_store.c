/* Implémentation de bed_mesh_store.h -- voir ce header. Patron usb_fichiers/
 * parc_imprimantes EXACT : instance PSRAM paresseuse sans repli RAM interne,
 * verrou portMUX court (la copie ~1 Ko reste bornée et rare -- un mesh
 * change à la calibration, pas en continu), no-op host. */
#include "bed_mesh_store.h"

#include <string.h>

#include "journal.h"

#ifdef ESP_PLATFORM
static const char *TAG = "bed_mesh"; /* seul le chemin ESP journalise (echec PSRAM) */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)

static bed_mesh_t *g_store;
static bool        g_echec;

static bed_mesh_t *store_obtenir(void)
{
    if (g_store != NULL) {
        return g_store;
    }
    if (g_echec) {
        return NULL;
    }
    bed_mesh_t *tampon = (bed_mesh_t *)heap_caps_malloc(sizeof(bed_mesh_t), MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour le store bed_mesh (%u octets)",
                       (unsigned)sizeof(bed_mesh_t));
        g_echec = true;
        return NULL;
    }
    memset(tampon, 0, sizeof(*tampon));
    /* Publication SOUS verrou (revue du 2026-08-15, L3) : deux taches
       peuvent atteindre ce point ensemble a la toute premiere utilisation
       (ex. tache WS qui publie pendant que la tache LVGL lit) -- sans ce
       verrou, chacune installait SON tampon et l'un des deux (avec sa
       premiere donnee deposee) etait perdu, plus une fuite PSRAM. Le
       perdant libere son tampon et repart sur celui du gagnant. */
    VERROU_PRENDRE();
    if (g_store == NULL) {
        g_store = tampon;
        tampon = NULL;
    }
    VERROU_RENDRE();
    if (tampon != NULL) {
        heap_caps_free(tampon);
    }
    return g_store;
}
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
static bed_mesh_t g_store_hote;
static bed_mesh_t *store_obtenir(void)
{
    return &g_store_hote;
}
#endif

static uint32_t g_generation;

void bed_mesh_definir(const bed_mesh_t *mesh)
{
    if (mesh == NULL) {
        return;
    }
    bed_mesh_t *store = store_obtenir();
    if (store == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *store = *mesh;
    store->profil[sizeof(store->profil) - 1] = '\0';
    if (store->nb_x > BED_MESH_MAX) {
        store->nb_x = BED_MESH_MAX;
    }
    if (store->nb_y > BED_MESH_MAX) {
        store->nb_y = BED_MESH_MAX;
    }
    g_generation++;
    VERROU_RENDRE();
}

void bed_mesh_lire(bed_mesh_t *dest)
{
    if (dest == NULL) {
        return;
    }
    bed_mesh_t *store = store_obtenir();
    if (store == NULL) {
        memset(dest, 0, sizeof(*dest));
        return;
    }
    VERROU_PRENDRE();
    *dest = *store;
    VERROU_RENDRE();
}

uint32_t bed_mesh_generation(void)
{
    uint32_t generation;
    VERROU_PRENDRE();
    generation = g_generation;
    VERROU_RENDRE();
    return generation;
}

/* Liste des profils : ~230 octets, une instance statique suffit (pas la
 * peine du détour PSRAM paresseux réservé aux gros blocs -- même ordre de
 * grandeur que les statiques de moonraker_boite). Même verrou et MÊME
 * compteur de génération que la carte, voir bed_mesh_store.h. */
static bed_mesh_profils_t g_profils;

void bed_mesh_profils_definir(const bed_mesh_profils_t *profils)
{
    if (profils == NULL) {
        return;
    }
    VERROU_PRENDRE();
    g_profils = *profils;
    if (g_profils.nb > BED_MESH_PROFILS_MAX) {
        g_profils.nb = BED_MESH_PROFILS_MAX; /* garde défensive */
    }
    for (uint8_t i = 0; i < BED_MESH_PROFILS_MAX; i++) {
        g_profils.noms[i][BED_MESH_PROFIL_NOM_MAX - 1] = '\0';
    }
    g_generation++;
    VERROU_RENDRE();
}

void bed_mesh_profils_lire(bed_mesh_profils_t *dest)
{
    if (dest == NULL) {
        return;
    }
    VERROU_PRENDRE();
    *dest = g_profils;
    VERROU_RENDRE();
}

bool bed_mesh_position_point(const bed_mesh_t *mesh, uint8_t ligne, uint8_t colonne,
                             float *x, float *y)
{
    if (mesh == NULL || !mesh->present || x == NULL || y == NULL ||
        ligne >= mesh->nb_y || colonne >= mesh->nb_x) {
        return false;
    }
    /* Grille de sondage régulière : colonne (X) et ligne (Y) interpolées
       indépendamment. nb == 1 : un seul point posé sur la borne min. */
    *x = mesh->mesh_min_x;
    if (mesh->nb_x > 1) {
        *x += (float)colonne * (mesh->mesh_max_x - mesh->mesh_min_x) / (float)(mesh->nb_x - 1);
    }
    *y = mesh->mesh_min_y;
    if (mesh->nb_y > 1) {
        *y += (float)ligne * (mesh->mesh_max_y - mesh->mesh_min_y) / (float)(mesh->nb_y - 1);
    }
    return true;
}
