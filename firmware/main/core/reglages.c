#include "reglages.h"

#include <string.h>

#include "hote_parse.h"
#include "journal.h"
#include "nvs.h"

/* Étiquette de journalisation : convention reprise du reste du firmware (voir
 * app_main.c, backend_factice.c), pour que /log reste lisible par module. */
static const char *TAG = "reglages";

/* Clé courante : adresse et port regroupés dans une seule chaîne
 * "adresse:port" (voir reglages_definir_hote() pour la raison). */
#define REGLAGES_CLE_HOTE "hote"

/* Anciennes clés, écrites par une version antérieure de ce fichier qui
 * stockait adresse et port séparément. On ne les écrit plus jamais, mais on
 * les lit encore une fois en secours si REGLAGES_CLE_HOTE est absente, pour
 * ne pas réinitialiser silencieusement un appareil déjà configuré par un
 * firmware intermédiaire. */
#define REGLAGES_CLE_ADRESSE_HERITEE "hote_adresse"
#define REGLAGES_CLE_PORT_HERITEE    "hote_port"

#define REGLAGES_CLE_BACKEND "backend"

/* Alias sur la constante de hote_parse.h : une seule source de vérité pour le
 * port par défaut, partagée entre l'analyse pure (hote_parse.c) et le reste
 * de ce fichier (cache initial, migration depuis les anciennes clés). */
#define REGLAGES_PORT_DEFAUT    HOTE_PARSE_PORT_DEFAUT
#define REGLAGES_BACKEND_DEFAUT "moonraker"

/* Longueur maximale d'un nom de backend stocké ("moonraker", "factice", et
 * ceux à venir) : large marge au-delà des noms connus, pour ne pas avoir à
 * revenir ici à chaque backend ajouté. */
#define REGLAGES_BACKEND_LONGUEUR_MAX 32

/* Longueur maximale de la chaîne combinée "adresse:port" : la partie adresse
 * (BACKEND_HOTE_LONGUEUR_MAX octets, nul compris), un ':', jusqu'à 5 chiffres
 * de port ("65535"), et le nul terminal de la chaîne combinée elle-même. */
#define REGLAGES_HOTE_CHAINE_MAX (BACKEND_HOTE_LONGUEUR_MAX + 1 + 5 + 1)

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

/* Secours de migration : relit les deux anciennes clés séparées quand
 * REGLAGES_CLE_HOTE est absente. `handle` est déjà ouvert par l'appelant. */
