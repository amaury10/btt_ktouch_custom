/* Implémentation : voir moonraker_ws.h pour le contrat complet.
 *
 * --- Corrélateur : comment commande() attend sans jamais bloquer la tâche
 * WS -----------------------------------------------------------------------
 *
 * `moonraker_ws_commande()` est appelée depuis `boucle_traiter_commandes()`
 * (core/boucle.c), qui tourne dans la tâche d'INTERROGATION (`boucle_klipper`,
 * celle qui appelait déjà les POST HTTP bloquants du 2a) -- PAS dans la
 * tâche WS elle-même (celle d'esp_websocket_client, créée en interne par
 * `esp_websocket_client_start()`). Un seul corrélateur "un coup à la fois"
 * suffit : `boucle_traiter_commandes()` dépile et exécute les commandes de
 * sa file UNE PAR UNE, jamais deux `commande()` en vol simultanément (voir
 * son commentaire dans boucle.c).
 *
 * Le mécanisme : `moonraker_ws_commande()` génère un id, construit la
 * requête (`rpc_construire_requete()`, fonction pure de la tâche 3), l'envoie
 * via `esp_websocket_client_send_text()`, POSE le corrélateur (id attendu,
 * `repondu=false`) sous verrou, puis SONDE ce même corrélateur par petites
 * tranches (`vTaskDelay(MOONRAKER_WS_SONDAGE_MS)`) jusqu'à `repondu==true`
 * ou l'expiration de `timeout_ms` -- c'est CETTE tâche (boucle_klipper) qui
 * dort dans la boucle de sondage, jamais la tâche WS.
 *
 * Côté tâche WS : quand une réponse JSON-RPC corrélée arrive
 * (`RPC_MSG_REPONSE`, voir `traiter_message_complet()` plus bas), le
 * gestionnaire d'événement écrit le résultat dans le corrélateur (sous le
 * même verrou) et REND LA MAIN IMMÉDIATEMENT -- il ne sonde rien, n'attend
 * rien, ne bloque jamais sur quoi que ce soit venant de `boucle_klipper`.
 * Les deux tâches ne se synchronisent donc QUE par ce verrou (une exclusion
 * mutuelle très courte à chaque accès, jamais une attente longue tenue par
 * l'une pour l'autre) plutôt que par une primitive de signalisation
 * (sémaphore binaire) qui aurait été tout aussi valide mais n'apporte rien
 * ici : le sondage à `MOONRAKER_WS_SONDAGE_MS` (20 ms) est largement en
 * dessous de toute latence perceptible pour une commande utilisateur
 * (pause/reprise/annulation/arrêt d'urgence), et évite un objet FreeRTOS de
 * plus à créer/détruire avec le cycle de vie du backend. */
#include "moonraker_ws.h"

#include <string.h>

#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_transport_ws.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "journal.h"
#include "moonraker_boite.h"
#include "moonraker_rpc.h"

static const char *TAG = "moonraker_ws";

/* Tampon de réassemblage des trames fragmentées : STATIQUE et de taille
 * FIXE, même politique que MOONRAKER_TAMPON_OCTETS dans backend_moonraker.c
 * (jamais d'allocation dans un chemin qui tourne en continu pendant des
 * heures). 4 Kio couvre largement un `notify_status_update` ou une réponse
 * corrélée -- au-delà, le message est ABANDONNÉ (jamais un parse partiel,
 * jamais un débordement du tampon lui-même) et journalisé, throttlé. */
#define MOONRAKER_WS_TAMPON_OCTETS 4096
static char   g_tampon_msg[MOONRAKER_WS_TAMPON_OCTETS];
static size_t g_tampon_len   = 0;
static bool   g_tampon_texte = false; /* type de la trame en cours de reassemblage (fixe des le premier fragment) */
static bool   g_tampon_deborde = false;

/* Tampon d'émission (identify, abonnement, commandes) : RPC_ABONNEMENT_TAILLE_MIN
 * (384, voir moonraker_rpc.h) est le besoin mesuré le plus grand des
 * requêtes que ce fichier construit ; marge ronde au-dessus. Sur la pile de
 * la tâche appelante (WS ou boucle_klipper selon le site), jamais statique
 * -- contrairement au tampon de réception ci-dessus, ce tampon n'a pas
 * besoin de survivre entre deux appels. */
