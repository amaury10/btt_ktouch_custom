#include "backend_moonraker.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"

#include "etat_klipper.h"
#include "journal.h"
#include "moonraker_parse.h"

/* Étiquette de journalisation : convention reprise du reste du firmware
 * (voir app_main.c, backend_factice.c), pour que /log reste lisible par
 * module. */
static const char *TAG = "backend_moonraker";

/* Chemin de l'interrogation périodique : les cinq sous-objets nécessaires à
 * remplir etat_klipper_t (voir moonraker_parse.c), ni plus ni moins — chaque
 * objet en trop est un aller-retour réseau et un tampon de réponse plus
 * grands sans raison. */
#define MOONRAKER_CHEMIN_INTERROGATION \
    "printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard&webhooks"

/* Tampon de réponse HTTP : STATIQUE et de taille FIXE, jamais alloué à chaque
 * appel. rafraichir() tourne une fois par seconde pendant des heures sur un
 * appareil qu'on ne peut pas rebrancher par câble ; une allocation dans ce
 * chemin est la façon la plus sûre de provoquer un redémarrage nocturne
 * qu'on ne saura pas déboguer (fragmentation du tas au bout de quelques
 * milliers d'appels). 4 Kio couvre largement une réponse
 * /printer/objects/query pour les cinq objets demandés ci-dessus (quelques
 * centaines d'octets en pratique) ; au-delà, la réponse est tronquée et
 * l'appel rend une erreur plutôt que de grandir le tampon. Partagé entre les
 * requêtes GET et POST : les deux ne s'exécutent jamais en même temps, elles
 * passent toutes deux par la même tâche unique (voir core/boucle.c). */
#define MOONRAKER_TAMPON_OCTETS 4096
static char g_tampon_reponse[MOONRAKER_TAMPON_OCTETS];

/* Taille de tampon d'URL : "http://" (7) + adresse (BACKEND_HOTE_LONGUEUR_MAX)
 * + ":" (1) + port (5 chiffres max) + "/" (1) + le plus long chemin utilisé
 * ici (l'interrogation, ~74 octets) + le nul terminal. Marge ronde
 * au-dessus de cette somme plutôt qu'un calcul au plus juste, pour ne pas
 * avoir à revenir ici si le chemin d'interrogation s'allonge d'un objet. */
#define MOONRAKER_URL_OCTETS (BACKEND_HOTE_LONGUEUR_MAX + 128)

/* Délai avant abandon d'une requête HTTP : assez long pour un aller-retour
 * LAN chargé (le Raspberry Pi qui héberge Moonraker peut lui-même être
 * occupé à écrire du G-code sur la carte SD), assez court pour ne jamais
 * bloquer la tâche d'interrogation plus de quelques cycles d'affilée — la
 * boucle appelante (core/boucle.c) ne fait rien d'autre pendant ce temps. */
#define MOONRAKER_DELAI_MS 3000

/* Hôte mémorisé par demarrer(), puisque rafraichir()/commande() ne le
 * reçoivent pas en paramètre (voir le contrat dans backend.h). Un seul
 * backend Moonraker tourne à la fois dans le socle — même hypothèse que
 * backend_factice.c pour son scénario courant — donc une variable statique
 * suffit. */
static backend_hote_t g_hote;
static bool           g_actif = false;

/* Construit "http://<adresse>:<port>/<chemin>" dans `tampon`. `chemin` ne
 * doit pas commencer par '/'. */
static void moonraker_construire_url(char *tampon, size_t taille, const char *chemin)
{
    snprintf(tampon, taille, "http://%s:%u/%s", g_hote.adresse, (unsigned)g_hote.port, chemin);
}

/* Émet la requête GET d'interrogation périodique dans le tampon statique et
 * rend sa longueur utile dans `*longueur_lue`. Ne modifie jamais le tampon
 * fourni par l'appelant : c'est à `moonraker_parse_status` de décider, plus
 * haut, ce qu'il advient de l'état déjà en place. */
static esp_err_t moonraker_get(const char *chemin, size_t *longueur_lue)
{
    char url[MOONRAKER_URL_OCTETS];
    moonraker_construire_url(url, sizeof(url), chemin);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = MOONRAKER_DELAI_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        JOURNAL_ERREUR(TAG, "esp_http_client_init a echoue pour %s", url);
        return ESP_FAIL;
    }

    esp_err_t erreur = esp_http_client_open(client, 0);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "connexion a %s impossible (%s)", url, esp_err_to_name(erreur));
        esp_http_client_cleanup(client);
        return erreur;
    }

    /* content_length peut valoir -1 (reponse en chunked) : on ne s'y fie pas
     * pour dimensionner quoi que ce soit, la lecture ci-dessous s'arrete
     * d'elle-meme a la fin des donnees ou au bord du tampon statique. */
    (void)esp_http_client_fetch_headers(client);

    size_t total = 0;
    while (total < sizeof(g_tampon_reponse) - 1) {
        int lu = esp_http_client_read(client, g_tampon_reponse + total,
                                       sizeof(g_tampon_reponse) - 1 - total);
        if (lu <= 0) {
            break;
        }
        total += (size_t)lu;
    }
    g_tampon_reponse[total] = '\0';

    /* Une reponse qui deborde le tampon statique est tronquee, jamais
     * grandie (voir le commentaire sur MOONRAKER_TAMPON_OCTETS) : on le
     * detecte ici et on rend une erreur explicite plutot que de tenter un
     * cJSON_ParseWithLength() sur un document coupe au milieu, qui
     * echouerait de toute facon mais sans dire pourquoi dans le journal. */
    bool complete = esp_http_client_is_complete_data_received(client);
    int statut = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!complete) {
        JOURNAL_ALERTE(TAG, "reponse de %s tronquee au-dela de %u octets ; ignoree",
                       url, (unsigned)sizeof(g_tampon_reponse) - 1u);
        return ESP_ERR_INVALID_SIZE;
    }
    if (statut != 200) {
        JOURNAL_ALERTE(TAG, "statut HTTP %d sur %s", statut, url);
        return ESP_FAIL;
    }

    *longueur_lue = total;
    return ESP_OK;
}

