/* Implémentation de parc_parse.h -- voir le header. Mêmes idiomes cJSON que
 * moonraker_parse.c (cJSON_ParseWithLength, GetObjectItemCaseSensitive,
 * IsNumber avant lecture), en beaucoup plus petit. */
#include "parc_parse.h"

#include <string.h>

#include "cJSON.h"

bool parc_parse_reponse(const char *json, size_t longueur, parc_etat_t *sortie)
{
    if (sortie != NULL) {
        memset(sortie, 0, sizeof(*sortie));
    }
    if (json == NULL || longueur == 0 || sortie == NULL) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL) {
        return false;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(racine, "result");
    cJSON *status = cJSON_GetObjectItemCaseSensitive(result, "status");
    if (!cJSON_IsObject(status)) {
        cJSON_Delete(racine);
        return false;
    }

    sortie->sonde = true;
    sortie->atteignable = true;

    cJSON *print_stats = cJSON_GetObjectItemCaseSensitive(status, "print_stats");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(print_stats, "state");
    if (cJSON_IsString(state) && state->valuestring != NULL) {
        size_t n = strlen(state->valuestring);
        if (n >= sizeof(sortie->etat)) {
            n = sizeof(sortie->etat) - 1;
        }
        memcpy(sortie->etat, state->valuestring, n);
        sortie->etat[n] = '\0';
    }

    cJSON *extruder = cJSON_GetObjectItemCaseSensitive(status, "extruder");
    cJSON *temperature = cJSON_GetObjectItemCaseSensitive(extruder, "temperature");
    if (cJSON_IsNumber(temperature)) {
        sortie->buse = (float)temperature->valuedouble;
    }

    cJSON *heater_bed = cJSON_GetObjectItemCaseSensitive(status, "heater_bed");
    temperature = cJSON_GetObjectItemCaseSensitive(heater_bed, "temperature");
    if (cJSON_IsNumber(temperature)) {
        sortie->lit = (float)temperature->valuedouble;
    }

    cJSON *display_status = cJSON_GetObjectItemCaseSensitive(status, "display_status");
    cJSON *progress = cJSON_GetObjectItemCaseSensitive(display_status, "progress");
    if (cJSON_IsNumber(progress)) {
        double pct = progress->valuedouble * 100.0;
        if (pct < 0.0) {
            pct = 0.0;
        }
        if (pct > 100.0) {
            pct = 100.0;
        }
        sortie->progression_pct = (uint8_t)pct;
    }

    cJSON_Delete(racine);
    return true;
}