#define MOONRAKER_WS_REQUETE_OCTETS 512

/* Délai de l'opération d'envoi elle-même (mise en file d'attente par
 * esp_websocket_client, PAS le temps d'attendre une réponse -- voir
 * MOONRAKER_WS_TIMEOUT_COMMANDE_DEFAUT_MS plus bas pour ça). */
#define MOONRAKER_WS_ENVOI_DELAI_MS 2000u

/* Granularité du sondage du corrélateur par moonraker_ws_commande() -- voir
 * le commentaire de tête. Assez court pour qu'un bouton utilisateur reste
 * réactif, assez long pour ne pas saturer inutilement le CPU en boucle. */
#define MOONRAKER_WS_SONDAGE_MS 20u

/* Backoff de reconnexion (spec §4, critère 2) : 1 s -> 2 -> 4 -> ... plafonné
 * 30 s, ré-identify + ré-abonnement à CHAQUE retour (jamais seulement le
 * premier). */
#define MOONRAKER_WS_BACKOFF_INITIAL_MS 1000u
#define MOONRAKER_WS_BACKOFF_MAX_MS     30000u

/* Cadencement du journal de reconnexion (même principe que
 * moonraker_journal_echec_pret() dans backend_moonraker.c, dont l'historique
 * documente déjà un déluge de journal comme défaut réel de ce genre de
 * chemin -- voir le commentaire de tête de ce fichier-là) : la toute
 * première reconnexion est journalisée, puis au plus une ligne par minute
 * tant que les tentatives se succèdent, pour ne jamais noyer /log (16 Kio,
 * seul canal de diagnostic d'un appareil sans port série) sous une ligne
 * par tentative pendant une panne prolongée. */
#define MOONRAKER_WS_JOURNAL_INTERVALLE_US (60LL * 1000 * 1000)

/* --- État process-wide (singleton de fichier, voir le commentaire de tête
 * de moonraker_ws.h -- un seul client WS à la fois dans le socle). --- */

static esp_websocket_client_handle_t g_client = NULL;
static SemaphoreHandle_t             g_verrou = NULL;
static esp_timer_handle_t            g_minuterie_reconnexion = NULL;

static backend_hote_t g_hote;
static moonraker_boite_t g_boite;

static volatile bool g_connecte     = false;
static volatile bool g_klippy_pret  = true;

static uint32_t g_backoff_ms             = MOONRAKER_WS_BACKOFF_INITIAL_MS;
static bool     g_reconnexion_armee      = false;
static uint32_t g_compteur_reconnexions  = 0;
static int64_t  g_dernier_journal_reco_us = 0;

/* Générateur d'id JSON-RPC : UN SEUL compteur pour tout ce module (identify,
 * abonnement, commandes) -- jamais deux compteurs qui pourraient produire un
 * id en double, ce qui romprait la corrélation. Accès protégé par g_verrou
 * (prochain_id() est appelée à la fois depuis la tâche WS -- identify/
 * abonnement au connect -- et depuis boucle_klipper -- commande()). */
static uint32_t g_id_suivant = 1;

/* Id de la requête d'abonnement EN COURS -- mis à jour à chaque (re)connexion
 * dans envoyer_identify_et_abonnement(), lu uniquement depuis la tâche WS
 * (le seul contexte qui reçoit des réponses corrélées) : aucun verrou
 * nécessaire, jamais touché depuis une autre tâche. */
static uint32_t g_id_abonnement = 0;

/* Corrélateur "un coup à la fois" pour moonraker_ws_commande() -- voir le
 * commentaire de tête pour le mécanisme complet. Protégé par g_verrou. */
typedef struct {
    bool     en_cours;
    uint32_t id;
    bool     repondu;
    bool     succes;
    char     erreur_texte[128];
} correlateur_t;

static correlateur_t g_correlateur;

/* ------------------------------------------------------------------------
 * Petits utilitaires internes
 * ------------------------------------------------------------------------ */

static uint32_t prochain_id(void)
{
    xSemaphoreTake(g_verrou, portMAX_DELAY);
    uint32_t id = g_id_suivant++;
    if (g_id_suivant == 0) {
        g_id_suivant = 1; /* jamais l'id 0 : reserve pour "aucune reponse" */
    }
    xSemaphoreGive(g_verrou);
    return id;
}

