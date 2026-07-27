#include "moonraker_parse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/* Lit un nombre optionnel ; laisse `defaut` si le champ manque ou n'en est pas un. */
static float nombre_ou(const cJSON *parent, const char *cle, float defaut)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(parent, cle);
    return cJSON_IsNumber(n) ? (float)n->valuedouble : defaut;
}

/* Copie une chaîne optionnelle en tronquant proprement. */
static void texte_ou_vide(const cJSON *parent, const char *cle, char *sortie, size_t taille)
{
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(parent, cle);
    if (cJSON_IsString(s) && s->valuestring != NULL) {
        snprintf(sortie, taille, "%s", s->valuestring);
    } else {
        sortie[0] = '\0';
    }
}

bool moonraker_parse_status(const char *json, size_t longueur, etat_klipper_t *sortie)
{
    if (json == NULL || longueur == 0 || sortie == NULL) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL) {
        return false;
    }

    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *statut = cJSON_GetObjectItemCaseSensitive(resultat, "status");
    if (!cJSON_IsObject(statut)) {
        cJSON_Delete(racine);
        return false;
    }

    /* À partir d'ici l'analyse réussit : on écrit dans une structure locale puis
     * on la copie d'un bloc, pour ne jamais laisser `sortie` à moitié remplie si
     * un champ manque. */
    etat_klipper_t e;
    memset(&e, 0, sizeof(e));

    const cJSON *extrudeur = cJSON_GetObjectItemCaseSensitive(statut, "extruder");
    e.buse_actuelle = nombre_ou(extrudeur, "temperature", 0.0f);
    e.buse_consigne = nombre_ou(extrudeur, "target", 0.0f);

    const cJSON *plateau = cJSON_GetObjectItemCaseSensitive(statut, "heater_bed");
    e.plateau_actuel = nombre_ou(plateau, "temperature", 0.0f);
    e.plateau_consigne = nombre_ou(plateau, "target", 0.0f);

    const cJSON *stats = cJSON_GetObjectItemCaseSensitive(statut, "print_stats");
    texte_ou_vide(stats, "filename", e.fichier, sizeof(e.fichier));
    texte_ou_vide(stats, "state", e.etat, sizeof(e.etat));
    float ecoule = nombre_ou(stats, "print_duration", 0.0f);

    const cJSON *sdcard = cJSON_GetObjectItemCaseSensitive(statut, "virtual_sdcard");
    e.progression = nombre_ou(sdcard, "progress", 0.0f);

    e.impression_en_cours = (strcmp(e.etat, "printing") == 0);
    e.impression_en_pause = (strcmp(e.etat, "paused") == 0);

    /* Moonraker ne fournit pas de temps restant : on l'estime à partir du temps
     * écoulé rapporté à la progression. Sous 1 %, le rapport explose et
     * produirait une valeur absurde — mieux vaut ne rien afficher. */
    if (e.progression > 0.01f && e.progression < 1.0f && ecoule > 0.0f) {
        float restant = ecoule * (1.0f - e.progression) / e.progression;
        /* Borner AVANT la conversion. Convertir un flottant non fini ou hors de
         * portée vers un entier non signé est un comportement indéfini (C11
         * 6.3.1.4) : une réponse malformée ne doit pas en produire dans un
         * firmware qui tourne sans surveillance. Le plafond est un maximum
         * d'affichage, pas UINT32_MAX — annoncer un siècle de temps restant
         * n'informerait pas davantage. Un `restant` non fini (ex. un
         * `print_duration` de 1e40, qui devient +infini une fois retreci en
         * float) est traité comme « au moins aussi grand que le plafond »,
         * pas comme une absence de valeur : on plafonne, on ne remet pas a
         * zero silencieusement. */
        if (!isfinite(restant) || restant > (float)KLIPPER_TEMPS_RESTANT_MAX_S) {
            restant = (float)KLIPPER_TEMPS_RESTANT_MAX_S;
        }
        if (restant > 0.0f) {
            e.temps_restant_s = (uint32_t)(restant + 0.5f);
        }
    }

    *sortie = e;
    cJSON_Delete(racine);
    return true;
}
