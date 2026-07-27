#include "reglages.h"

#include <string.h>

#include "journal.h"
#include "nvs.h"

/* Étiquette de journalisation : convention reprise du reste du firmware (voir
 * app_main.c, backend_factice.c), pour que /log reste lisible par module. */
static const char *TAG = "reglages";

#define REGLAGES_CLE_ADRESSE "hote_adresse"
#define REGLAGES_CLE_PORT    "hote_port"
#define REGLAGES_CLE_BACKEND "backend"

#define REGLAGES_PORT_DEFAUT    7125u
#define REGLAGES_BACKEND_DEFAUT "moonraker"

/* Longueur maximale d'un nom de backend stocké ("moonraker", "factice", et
 * ceux à venir) : large marge au-delà des noms connus, pour ne pas avoir à
 * revenir ici à chaque backend ajouté. */
#define REGLAGES_BACKEND_LONGUEUR_MAX 32

/* Cache en mémoire, peuplé par reglages_charger() depuis la NVS — ou laissé
 * à ces valeurs par défaut si la clé est absente ou si la NVS est
 * inaccessible (voir le commentaire d'en-tête de reglages.h : un échec
 * d'ouverture n'est jamais fatal). Les accesseurs ne relisent jamais la
 * NVS : ils rendent ce cache, que les fonctions de définition tiennent à
 * jour à chaque écriture réussie. */
static backend_hote_t g_hote = {
    .adresse = "",
    .port = REGLAGES_PORT_DEFAUT,
};
static char g_backend[REGLAGES_BACKEND_LONGUEUR_MAX] = REGLAGES_BACKEND_DEFAUT;

esp_err_t reglages_charger(void)
{
    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(REGLAGES_ESPACE_NOMS, NVS_READWRITE, &handle);
    if (erreur != ESP_OK) {
        /* Non fatal, volontairement : l'appareil continue avec les valeurs
         * par défaut déjà en place ci-dessus, diagnosticable via /log,
         * plutôt que de refuser de démarrer pour un problème de NVS. */
        JOURNAL_ERREUR(TAG, "nvs_open a echoue (%s) ; reglages par defaut conserves",
                       esp_err_to_name(erreur));
        return erreur;
    }

    size_t taille_adresse = sizeof(g_hote.adresse);
    esp_err_t erreur_adresse = nvs_get_str(handle, REGLAGES_CLE_ADRESSE, g_hote.adresse, &taille_adresse);
    if (erreur_adresse != ESP_OK) {
        g_hote.adresse[0] = '\0';
        if (erreur_adresse != ESP_ERR_NVS_NOT_FOUND) {
            JOURNAL_ALERTE(TAG, "lecture de %s impossible (%s) ; valeur par defaut",
                           REGLAGES_CLE_ADRESSE, esp_err_to_name(erreur_adresse));
        }
    }

    uint16_t port = REGLAGES_PORT_DEFAUT;
    esp_err_t erreur_port = nvs_get_u16(handle, REGLAGES_CLE_PORT, &port);
    if (erreur_port == ESP_OK) {
        g_hote.port = port;
    } else {
        g_hote.port = REGLAGES_PORT_DEFAUT;
        if (erreur_port != ESP_ERR_NVS_NOT_FOUND) {
            JOURNAL_ALERTE(TAG, "lecture de %s impossible (%s) ; valeur par defaut",
                           REGLAGES_CLE_PORT, esp_err_to_name(erreur_port));
        }
    }

    size_t taille_backend = sizeof(g_backend);
    esp_err_t erreur_backend = nvs_get_str(handle, REGLAGES_CLE_BACKEND, g_backend, &taille_backend);
    if (erreur_backend != ESP_OK) {
        strlcpy(g_backend, REGLAGES_BACKEND_DEFAUT, sizeof(g_backend));
        if (erreur_backend != ESP_ERR_NVS_NOT_FOUND) {
            JOURNAL_ALERTE(TAG, "lecture de %s impossible (%s) ; valeur par defaut",
                           REGLAGES_CLE_BACKEND, esp_err_to_name(erreur_backend));
        }
    }

    /* nvs_open() a réussi : fermer le handle sans y toucher davantage. Rien
     * ici n'écrit dans la NVS ni ne l'efface — reglages_charger() ne fait que
     * lire. */
    nvs_close(handle);

    JOURNAL_INFO(TAG, "reglages charges (hote=%s port=%u backend=%s)",
                 g_hote.adresse, (unsigned)g_hote.port, g_backend);
    return ESP_OK;
}

bool reglages_configures(void)
{
    return g_hote.adresse[0] != '\0';
}

bool reglages_hote(backend_hote_t *sortie)
{
    if (sortie != NULL) {
        *sortie = g_hote;
    }
    return reglages_configures();
}

esp_err_t reglages_definir_hote(const backend_hote_t *hote)
{
    if (hote == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(REGLAGES_ESPACE_NOMS, NVS_READWRITE, &handle);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "nvs_open a echoue lors de l'ecriture de l'hote (%s)", esp_err_to_name(erreur));
        return erreur;
    }

    erreur = nvs_set_str(handle, REGLAGES_CLE_ADRESSE, hote->adresse);
    if (erreur == ESP_OK) {
        erreur = nvs_set_u16(handle, REGLAGES_CLE_PORT, hote->port);
    }
    if (erreur == ESP_OK) {
        erreur = nvs_commit(handle);
    }
    nvs_close(handle);

    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "ecriture de l'hote impossible (%s)", esp_err_to_name(erreur));
        return erreur;
    }

    g_hote = *hote;
    JOURNAL_INFO(TAG, "hote enregistre (adresse=%s port=%u)", g_hote.adresse, (unsigned)g_hote.port);
    return ESP_OK;
}

const char *reglages_backend(void)
{
    return g_backend;
}

esp_err_t reglages_definir_backend(const char *nom)
{
    if (nom == NULL || nom[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(nom) >= sizeof(g_backend)) {
        JOURNAL_ALERTE(TAG, "nom de backend trop long (%u octets, max %u)",
                       (unsigned)strlen(nom), (unsigned)sizeof(g_backend) - 1u);
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(REGLAGES_ESPACE_NOMS, NVS_READWRITE, &handle);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "nvs_open a echoue lors de l'ecriture du backend (%s)", esp_err_to_name(erreur));
        return erreur;
    }

    erreur = nvs_set_str(handle, REGLAGES_CLE_BACKEND, nom);
    if (erreur == ESP_OK) {
        erreur = nvs_commit(handle);
    }
    nvs_close(handle);

    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "ecriture du backend impossible (%s)", esp_err_to_name(erreur));
        return erreur;
    }

    strlcpy(g_backend, nom, sizeof(g_backend));
    JOURNAL_INFO(TAG, "backend enregistre (%s)", g_backend);
    return ESP_OK;
}