/* "http://<adresse>:<port>/..." devient "ws://..." -- même détection
 * IPv6 par ':' que moonraker_construire_url() dans backend_moonraker.c
 * (voir son commentaire de tête pour la justification RFC 3986 §3.2.2 :
 * hote_parse.c stocke déjà l'adresse SANS crochets). */
static void construire_url(char *tampon, size_t taille, const backend_hote_t *hote)
{
    if (strchr(hote->adresse, ':') != NULL) {
        snprintf(tampon, taille, "ws://[%s]:%u/websocket", hote->adresse, (unsigned)hote->port);
    } else {
        snprintf(tampon, taille, "ws://%s:%u/websocket", hote->adresse, (unsigned)hote->port);
    }
}

static void journaliser_debordement(void)
{
    /* Un seul événement notable par débordement réel (pas de sous-throttle
     * supplémentaire ici) : contrairement aux échecs réseau répétitifs de
     * backend_moonraker.c, un message qui dépasse 4 Kio est rarissime en
     * pratique (voir son propre commentaire sur ce même budget) -- s'il
     * devait se reproduire en rafale, ce serait le symptôme d'un problème
     * bien plus grave que ce que throttler masquerait utilement. */
    JOURNAL_ALERTE(TAG, "message WS au-dela de %u octets ; ignore",
                   (unsigned)sizeof(g_tampon_msg) - 1u);
}

static void journaliser_reconnexion(void)
{
    int64_t maintenant = esp_timer_get_time();
    bool premier = (g_compteur_reconnexions == 1);
    bool intervalle_ecoule = (maintenant - g_dernier_journal_reco_us) >= MOONRAKER_WS_JOURNAL_INTERVALLE_US;
    if (premier || intervalle_ecoule) {
        g_dernier_journal_reco_us = maintenant;
        JOURNAL_ALERTE(TAG, "reconnexion WS #%u (prochain backoff %u ms)",
                       (unsigned)g_compteur_reconnexions, (unsigned)g_backoff_ms);
    }
}

static uint32_t backoff_suivant(uint32_t actuel_ms)
{
    uint64_t double_ms = (uint64_t)actuel_ms * 2u;
    return double_ms > MOONRAKER_WS_BACKOFF_MAX_MS ? MOONRAKER_WS_BACKOFF_MAX_MS : (uint32_t)double_ms;
}

/* Payload fixe de server.connection.identify -- une chaîne JSON constante
 * suffit (comme PARAMS dans rpc_construire_abonnement(), voir moonraker_rpc.c) :
 * rien ici ne dépend d'une donnée d'entrée. "type":"other" et l'URL du
 * projet suivent la convention déjà enregistrée dans les fixtures réelles
 * de la tâche 4 (host-test/fixtures/moonraker, fichiers .jsonl). */
static const char *const MOONRAKER_WS_IDENTIFY_PARAMS =
    "{\"client_name\":\"ktouch\",\"version\":\"0.1\",\"type\":\"other\","
    "\"url\":\"https://github.com/bigtreetech/K-Touch\"}";

/* À WEBSOCKET_EVENT_CONNECTED : identify PUIS abonnement (critère 2 -- à
 * CHAQUE reconnexion, jamais seulement la première). Appelée depuis la
 * tâche WS elle-même (le gestionnaire d'événement) : g_id_abonnement n'a
 * donc besoin d'aucun verrou (voir son commentaire de déclaration). */
static void envoyer_identify_et_abonnement(void)
{
    char tampon[MOONRAKER_WS_REQUETE_OCTETS];

    uint32_t id_identify = prochain_id();
    if (rpc_construire_requete(tampon, sizeof(tampon), id_identify, "server.connection.identify",
                                MOONRAKER_WS_IDENTIFY_PARAMS)) {
        esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                        pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
    } else {
        JOURNAL_ERREUR(TAG, "construction de server.connection.identify impossible");
    }

    uint32_t id_abonnement = prochain_id();
    if (rpc_construire_abonnement(tampon, sizeof(tampon), id_abonnement)) {
        g_id_abonnement = id_abonnement;
        esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                        pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
    } else {
        JOURNAL_ERREUR(TAG, "construction de l'abonnement impossible");
    }
}