/* Émet un POST sans corps vers `chemin` (les quatre actions du tableau du
 * brief n'en prennent aucun) et vérifie seulement le code de statut. Le corps
 * de la réponse, s'il y en a un, est vidé dans le même tampon statique que
 * les interrogations puis ignoré. */
static esp_err_t moonraker_post(const char *chemin)
{
    char url[MOONRAKER_URL_OCTETS];
    moonraker_construire_url(url, sizeof(url), chemin);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = MOONRAKER_DELAI_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        JOURNAL_ERREUR(TAG, "esp_http_client_init a echoue pour %s", url);
        return ESP_FAIL;
    }

    /* write_len = 0 : requete sans corps. */
    esp_err_t erreur = esp_http_client_open(client, 0);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "connexion a %s impossible (%s)", url, esp_err_to_name(erreur));
        esp_http_client_cleanup(client);
        return erreur;
    }

    (void)esp_http_client_fetch_headers(client);

    size_t total = 0;
    while (total < sizeof(g_tampon_reponse) - 1) {
        int lu = esp_http_client_read(client, g_tampon_reponse + total,
                                       sizeof(g_tampon_reponse) - 1 - total);
        if (lu <= 0) {
            break;
        }
        total += (size_t)lu;
    }

    int statut = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (statut < 200 || statut >= 300) {
        JOURNAL_ALERTE(TAG, "statut HTTP %d sur %s", statut, url);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t backend_moonraker_demarrer(void *etat, const backend_hote_t *hote)
{
    if (hote == NULL) {
        JOURNAL_ERREUR(TAG, "demarrage sans hote");
        return ESP_ERR_INVALID_ARG;
    }

    /* Remise a zero defensive, comme backend_factice_demarrer() : ce backend
     * ne suppose rien de l'etat qu'on lui tend. */
    memset(etat, 0, sizeof(etat_klipper_t));

    g_hote = *hote;
    g_actif = true;

    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u)", hote->adresse, (unsigned)hote->port);
    return ESP_OK;
}

static esp_err_t backend_moonraker_rafraichir(void *etat)
{
    if (!g_actif) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t longueur = 0;
    esp_err_t erreur = moonraker_get(MOONRAKER_CHEMIN_INTERROGATION, &longueur);
    if (erreur != ESP_OK) {
        return erreur;
    }

    if (!moonraker_parse_status(g_tampon_reponse, longueur, (etat_klipper_t *)etat)) {
        JOURNAL_ALERTE(TAG, "reponse Moonraker inexploitable (result.status absent ou JSON malforme)");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void backend_moonraker_arreter(void *etat)
{
    (void)etat;
    g_actif = false;
    JOURNAL_INFO(TAG, "arret");
}

static esp_err_t backend_moonraker_commande(void *etat, const char *action,
                                             const char *arguments_json)
{
    (void)etat;
    (void)arguments_json; /* aucune des quatre actions ci-dessous ne prend de corps */

    const char *chemin;
    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0) {
        chemin = "printer/print/pause";
    } else if (strcmp(action, BACKEND_ACTION_REPRENDRE) == 0) {
        chemin = "printer/print/resume";
    } else if (strcmp(action, BACKEND_ACTION_ANNULER) == 0) {
        chemin = "printer/print/cancel";
    } else if (strcmp(action, BACKEND_ACTION_URGENCE) == 0) {
        chemin = "printer/emergency_stop";
    } else {
        /* Une action inconnue doit echouer fort et explicitement, pour que
         * l'interface puisse griser un bouton en connaissant la raison —
         * jamais l'ignorer en silence (meme regle que backend_factice.c). */
        JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
        return ESP_ERR_NOT_SUPPORTED;
    }

    JOURNAL_INFO(TAG, "commande %s -> POST /%s", action, chemin);
    return moonraker_post(chemin);
}

static const backend_desc_t g_backend_moonraker_desc = {
    .nom = "moonraker",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = backend_moonraker_demarrer,
    .rafraichir = backend_moonraker_rafraichir,
    .arreter = backend_moonraker_arreter,
    .commande = backend_moonraker_commande,
};

const backend_desc_t *backend_moonraker_desc(void)
{
    return &g_backend_moonraker_desc;
}
