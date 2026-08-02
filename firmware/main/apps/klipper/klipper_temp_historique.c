/* Implémentation du store d'historique de température -- voir
 * klipper_temp_historique.h pour le contrat et le POURQUOI (RAM interne :
 * même raisonnement que klipper_fichiers.c, voir son commentaire de tête).
 *
 * UNE seule instance statique du store (g_points/g_tete/g_nb/g_gen/g_present,
 * ~2,2 Ko en RAM interne .bss).
 *
 * Verrou : même politique que klipper_fichiers.c -- un portMUX_TYPE, section
 * critique COURTE réduite à des écritures/lectures de scalaires (jamais
 * d'appel bloquant ni de travail long sous le verrou). Hors cible (host-test
 * / simulateur, pas d'ESP_PLATFORM), il n'y a pas de FreeRTOS et le code est
 * mono-thread : le verrou retombe sur un no-op. */
#include "klipper_temp_historique.h"

#include <math.h>

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

static int16_t  g_points[KLIPPER_HISTO_SERIES][KLIPPER_HISTO_POINTS];
static uint16_t g_tete;
static uint16_t g_nb;
static uint32_t g_gen;
static bool     g_present[KLIPPER_HISTO_SERIES];

/* Arrondit une température en °C entier, en se protégeant d'un NaN/Inf
 * (capteur débranché, division par zéro côté Moonraker...) : un tel écart
 * est ramené à 0 avant l'arrondi plutôt que de propager une valeur
 * indéterminée dans le tampon (voir VERIFIER_FLOAT/test_harnais.c pour le
 * même réflexe déjà en place ailleurs dans ce dépôt). */
static int16_t arrondir_temperature(float actuelle)
{
    if (!isfinite(actuelle)) {
        actuelle = 0.0f;
    }
    return (int16_t)lroundf(actuelle);
}

void klipper_temp_historique_pousser(const etat_klipper_t *e)
{
    if (e == NULL) {
        return;
    }

    VERROU_PRENDRE();
    for (uint8_t i = 0; i < KLIPPER_EXTRUDEURS_MAX; i++) {
        g_present[i]         = e->extrudeurs[i].presente;
        g_points[i][g_tete]  = arrondir_temperature(e->extrudeurs[i].actuelle);
    }
    g_present[KLIPPER_EXTRUDEURS_MAX]        = e->plateau.presente;
    g_points[KLIPPER_EXTRUDEURS_MAX][g_tete] = arrondir_temperature(e->plateau.actuelle);

    g_tete = (uint16_t)((g_tete + 1) % KLIPPER_HISTO_POINTS);
    if (g_nb < KLIPPER_HISTO_POINTS) {
        g_nb++;
    }
    g_gen++;
    VERROU_RENDRE();
}

uint32_t klipper_temp_historique_generation(void)
{
    uint32_t gen;
    VERROU_PRENDRE();
    gen = g_gen;
    VERROU_RENDRE();
    return gen;
}

bool klipper_temp_historique_serie_presente(uint8_t serie)
{
    bool presente;
    if (serie >= KLIPPER_HISTO_SERIES) {
        return false;
    }
    VERROU_PRENDRE();
    presente = g_present[serie];
    VERROU_RENDRE();
    return presente;
}

bool klipper_temp_historique_dernier(uint8_t serie, int16_t *sortie)
{
    if (serie >= KLIPPER_HISTO_SERIES || sortie == NULL) {
        return false;
    }

    VERROU_PRENDRE();
    if (g_nb == 0) {
        VERROU_RENDRE();
        return false;
    }
    uint16_t idx = (uint16_t)((g_tete + KLIPPER_HISTO_POINTS - 1) % KLIPPER_HISTO_POINTS);
    *sortie = g_points[serie][idx];
    VERROU_RENDRE();
    return true;
}

size_t klipper_temp_historique_serie(uint8_t serie, int16_t *dest, size_t max)
{
    if (serie >= KLIPPER_HISTO_SERIES || dest == NULL) {
        return 0;
    }

    VERROU_PRENDRE();
    size_t n = g_nb;
    if (n > max) {
        n = max;
    }
    uint16_t plus_ancien = (uint16_t)((g_tete + KLIPPER_HISTO_POINTS - g_nb) % KLIPPER_HISTO_POINTS);
    for (size_t k = 0; k < n; k++) {
        uint16_t idx = (uint16_t)((plus_ancien + k) % KLIPPER_HISTO_POINTS);
        dest[k] = g_points[serie][idx];
    }
    VERROU_RENDRE();
    return n;
}