/* Traite un message JSON-RPC COMPLET (déjà réassemblé) -- appelée SOUS
 * g_verrou (voir traiter_data() ci-dessous, seul appelant). */
static void traiter_message_complet(const char *json, size_t longueur)
{
    uint32_t id = 0;
    rpc_message_type_t type = rpc_classifier(json, longueur, &id);

    switch (type) {
    case RPC_MSG_STATUS_UPDATE: {
        /* "copie de l'etat courant de la boite, rpc_fusionner_status,
         * boite_deposer" (brief) : g_boite.etat retient TOUJOURS son
         * dernier depot, meme deja draine (voir moonraker_boite.h -- le
         * drain ne remet jamais `etat` a zero, seulement `neuf`) -- c'est
         * exactement la base cumulative sur laquelle une mise a jour
         * PARTIELLE doit s'appliquer. */
        etat_klipper_t copie = g_boite.etat;
        if (rpc_fusionner_status(&copie, json, longueur)) {
            boite_deposer(&g_boite, &copie);
        }
        break;
    }
    case RPC_MSG_REPONSE:
        if (id == g_id_abonnement) {
            /* L'instantane initial de la reponse a l'abonnement -- meme
             * base cumulative que ci-dessus. */
            etat_klipper_t copie = g_boite.etat;
            if (rpc_fusionner_instantane(&copie, json, longueur)) {
                boite_deposer(&g_boite, &copie);
            }
        } else if (g_correlateur.en_cours && id == g_correlateur.id) {
            bool succes = false;
            char erreur[sizeof(g_correlateur.erreur_texte)];
            memset(erreur, 0, sizeof(erreur));
            if (rpc_lire_reponse(json, longueur, &succes, erreur, sizeof(erreur))) {
                g_correlateur.succes = succes;
                strlcpy(g_correlateur.erreur_texte, erreur, sizeof(g_correlateur.erreur_texte));
                g_correlateur.repondu = true;
            }
            /* rpc_lire_reponse() rend false (JSON illisible, ou ni "result"
             * ni une erreur exploitable) : g_correlateur.repondu reste
             * false a dessein -- moonraker_ws_commande() verra un timeout
             * plutot qu'un succes ou un echec invente. */
        }
        /* Reponse a l'identify (id == celui genere juste avant l'abonnement
         * dans envoyer_identify_et_abonnement()) : rien a en extraire,
         * ignoree silencieusement -- Moonraker ne renvoie qu'un accuse de
         * reception sans information utile a ce module. */
        break;
    case RPC_MSG_KLIPPY_READY:
        g_klippy_pret = true;
        break;
    case RPC_MSG_KLIPPY_DECONNECTE:
        g_klippy_pret = false;
        break;
    case RPC_MSG_AUTRE:
    case RPC_MSG_INVALIDE:
    default:
        /* RPC_MSG_AUTRE couvre notamment notify_gcode_response, le canal
         * par lequel Klipper signale l'echec REEL d'une macro inconnue
         * (voir le commentaire de tete de moonraker_rpc.h) -- son
         * interpretation est une decision de la tache 6 (ecran macros),
         * volontairement PAS prise ici. */
        break;
    }
}

/* Réassemble les trames fragmentées dans le tampon statique borné, puis
 * dispatch le message complet sous verrou. `data->payload_offset`/
 * `data->payload_len`/`data->data_len` sont les champs qu'esp_websocket_client
 * fournit précisément pour ça : un message qui dépasse la taille du tampon
 * interne de la bibliothèque (`buffer_size`, indépendant de notre propre
 * tampon ici) arrive en PLUSIEURS événements WEBSOCKET_EVENT_DATA, chacun
 * portant un `data_ptr`/`data_len` pour SA part, `payload_offset` pour la
 * position de cette part dans le message logique complet, et `payload_len`
 * pour la taille TOTALE de ce message logique (constante sur tous les
 * événements d'un même message). */
