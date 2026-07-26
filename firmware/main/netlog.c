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

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NETLOG_TAILLE (16 * 1024)

static char tampon[NETLOG_TAILLE];
static size_t position;       /* prochain octet libre, modulo NETLOG_TAILLE */
static bool a_bien_bouclee;    /* vrai dès que le tampon a fait un tour complet */
static SemaphoreHandle_t mutex;
static vprintf_like_t vprintf_original;

static void ecrire_dans_tampon(const char *donnees, size_t longueur)
{
    if (mutex == NULL) {
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
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
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
    if (mutex == NULL || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
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
