/* Journal réseau : sans port série, la console n'est lisible que par le
 * réseau. Le relais posé ici avec esp_log_set_vprintf() est appelé depuis
 * n'importe quelle tâche, y compris pendant le traitement différé d'une
 * interruption — donc :
 *   - rien n'est alloué dedans (tampon statique, pas de malloc/strdup) ;
 *   - aucun ESP_LOG* n'est appelé ici (ce serait réentrant sur le même
 *     verrou de log et pourrait boucler) ;
 *   - le mutex est pris avec un délai nul : si une écriture concurrente le
 *     détient déjà, la ligne est perdue plutôt que d'attendre — perdre une
 *     ligne de log est toujours préférable à bloquer l'appelant. */

#include "netlog.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* NETLOG_TAILLE vit dans netlog.h (partagée avec le tampon de /log, web.c). */

/* EN PSRAM depuis le fix RAM interne du 2026-08-14 : ces 16 Kio vivaient en
 * .bss RAM interne alors que la machine tourne à ~13-54 Kio de marge interne
 * (mesuré via /status.heap_interne pendant le diagnostic USB) -- le tampon
 * d'un journal de CONFORT ne doit jamais concurrencer les piles de tâches et
 * les allocations DMA/WiFi qui, elles, n'ont pas le choix. Alloué une fois
 * dans netlog_init() ; NULL si la PSRAM manque, et tout netlog devient no-op
 * (mêmes gardes que le mutex juste en dessous). L'écriture depuis n'importe
 * quelle tâche reste valable : la PSRAM est adressable partout hors contexte
 * cache-flash-désactivé, où ESP_LOG* est de toute façon déjà interdit. */
static char *tampon;
static size_t position;       /* prochain octet libre, modulo NETLOG_TAILLE */
static bool a_bien_bouclee;    /* vrai dès que le tampon a fait un tour complet */
static SemaphoreHandle_t mutex;
static vprintf_like_t vprintf_original;

static void ecrire_dans_tampon(const char *donnees, size_t longueur)
{
    if (mutex == NULL || tampon == NULL) {
        return;
    }
    /* Délai nul : on abandonne l'écriture plutôt que d'attendre. */
    if (xSemaphoreTake(mutex, 0) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < longueur; i++) {
        tampon[position] = donnees[i];
        position++;
        if (position >= NETLOG_TAILLE) {
            position = 0;
            a_bien_bouclee = true;
        }
    }
    xSemaphoreGive(mutex);
}

static int relais_vprintf(const char *format, va_list args)
{
    /* On formate dans un tampon de pile fixe, sans allocation : la copie de
     * va_list sert uniquement à ne pas consommer `args`, qui doit encore
     * servir pour le relais vers la sortie d'origine ci-dessous. */
    char ligne[256];
    va_list copie;
    va_copy(copie, args);
    int longueur = vsnprintf(ligne, sizeof(ligne), format, copie);
    va_end(copie);

    if (longueur > 0) {
        size_t a_ecrire = (size_t)longueur < sizeof(ligne) ? (size_t)longueur : sizeof(ligne) - 1;
        ecrire_dans_tampon(ligne, a_ecrire);
    }

    if (vprintf_original != NULL) {
        return vprintf_original(format, args);
    }
    return vprintf(format, args);
}

esp_err_t netlog_init(void)
{
    /* Tampon d'abord, relais en dernier : le relais ne doit jamais être posé
     * tant que tout ce qu'il touche n'existe pas (il est appelé depuis
     * n'importe quelle tâche dès l'instant où il est posé). */
    tampon = (char *)heap_caps_malloc(NETLOG_TAILLE, MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        return ESP_ERR_NO_MEM;
    }
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        /* Sans mutex, aucun consommateur ne touchera jamais ce tampon : le
         * rendre plutôt que de laisser 16 Kio orphelins (personne ne
         * rappelle netlog_init()). */
        heap_caps_free(tampon);
        tampon = NULL;
        return ESP_ERR_NO_MEM;
    }
    vprintf_original = esp_log_set_vprintf(relais_vprintf);
    return ESP_OK;
}

size_t netlog_snapshot(char *out, size_t len)
{
    if (out == NULL || len == 0) {
        return 0;
    }
    if (mutex == NULL || tampon == NULL || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        out[0] = '\0';
        return 0;
    }

    size_t disponible = a_bien_bouclee ? NETLOG_TAILLE : position;
    size_t debut = a_bien_bouclee ? position : 0; /* plus ancien octet conservé */
    size_t a_copier = disponible < (len - 1) ? disponible : (len - 1);
    size_t saut = disponible - a_copier; /* on ne garde que les plus récents */

    for (size_t i = 0; i < a_copier; i++) {
        size_t indice = (debut + saut + i) % NETLOG_TAILLE;
        out[i] = tampon[indice];
    }
    out[a_copier] = '\0';

    xSemaphoreGive(mutex);
    return a_copier;
}