static void traiter_data(const esp_websocket_event_data_t *data)
{
    if (data->payload_offset == 0) {
        /* Premier (ou seul) fragment d'un nouveau message logique : reinitialise
         * le tampon de reassemblage, memorise le type de trame -- Moonraker ne
         * pousse jamais que du JSON en trame TEXTE ; une trame binaire ou de
         * controle (ping/pong/close, deja geree en interne par la bibliotheque)
         * n'a rien a faire ici. */
        g_tampon_len = 0;
        g_tampon_deborde = false;
        g_tampon_texte = (data->op_code == WS_TRANSPORT_OPCODES_TEXT);
    }

    if (!g_tampon_texte || data->data_len <= 0) {
        return;
    }
    if (g_tampon_deborde) {
        /* Message deja abandonne : on continue d'ignorer ses fragments
         * suivants sans les recopier, jusqu'a la resynchronisation naturelle
         * au prochain payload_offset==0. */
        return;
    }

    size_t total_annonce = (size_t)data->payload_len;
    if (total_annonce >= sizeof(g_tampon_msg) ||
        g_tampon_len + (size_t)data->data_len >= sizeof(g_tampon_msg)) {
        /* Depassement : message ENTIER abandonne, jamais un parse partiel
         * (meme politique que le tampon HTTP de backend_moonraker.c). */
        g_tampon_deborde = true;
        journaliser_debordement();
        return;
    }

    memcpy(g_tampon_msg + g_tampon_len, data->data_ptr, (size_t)data->data_len);
    g_tampon_len += (size_t)data->data_len;

    if (g_tampon_len >= total_annonce) {
        /* Message complet : dispatch sous verrou (traiter_message_complet()
         * touche g_boite/g_correlateur, partages avec boucle_klipper). */
        g_tampon_msg[g_tampon_len] = '\0';
        xSemaphoreTake(g_verrou, portMAX_DELAY);
        traiter_message_complet(g_tampon_msg, g_tampon_len);
        xSemaphoreGive(g_verrou);
        g_tampon_len = 0;
    }
}

/* Callback de la minuterie de reconnexion (esp_timer, tourne dans la tache
 * esp_timer, PAS la tache WS -- sans consequence ici, ce callback ne touche
 * que l'etat de reconnexion et esp_websocket_client_start(), jamais
 * etat_store_*, boucle_*, ni LVGL). */
static void minuterie_reconnexion_cb(void *arg)
{
    (void)arg;
    g_reconnexion_armee = false;
    g_compteur_reconnexions++;
    journaliser_reconnexion();
    /* Avance le backoff pour la PROCHAINE tentative avant meme de savoir si
     * celle-ci reussit : si elle reussit, WEBSOCKET_EVENT_CONNECTED le
     * remettra a l'initial de toute facon (voir plus bas) ; si elle echoue
     * encore, la prochaine armée par programmer_reconnexion() utilisera
     * deja la valeur doublee -- c'est la progression exponentielle attendue
     * (1 s -> 2 -> 4 -> ... plafonnee 30 s). */
    g_backoff_ms = backoff_suivant(g_backoff_ms);

    esp_err_t erreur = esp_websocket_client_start(g_client);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "relance du client WS impossible (%s) ; nouvelle tentative programmee",
                       esp_err_to_name(erreur));
        g_reconnexion_armee = true;
        esp_timer_start_once(g_minuterie_reconnexion, (uint64_t)g_backoff_ms * 1000ULL);
    }
}

