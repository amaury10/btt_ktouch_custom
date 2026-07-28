#include "moonraker_parse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backend.h"
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
        /* Deux cas bien distincts, à ne pas confondre :
         *
         * - fini mais au-delà du plafond (ex. une durée écoulée énorme avec une
         *   progression proche du seuil bas) : l'estimation est réelle, juste
         *   trop grande pour être affichée telle quelle. On la plafonne — on
         *   connaît la réponse, on affiche le plus grand nombre représentable.
         * - non fini (ex. un `print_duration` de 1e40, qui devient +infini une
         *   fois rétréci en float) : la réponse est malformée, l'estimation ne
         *   veut rien dire (littéralement des années-lumière de temps restant).
         *   On NE plafonne PAS : `temps_restant_s` garde sa valeur par défaut 0,
         *   qui est justement le sens documenté de ce champ (« 0 si inconnu »,
         *   voir etat_klipper.h). Plafonner ici transformerait une entrée
         *   invalide en une estimation d'allure plausible.
         *
         * Convertir un flottant non fini ou hors de portée directement vers un
         * entier non signé est de toute façon un comportement indéfini (C11
         * 6.3.1.4) : la borne doit être posée avant la conversion, pas après. */
        if (isfinite(restant) && restant > 0.0f) {
            if (restant > (float)KLIPPER_TEMPS_RESTANT_MAX_S) {
                restant = (float)KLIPPER_TEMPS_RESTANT_MAX_S;
            }
            e.temps_restant_s = (uint32_t)(restant + 0.5f);
        }
    }

    *sortie = e;
    cJSON_Delete(racine);
    return true;
}

bool moonraker_chemin_commande(const char *action, char *chemin, size_t taille)
{
    if (action == NULL || chemin == NULL || taille == 0) {
        return false;
    }

    const char *trouve;
    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0) {
        trouve = "printer/print/pause";
    } else if (strcmp(action, BACKEND_ACTION_REPRENDRE) == 0) {
        trouve = "printer/print/resume";
    } else if (strcmp(action, BACKEND_ACTION_ANNULER) == 0) {
        trouve = "printer/print/cancel";
    } else if (strcmp(action, BACKEND_ACTION_URGENCE) == 0) {
        trouve = "printer/emergency_stop";
    } else {
        /* Action inconnue : ne rien ecrire dans `chemin`, l'appelant ne doit
         * jamais emettre de requete avec un tampon a moitie rempli ou perime
         * d'un appel precedent. */
        return false;
    }

    snprintf(chemin, taille, "%s", trouve);
    return true;
}