static void reglages_charger_ancien_format(nvs_handle_t handle)
{
    size_t taille_adresse = sizeof(g_hote.adresse);
    esp_err_t erreur_adresse =
        nvs_get_str(handle, REGLAGES_CLE_ADRESSE_HERITEE, g_hote.adresse, &taille_adresse);
    if (erreur_adresse != ESP_OK) {
        g_hote.adresse[0] = '\0';
        if (erreur_adresse != ESP_ERR_NVS_NOT_FOUND) {
            JOURNAL_ALERTE(TAG, "lecture de %s impossible (%s) ; valeur par defaut",
                           REGLAGES_CLE_ADRESSE_HERITEE, esp_err_to_name(erreur_adresse));
        }
    }

    uint16_t port = REGLAGES_PORT_DEFAUT;
    esp_err_t erreur_port = nvs_get_u16(handle, REGLAGES_CLE_PORT_HERITEE, &port);
    if (erreur_port == ESP_OK) {
        g_hote.port = port;
    } else {
        g_hote.port = REGLAGES_PORT_DEFAUT;
        if (erreur_port != ESP_ERR_NVS_NOT_FOUND) {
            JOURNAL_ALERTE(TAG, "lecture de %s impossible (%s) ; valeur par defaut",
                           REGLAGES_CLE_PORT_HERITEE, esp_err_to_name(erreur_port));
        }
    }
}

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

    char tampon_hote[REGLAGES_HOTE_CHAINE_MAX];
    size_t taille_hote = sizeof(tampon_hote);
    esp_err_t erreur_hote = nvs_get_str(handle, REGLAGES_CLE_HOTE, tampon_hote, &taille_hote);
    if (erreur_hote == ESP_OK) {
        if (!hote_parse(tampon_hote, &g_hote)) {
            /* hote_parse() a quand meme rempli g_hote de facon deterministe
             * (voir hote_parse.h) ; on journalise seulement que la chaine
             * stockee ne decrit pas un hote exploitable, sans dupliquer ici
             * le detail du pourquoi. */
            JOURNAL_ALERTE(TAG, "cle %s inexploitable (%s) ; hote par defaut",
                           REGLAGES_CLE_HOTE, tampon_hote);
        }
    } else if (erreur_hote == ESP_ERR_NVS_NOT_FOUND) {
        /* Migration : un appareil déjà configuré par une version antérieure
         * (avant le regroupement en une seule clé) a ses réglages dans les
         * deux anciennes clés séparées — les relire évite de le réinitialiser
         * silencieusement au premier démarrage sur ce firmware. */
        reglages_charger_ancien_format(handle);
    } else {
        JOURNAL_ALERTE(TAG, "lecture de %s impossible (%s) ; hote par defaut",
                       REGLAGES_CLE_HOTE, esp_err_to_name(erreur_hote));
        g_hote.adresse[0] = '\0';
        g_hote.port = REGLAGES_PORT_DEFAUT;
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

    /* Miroir de la validation appliquée à la lecture (hote_parse.c rejette un
     * port de 0 et retombe sur REGLAGES_PORT_DEFAUT) : sans cette garde, un
     * appelant qui écrit le port 0 verrait son adresse acceptée telle quelle
     * mais son port silencieusement remplacé par la valeur par défaut au
     * prochain reglages_charger(), sans qu'aucune erreur ne le signale à ce
     * moment-là. Ce qui est écrit doit rester ce qui est relu. */
    if (hote->port == 0) {
        JOURNAL_ALERTE(TAG, "port hote nul ; ecriture refusee");
        return ESP_ERR_INVALID_ARG;
    }

    /* hote->adresse doit être terminée par un octet nul dans les
     * sizeof(hote->adresse) octets du tampon fourni par l'appelant : sans
     * cette garde, le snprintf() ci-dessous lirait au-delà de ce tampon.
     * Aucun appelant actuel ne viole ce contrat (tous construisent l'adresse
     * via snprintf/strlcpy), mais la garde protège le premier qui le fera. */
    if (strnlen(hote->adresse, sizeof(hote->adresse)) >= sizeof(hote->adresse)) {
        JOURNAL_ALERTE(TAG, "adresse hote non terminee par un octet nul ; ecriture refusee");
        return ESP_ERR_INVALID_ARG;
    }

    /* Adresse et port sont sérialisés dans une seule chaîne et écrits sous
     * une seule clé NVS. NVS rend chaque nvs_set_*() durable individuellement
     * : nvs_commit() n'est PAS une frontière transactionnelle qui engloberait
     * plusieurs clés. Avec deux clés séparées, une écriture interrompue entre
     * les deux (usure flash, NVS pleine, coupure d'alimentation) laisserait
     * une adresse neuve associée à un port jamais saisi — un hote qui se
     * relit "configuré" sans jamais avoir existé tel quel. Une clé unique
     * rend l'écriture atomique au niveau NVS : soit l'ancienne valeur
     * complète, soit la nouvelle, jamais un mélange des deux. */
    char tampon[REGLAGES_HOTE_CHAINE_MAX];
    int longueur = snprintf(tampon, sizeof(tampon), "%s:%u", hote->adresse, (unsigned)hote->port);
    if (longueur < 0 || (size_t)longueur >= sizeof(tampon)) {
        JOURNAL_ERREUR(TAG, "hote trop long pour etre serialise ; ecriture refusee");
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(REGLAGES_ESPACE_NOMS, NVS_READWRITE, &handle);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "nvs_open a echoue lors de l'ecriture de l'hote (%s)", esp_err_to_name(erreur));
        return erreur;
    }

    erreur = nvs_set_str(handle, REGLAGES_CLE_HOTE, tampon);
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