static void programmer_reconnexion(void)
{
    g_connecte = false;
    if (g_reconnexion_armee) {
        return; /* deja une tentative en attente ; ne pas la redoubler */
    }
    g_reconnexion_armee = true;
    esp_timer_start_once(g_minuterie_reconnexion, (uint64_t)g_backoff_ms * 1000ULL);
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        g_connecte = true;
        /* Optimiste jusqu'a preuve du contraire (voir le commentaire de
         * moonraker_ws_klippy_pret() dans moonraker_ws.h) : une identification
         * et un abonnement tout neufs viennent d'etre envoyes, rien n'a
         * encore dit que Klippy est down. */
        g_klippy_pret = true;
        /* Reussite : le prochain backoff, si une deconnexion survient plus
         * tard, repart de l'initial -- jamais cumule d'une panne a l'autre. */
        g_backoff_ms = MOONRAKER_WS_BACKOFF_INITIAL_MS;
        g_tampon_len = 0;
        g_tampon_deborde = false;
        JOURNAL_INFO(TAG, "connecte ; identification + abonnement");
        envoyer_identify_et_abonnement();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
    case WEBSOCKET_EVENT_CLOSED:
        programmer_reconnexion();
        break;
    case WEBSOCKET_EVENT_DATA:
        traiter_data((const esp_websocket_event_data_t *)event_data);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * API publique -- voir moonraker_ws.h
 * ------------------------------------------------------------------------ */

esp_err_t moonraker_ws_demarrer(const backend_hote_t *hote)
{
    if (hote == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_verrou == NULL) {
        g_verrou = xSemaphoreCreateMutex();
        if (g_verrou == NULL) {
            JOURNAL_ERREUR(TAG, "xSemaphoreCreateMutex a echoue");
            return ESP_ERR_NO_MEM;
        }
    }
    if (g_minuterie_reconnexion == NULL) {
        const esp_timer_create_args_t args = {
            .callback = minuterie_reconnexion_cb,
            .name = "moonraker_ws_reco",
        };
        if (esp_timer_create(&args, &g_minuterie_reconnexion) != ESP_OK) {
            JOURNAL_ERREUR(TAG, "esp_timer_create a echoue");
            return ESP_ERR_NO_MEM;
        }
    }

    /* Au cas ou demarrer() serait rappele sans arreter() intermediaire (meme
     * garde defensive que backend_moonraker_demarrer()) : ne pas fuir un
     * client deja cree. */
    if (g_client != NULL) {
        esp_websocket_client_stop(g_client);
        esp_websocket_client_destroy(g_client);
        g_client = NULL;
    }

    g_hote = *hote;
    memset(&g_boite, 0, sizeof(g_boite));
    memset(&g_correlateur, 0, sizeof(g_correlateur));
    g_connecte = false;
    g_klippy_pret = true;
    g_backoff_ms = MOONRAKER_WS_BACKOFF_INITIAL_MS;
    g_reconnexion_armee = false;
    g_compteur_reconnexions = 0;
    g_dernier_journal_reco_us = 0;
    g_id_abonnement = 0;
    g_id_suivant = 1;
    g_tampon_len = 0;
    g_tampon_deborde = false;

    char url[BACKEND_HOTE_LONGUEUR_MAX + 32];
    construire_url(url, sizeof(url), &g_hote);

    esp_websocket_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.uri = url;
    config.disable_auto_reconnect = true; /* backoff exponentiel gere par CE fichier, pas la bibliotheque */

    g_client = esp_websocket_client_init(&config);
    if (g_client == NULL) {
        JOURNAL_ERREUR(TAG, "esp_websocket_client_init a echoue");
        return ESP_FAIL;
    }
    esp_websocket_register_events(g_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);

    esp_err_t erreur = esp_websocket_client_start(g_client);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "esp_websocket_client_start a echoue (%s)", esp_err_to_name(erreur));
        esp_websocket_client_destroy(g_client);
        g_client = NULL;
        return erreur;
    }

    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u url=%s)", hote->adresse, (unsigned)hote->port, url);
    return ESP_OK;
}

void moonraker_ws_arreter(void)
{
    if (g_minuterie_reconnexion != NULL) {
        esp_timer_stop(g_minuterie_reconnexion); /* sans effet si deja arretee */
    }
    g_reconnexion_armee = false;
    if (g_client != NULL) {
        esp_websocket_client_stop(g_client);
        esp_websocket_client_destroy(g_client);
        g_client = NULL;
    }
    g_connecte = false;
    JOURNAL_INFO(TAG, "arret");
}

bool moonraker_ws_en_ligne(void)
{
    return g_connecte;
}

bool moonraker_ws_klippy_pret(void)
{
    return g_klippy_pret;
}

bool moonraker_ws_drainer(etat_klipper_t *sortie)
{
    if (sortie == NULL || g_verrou == NULL) {
        return false;
    }
    xSemaphoreTake(g_verrou, portMAX_DELAY);
    bool r = boite_drainer(&g_boite, sortie);
    xSemaphoreGive(g_verrou);
    return r;
}

esp_err_t moonraker_ws_commande(const char *methode, const char *params_json,
                                 uint32_t timeout_ms, bool *succes,
                                 char *erreur_texte, size_t taille_erreur)
{
    if (g_client == NULL || !g_connecte) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Fix round 1 (revue tache 5, C1 CRITIQUE) : l'id doit etre genere AVANT
     * de prendre g_verrou ci-dessous -- prochain_id() prend CE MEME verrou
     * en interne (voir sa declaration), et xSemaphoreCreateMutex() cree un
     * mutex NON recursif : un appel a prochain_id() alors que ce site tient
     * deja g_verrou bloquait cette tache indefiniment sur son propre verrou
     * (auto-interblocage, aucune assertion FreeRTOS ne le detecte sur ce
     * chemin). Rayon d'effet du bug avant ce fix : boucle_klipper ne
     * revenait plus jamais de la premiere commande WS (pause/reprendre/
     * annuler/ARRET D'URGENCE compris), plus aucun rafraichissement ni
     * liaison_echec() ; la tache WS elle-meme se bloquait a son tour sur ce
     * meme verrou des le message suivant (traiter_data()) -- ecran fige,
     * aucun repli (le WS reste "en ligne" du point de vue de
     * moonraker_ws_en_ligne()), redemarrage materiel requis. Verifie apres
     * correctif : chaque paire xSemaphoreTake(g_verrou)/xSemaphoreGive(g_verrou)
     * de ce fichier relue une a une (liste complete dans le rapport de
     * tache) -- prochain_id() est desormais TOUJOURS appelee hors de toute
     * section critique tenue par ce meme fichier ; c'etait la SEULE
     * occurrence d'un tel appel imbrique. */
    uint32_t id = prochain_id();

    xSemaphoreTake(g_verrou, portMAX_DELAY);
    if (g_correlateur.en_cours) {
        xSemaphoreGive(g_verrou);
        /* Ne devrait jamais arriver : voir le commentaire de tete sur
         * l'hypothese "une seule commande a la fois" -- garde defensive
         * plutot qu'un chemin normal. L'id genere ci-dessus est simplement
         * perdu (jamais consomme) : sans consequence, rien n'exige que les
         * id soient contigus. */
        JOURNAL_ALERTE(TAG, "commande WS deja en cours ; %s refusee", methode);
        return ESP_ERR_INVALID_STATE;
    }

    char tampon[MOONRAKER_WS_REQUETE_OCTETS];
    if (!rpc_construire_requete(tampon, sizeof(tampon), id, methode, params_json)) {
        xSemaphoreGive(g_verrou);
        JOURNAL_ERREUR(TAG, "construction de la requete %s impossible", methode);
        return ESP_FAIL;
    }
    g_correlateur.id = id;
    g_correlateur.en_cours = true;
    g_correlateur.repondu = false;
    g_correlateur.succes = false;
    g_correlateur.erreur_texte[0] = '\0';
    xSemaphoreGive(g_verrou);

    int envoye = esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                                 pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
    if (envoye < 0) {
        xSemaphoreTake(g_verrou, portMAX_DELAY);
        g_correlateur.en_cours = false;
        xSemaphoreGive(g_verrou);
        JOURNAL_ALERTE(TAG, "envoi WS de %s echoue", methode);
        return ESP_FAIL;
    }

    /* Sondage borne -- voir le commentaire de tete de ce fichier pour le
     * detail complet du mecanisme (jamais un blocage de la tache WS). */
    int64_t debut_us = esp_timer_get_time();
    for (;;) {
        xSemaphoreTake(g_verrou, portMAX_DELAY);
        bool pret = g_correlateur.repondu && g_correlateur.id == id;
        if (pret) {
            if (succes != NULL) {
                *succes = g_correlateur.succes;
            }
            if (erreur_texte != NULL && taille_erreur > 0) {
                strlcpy(erreur_texte, g_correlateur.erreur_texte, taille_erreur);
            }
            g_correlateur.en_cours = false;
            xSemaphoreGive(g_verrou);
            return ESP_OK;
        }
        xSemaphoreGive(g_verrou);

        if (((esp_timer_get_time() - debut_us) / 1000) >= timeout_ms) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(MOONRAKER_WS_SONDAGE_MS));
    }

    xSemaphoreTake(g_verrou, portMAX_DELAY);
    g_correlateur.en_cours = false;
    xSemaphoreGive(g_verrou);
    JOURNAL_ALERTE(TAG, "commande WS %s : timeout apres %u ms", methode, (unsigned)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

uint32_t moonraker_ws_compteur_reconnexions(void)
{
    return g_compteur_reconnexions;
}
