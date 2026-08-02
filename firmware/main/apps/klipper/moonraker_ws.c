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
/* 16 Ko : l'etat complet + surtout la reponse `printer.objects.list` d'une
 * VRAIE imprimante (nombreux objets/macros) depasse facilement 4 Ko. Un tampon
 * trop petit tronquait le JSON -> parse en echec -> macros/etat non appliques
 * (le debordement est detecte et journalise, jamais un crash, mais la donnee
 * reelle etait perdue). Statique (.bss), pas de risque de pile. */
#define MOONRAKER_WS_TAMPON_OCTETS 16384
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
 * MOONRAKER_WS_COMMANDE_TIMEOUT_MS dans backend_moonraker.c pour ça, seul
 * appelant de moonraker_ws_commande() : ce fichier-ci ne fixe pas lui-même
 * de délai par défaut, le `timeout_ms` de moonraker_ws_commande() est
 * toujours fourni par l'appelant). Fix round 1 (revue tache 5, cosmetique) :
 * ce commentaire renvoyait vers un nom de constante qui n'a jamais existe
 * (MOONRAKER_WS_TIMEOUT_COMMANDE_DEFAUT_MS). */
#define MOONRAKER_WS_ENVOI_DELAI_MS 2000u

/* Granularité du sondage du corrélateur par moonraker_ws_commande() -- voir
 * le commentaire de tête. Assez court pour qu'un bouton utilisateur reste
 * réactif, assez long pour ne pas saturer inutilement le CPU en boucle. */
#define MOONRAKER_WS_SONDAGE_MS 20u

/* Backoff de reconnexion (spec §4, critère 2) : 1 s -> 2 -> 4 -> ... plafonné,
 * ré-identify + ré-abonnement à CHAQUE retour (jamais seulement le premier).
 * Plafond a 5 s (et non 30 s) : constate sur vraie K-Touch -- si l'ecran boote
 * AVANT l'imprimante, le backoff atteignait 30 s, donc jusqu'a 30 s de latence
 * pour accrocher quand l'imprimante revient (ressenti "n'accroche jamais"). A
 * 5 s la reconnexion est quasi-immediate ; le cout (une tentative WS toutes les
 * <=5 s pendant que l'imprimante est absente) est negligeable -- le poll HTTP
 * du backend tourne deja a ~4 s. */
#define MOONRAKER_WS_BACKOFF_INITIAL_MS 1000u
#define MOONRAKER_WS_BACKOFF_MAX_MS     5000u

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

/* Fix round 1 (revue tache 5, M1) : compteur + horodatage de throttle pour
 * journaliser_debordement(), meme structure que g_compteur_reconnexions/
 * g_dernier_journal_reco_us juste au-dessus -- voir son commentaire. */
static uint32_t g_compteur_debordements       = 0;
static int64_t  g_dernier_journal_debordement_us = 0;

/* Fix round 1 (revue tache 5, M3) : meme throttle que g_compteur_debordements
 * ci-dessus, pour une trame WS fragmentee au niveau protocole (fin=0) --
 * voir traiter_data() et son commentaire de tete pour ce que ce cas precis
 * signifie et pourquoi il est refuse plutot que recolle. */
static uint32_t g_compteur_fragmentations_ws       = 0;
static int64_t  g_dernier_journal_fragmentation_us = 0;

/* Fix round 1 (revue tache 5, M2) : vrai tant que ce module est cense
 * tourner -- distinct de g_connecte (qui dit si la CONNEXION est
 * actuellement etablie) : g_ws_demarre reste true pendant une coupure/
 * reconnexion, et ne passe a false QUE dans moonraker_ws_arreter(). Protege
 * par g_verrou : ecrit depuis boucle_klipper (demarrer()/arreter()), LU
 * depuis la tache esp_timer (minuterie_reconnexion_cb()) -- c'est
 * exactement la course entre l'arret du backend et une reconnexion deja
 * armee que ce drapeau neutralise, voir minuterie_reconnexion_cb() et
 * moonraker_ws_arreter(). */
static bool g_ws_demarre = false;

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

/* Trouvaille A (revue taches 4/5, jalon 3a) : rpc_lire_macros() (tache 3)
 * n'avait JAMAIS d'appelant -- sur un vrai Moonraker, etat->macros[] restait
 * vide pour toujours. Meme convention que g_id_abonnement juste au-dessus :
 * id de la requete printer.objects.list EN COURS, mis a jour a CHAQUE
 * (re)connexion (envoyer_identify_et_abonnement()) ET a chaque
 * notify_klippy_ready (Klippy peut redemarrer avec une config differente
 * SANS que le WS ne se deconnecte -- spec §5 : "remplie a la connexion...
 * et sur notify_klippy_ready"), lu uniquement depuis la tache WS -- aucun
 * verrou necessaire, meme raison que g_id_abonnement. */
static uint32_t g_id_macros = 0;

/* Tache 2, jalon "browser de fichiers" : meme convention que g_id_macros
 * juste au-dessus -- id de la requete `server.files.list` EN COURS, mis a
 * jour a CHAQUE (re)connexion (envoyer_identify_et_abonnement()), lu
 * uniquement depuis la tache WS -- aucun verrou necessaire, meme raison que
 * g_id_abonnement/g_id_macros. Contrairement aux macros, PAS de re-demande
 * sur notify_klippy_ready (MVP : un fetch au connect suffit -- la liste de
 * fichiers ne depend pas d'un redemarrage de Klippy comme le ferait une
 * config de macros). */
static uint32_t g_id_fichiers = 0;

/* Drapeau "une requete macros doit partir des que g_verrou sera relache" --
 * voir son unique lecteur/ecrivain sous verrou (traiter_message_complet(),
 * cas RPC_MSG_KLIPPY_READY) et son unique consommateur HORS verrou
 * (traiter_data(), juste apres xSemaphoreGive()). Necessaire car
 * envoyer_requete_macros() appelle prochain_id(), qui prend g_verrou en
 * interne (voir sa declaration) -- l'appeler DEPUIS traiter_message_complet()
 * (deja sous CE MEME verrou, non recursif) auto-interblocerait la tache WS,
 * EXACTEMENT le bug C1 CRITIQUE deja corrige une fois pour
 * moonraker_ws_commande() (fix round 1, revue tache 5) -- ce drapeau est ce
 * qui evite de le reintroduire ici. Pas de verrou dedie : lu et ecrit
 * uniquement depuis la tache WS elle-meme (traiter_data()/
 * traiter_message_complet() ne tournent jamais que la, en sequence, jamais
 * en concurrence l'une de l'autre). */
static bool g_macros_a_demander = false;

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
    /* Fix round 1 (revue tache 5, M1) : throttlé -- l'ancien commentaire
     * ("un débordement isolé est rarissime, une rafale serait déjà le
     * symptôme d'un problème pire") avait le raisonnement à l'envers : une
     * rafale est justement le cas où /log (16 Kio, seul canal de diagnostic
     * de cet appareil) a le PLUS besoin de survivre, pas le moins -- et le
     * risque n'est pas théorique : la fixture réelle la plus grosse de la
     * tâche 4 (host-test/fixtures/moonraker) culmine déjà à 3062 octets sur
     * 4096, un serveur qui pousserait un peu plus (beaucoup de macros, une
     * position à 8 extrudeurs) franchirait ce seuil à chaque
     * notify_status_update, potentiellement plusieurs fois par seconde --
     * exactement le débit qui rasait déjà le netlog dans
     * moonraker_journal_echec_pret() (backend_moonraker.c). Même politique
     * ici : premier événement journalisé, puis au plus une ligne par minute
     * tant que ça continue. */
    g_compteur_debordements++;
    int64_t maintenant = esp_timer_get_time();
    bool premier = (g_compteur_debordements == 1);
    bool intervalle_ecoule = (maintenant - g_dernier_journal_debordement_us) >= MOONRAKER_WS_JOURNAL_INTERVALLE_US;
    if (premier || intervalle_ecoule) {
        g_dernier_journal_debordement_us = maintenant;
        JOURNAL_ALERTE(TAG, "message WS au-dela de %u octets ; ignore (occurrences cumulees : %u)",
                       (unsigned)sizeof(g_tampon_msg) - 1u, (unsigned)g_compteur_debordements);
    }
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

static void journaliser_fragmentation_ws(void)
{
    /* Meme politique de throttle que journaliser_debordement() (fix round 1,
     * M1/M3) : premier evenement, puis au plus une ligne par minute. */
    g_compteur_fragmentations_ws++;
    int64_t maintenant = esp_timer_get_time();
    bool premier = (g_compteur_fragmentations_ws == 1);
    bool intervalle_ecoule = (maintenant - g_dernier_journal_fragmentation_us) >= MOONRAKER_WS_JOURNAL_INTERVALLE_US;
    if (premier || intervalle_ecoule) {
        g_dernier_journal_fragmentation_us = maintenant;
        JOURNAL_ALERTE(TAG,
            "trame WS fragmentee au niveau protocole (fin=0), non recollee (non supportee) ; "
            "message ignore (occurrences cumulees : %u)",
            (unsigned)g_compteur_fragmentations_ws);
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

/* Trouvaille A (revue taches 4/5, jalon 3a) : requete `printer.objects.list`
 * (jamais de params) -- source choisie parmi les deux formes que
 * rpc_lire_macros() reconnait (voir moonraker_rpc.h) : plus legere qu'un
 * dump complet de `configfile`, qui porterait aussi tout le contenu de
 * printer.cfg pour ne finalement en tirer que les noms de section
 * "gcode_macro ...". Appelee a CHAQUE (re)connexion (voir
 * envoyer_identify_et_abonnement() ci-dessous, appelee elle-meme a CHAQUE
 * WEBSOCKET_EVENT_CONNECTED -- "la souscription est liee a la connexion")
 * et depuis le cas RPC_MSG_KLIPPY_READY de traiter_message_complet() (via
 * g_macros_a_demander, voir son commentaire de declaration) -- jamais
 * appelee sous g_verrou directement : prochain_id() ci-dessous prend ce
 * meme verrou en interne. */
static void envoyer_requete_macros(void)
{
    char tampon[MOONRAKER_WS_REQUETE_OCTETS];
    uint32_t id = prochain_id();
    if (rpc_construire_requete(tampon, sizeof(tampon), id, "printer.objects.list", NULL)) {
        g_id_macros = id;
        /* Fix round 1 (revue tache 6, M1 MEDIUM) : le resultat de l'envoi
         * DOIT etre verifie -- esp_websocket_client_send_text() rend -1 sur
         * un timeout de tx_lock, une erreur de transport, ou un client pas
         * (plus) connecte (voir esp_websocket_client_commande() plus bas
         * dans ce fichier, qui fait deja ce controle pour les commandes
         * RPC). Sans lui, un envoi qui echoue laisse g_id_macros arme sur un
         * id dont la requete n'est JAMAIS partie : aucune reponse
         * n'arrivera jamais, aucune nouvelle tentative n'est programmee, et
         * -- sans ce JOURNAL_ALERTE -- rien ne le signale sur /log, le seul
         * canal de diagnostic d'un appareil sans port serie. Consequence
         * concrete que ce jalon existe pour eliminer : un timeout de
         * tx_lock transitoire au moment de la connexion => `etat.macros[]`
         * reste vide pour toujours => le bouton Macros de l'accueil
         * n'apparait jamais => macros inutilisables, potentiellement des
         * jours, sans la moindre trace. */
        int envoye = esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                                      pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
        if (envoye < 0) {
            JOURNAL_ALERTE(TAG, "envoi WS de printer.objects.list echoue (id=%u)", (unsigned)id);
        }
    } else {
        JOURNAL_ERREUR(TAG, "construction de printer.objects.list impossible");
    }
}

/* Tache 2, jalon "browser de fichiers" : COPIE d'envoyer_requete_macros()
 * ci-dessus, meme structure trait pour trait -- seules la methode
 * (`server.files.list`) et les params (`{"root":"gcodes"}`, la racine des
 * gcodes prets a imprimer -- rpc_construire_requete() recopie params_json
 * tel quel, voir moonraker_rpc.h) different. Appelee a CHAQUE (re)connexion
 * (voir envoyer_identify_et_abonnement() ci-dessous), JAMAIS sous g_verrou
 * directement -- meme remarque que pour envoyer_requete_macros() : prochain_id()
 * prend ce meme verrou en interne. Pas de re-demande sur notify_klippy_ready
 * (voir le commentaire de g_id_fichiers) : MVP, un fetch au connect suffit. */
static void envoyer_requete_fichiers(void)
{
    char tampon[MOONRAKER_WS_REQUETE_OCTETS];
    uint32_t id = prochain_id();
    if (rpc_construire_requete(tampon, sizeof(tampon), id, "server.files.list", "{\"root\":\"gcodes\"}")) {
        g_id_fichiers = id;
        /* Meme controle du resultat de l'envoi qu'envoyer_requete_macros()
         * ci-dessus (fix round 1, revue tache 6, M1 MEDIUM) -- voir son
         * commentaire pour la consequence concrete d'un envoi silencieusement
         * perdu. */
        int envoye = esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                                      pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
        if (envoye < 0) {
            JOURNAL_ALERTE(TAG, "envoi WS de server.files.list echoue (id=%u)", (unsigned)id);
        }
    } else {
        JOURNAL_ERREUR(TAG, "construction de server.files.list impossible");
    }
}

/* À WEBSOCKET_EVENT_CONNECTED : identify PUIS abonnement PUIS macros
 * (critère 2 -- à CHAQUE reconnexion, jamais seulement la première).
 * Appelée depuis la tâche WS elle-même (le gestionnaire d'événement) :
 * g_id_abonnement/g_id_macros n'ont donc besoin d'aucun verrou (voir leurs
 * commentaires de déclaration). */
static void envoyer_identify_et_abonnement(void)
{
    char tampon[MOONRAKER_WS_REQUETE_OCTETS];

    /* Fix round 1 (revue tache 6, M1 MEDIUM) : memes controles que
     * envoyer_requete_macros() ci-dessus sur les deux envois qui suivent --
     * un identify qui echoue silencieusement laisse Moonraker ignorer toute
     * la session (aucun accuse de reception attendu de toute facon, voir le
     * commentaire du cas RPC_MSG_REPONSE dans traiter_message_complet()),
     * et un abonnement qui echoue silencieusement se voit deja comme un flux
     * mort (aucun notify_status_update n'arrive jamais) -- mais NI L'UN NI
     * L'AUTRE n'etait signale sur /log avant ce fix, alors que
     * moonraker_ws_commande() fait deja ce controle pour les commandes RPC. */
    uint32_t id_identify = prochain_id();
    if (rpc_construire_requete(tampon, sizeof(tampon), id_identify, "server.connection.identify",
                                MOONRAKER_WS_IDENTIFY_PARAMS)) {
        int envoye = esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                                      pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
        if (envoye < 0) {
            JOURNAL_ALERTE(TAG, "envoi WS de server.connection.identify echoue (id=%u)",
                           (unsigned)id_identify);
        }
    } else {
        JOURNAL_ERREUR(TAG, "construction de server.connection.identify impossible");
    }

    uint32_t id_abonnement = prochain_id();
    if (rpc_construire_abonnement(tampon, sizeof(tampon), id_abonnement)) {
        g_id_abonnement = id_abonnement;
        int envoye = esp_websocket_client_send_text(g_client, tampon, (int)strlen(tampon),
                                                      pdMS_TO_TICKS(MOONRAKER_WS_ENVOI_DELAI_MS));
        if (envoye < 0) {
            JOURNAL_ALERTE(TAG, "envoi WS de l'abonnement echoue (id=%u)", (unsigned)id_abonnement);
        }
    } else {
        JOURNAL_ERREUR(TAG, "construction de l'abonnement impossible");
    }

    envoyer_requete_macros();
    /* Tache 2, jalon "browser de fichiers" : JUSTE APRES les macros, meme
     * endroit, meme absence de verrou -- voir le commentaire de
     * envoyer_requete_fichiers() ci-dessus. */
    envoyer_requete_fichiers();
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
        } else if (id == g_id_macros && g_id_macros != 0) {
            /* Trouvaille A : reponse a printer.objects.list -- rpc_lire_macros()
             * (tache 3) enfin appelee. Meme base cumulative que les deux cas
             * ci-dessus (fusion PARTIELLE du reste de l'etat ; rpc_lire_macros()
             * lui-meme REMPLACE entierement macros[]/nb_macros/macros_tronquees,
             * voir son commentaire dans moonraker_rpc.h -- c'est un instantane
             * complet, pas une fusion champ par champ). */
            etat_klipper_t copie = g_boite.etat;
            if (rpc_lire_macros(&copie, json, longueur)) {
                boite_deposer(&g_boite, &copie);
            }
        } else if (id == g_id_fichiers && g_id_fichiers != 0) {
            /* Tache 2, jalon "browser de fichiers" : reponse a
             * server.files.list -- rpc_lire_fichiers() (tache 1). Meme base
             * cumulative que les cas ci-dessus (fusion PARTIELLE du reste de
             * l'etat ; rpc_lire_fichiers() lui-meme REMPLACE entierement
             * fichiers[]/nb_fichiers/fichiers_tronques, voir son commentaire
             * dans moonraker_rpc.h -- un instantane complet, pas une fusion
             * champ par champ). */
            etat_klipper_t copie = g_boite.etat;
            if (rpc_lire_fichiers(&copie, json, longueur)) {
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
        /* Trouvaille A : Klippy peut redemarrer (FIRMWARE_RESTART, config
         * modifiee) SANS que le WS ne se deconnecte -- notify_klippy_ready
         * arrive alors SEUL, jamais precede d'un nouveau
         * WEBSOCKET_EVENT_CONNECTED (qui, lui, redemanderait deja les macros
         * via envoyer_identify_et_abonnement()). Sans ce second point
         * d'entree, une liste de macros changee par le redemarrage resterait
         * celle d'avant pour toujours. NE PAS appeler envoyer_requete_macros()
         * directement ICI : cette fonction appelle prochain_id(), qui prend
         * g_verrou en interne -- CE cas tourne deja SOUS g_verrou (voir le
         * commentaire de tete de cette fonction), l'appeler ici
         * auto-interbloquerait la tache WS (meme bug que le C1 CRITIQUE deja
         * corrige pour moonraker_ws_commande(), fix round 1 revue tache 5).
         * Le drapeau ci-dessous est consomme par traiter_data() UNE FOIS
         * g_verrou relache. */
        g_macros_a_demander = true;
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

/* Réassemble un message TEXTE arrivé en plusieurs événements
 * WEBSOCKET_EVENT_DATA dans le tampon statique borné, puis dispatch le
 * message complet sous verrou.
 *
 * Fix round 1 (revue tache 5, M3) : commentaire d'origine corrigé après
 * relecture de transport_ws.c (esp_websocket_client) -- CE qui est
 * effectivement géré ci-dessous, et CE qui ne l'est PAS :
 *   - GÉRÉ : le DÉCOUPAGE PAR LA BIBLIOTHÈQUE d'une SEULE trame WS plus
 *     grande que son `buffer_size` interne (indépendant de notre propre
 *     tampon ici) -- plusieurs événements DATA pour la MÊME trame, même
 *     `op_code`/`fin`, `payload_offset` croissant, `payload_len` constant
 *     (taille totale de CETTE trame). `data->payload_offset`/`payload_len`/
 *     `data_len` sont exactement les champs prévus pour recoller ce
 *     découpage-là, ce que le code ci-dessous fait.
 *   - PAS GÉRÉ (YAGNI tant que non observé) : la FRAGMENTATION AU NIVEAU
 *     PROTOCOLE WS (RFC 6455 -- un message logique envoyé en plusieurs
 *     TRAMES, la première `op_code=TEXTE` avec `fin=0`, les suivantes
 *     `op_code=CONTINUATION`). La bibliothèque ne recolle PAS ces trames
 *     pour nous : chacune arrive avec son PROPRE `payload_offset` reparti à
 *     0 et son PROPRE `payload_len` borné à CETTE SEULE trame. Sans le
 *     contrôle sur `data->fin` ci-dessous, la première trame (TEXTE, fin=0)
 *     serait dispatchée TRONQUÉE dès qu'elle atteint SA PROPRE fin, et la
 *     trame de continuation qui suit serait silencieusement perdue (déjà
 *     rejetée par le test `op_code == WS_TRANSPORT_OPCODES_TEXT`, qui exclut
 *     `WS_TRANSPORT_OPCODES_CONT`) -- un JSON tronqué dispatché comme s'il
 *     était complet. Moonraker/Tornado n'ont JAMAIS été observés fragmenter
 *     ainsi (vérifié contre les fixtures réelles de la tâche 4) : latent,
 *     jamais rencontré en pratique. Le contrôle ci-dessous REFUSE une telle
 *     trame plutôt que de la dispatcher à moitié -- une vraie reassemblage
 *     multi-trames RFC 6455 reste volontairement absente (YAGNI jusqu'à
 *     preuve qu'un serveur réel fragmente ainsi). */
static void traiter_data(const esp_websocket_event_data_t *data)
{
    if (data->payload_offset == 0) {
        /* Premiere partie (ou totalite) d'une trame : reinitialise le tampon
         * de reassemblage. `g_tampon_texte` n'accepte QUE les trames TEXTE
         * ET complètes en un seul envoi WS (`fin != 0`) -- voir le
         * commentaire de tete ci-dessus pour ce que ce deuxieme critere
         * exclut precisement. */
        g_tampon_len = 0;
        g_tampon_deborde = false;
        g_tampon_texte = (data->op_code == WS_TRANSPORT_OPCODES_TEXT) && (data->fin != 0);
        if (data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->fin == 0) {
            journaliser_fragmentation_ws();
        }
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

        /* Trouvaille A : consomme HORS verrou le drapeau pose par le cas
         * RPC_MSG_KLIPPY_READY de traiter_message_complet() -- voir le
         * commentaire de g_macros_a_demander pour pourquoi
         * envoyer_requete_macros() ne peut jamais etre appelee alors que
         * g_verrou est encore tenu. */
        if (g_macros_a_demander) {
            g_macros_a_demander = false;
            envoyer_requete_macros();
        }
    }
}

/* Callback de la minuterie de reconnexion (esp_timer, tourne dans la tache
 * esp_timer, PAS la tache WS -- sans consequence ici, ce callback ne touche
 * que l'etat de reconnexion et esp_websocket_client_start(), jamais
 * etat_store_*, boucle_*, ni LVGL).
 *
 * Fix round 1 (revue tache 5, M2) : cette minuterie peut se declencher APRES
 * que moonraker_ws_arreter() a deja detruit g_client (elle a ete armee AVANT
 * l'appel a arreter(), esp_timer_stop() n'annule que les armements FUTURS,
 * pas un declenchement deja en cours d'ordonnancement) -- lire g_client sans
 * verrou a ce moment-la serait un use-after-free, et rappeler
 * esp_websocket_client_start(NULL) sans le detecter re-armerait cette
 * minuterie indefiniment sur un backend deja arrete (3f, qui cycle les
 * backends d'un profil a l'autre, EXERCERA ce chemin). g_ws_demarre (mis a
 * false sous verrou par moonraker_ws_arreter() AVANT que g_client ne soit
 * touche, voir plus bas) est la garde : verifiee ici sous le MEME verrou, et
 * g_client lu SOUS CE VERROU aussi -- jamais apres l'avoir relache. La
 * portee du verrou reste volontairement COURTE (jamais tenue pendant
 * l'appel bloquant a esp_websocket_client_start() lui-meme, qui pourrait
 * prendre un temps non borne) : voir moonraker_ws_arreter() pour la moitie
 * symetrique de cette synchronisation. Fenetre residuelle assumee et
 * documentee (pas prouvee nulle) : entre la lecture de g_client sous verrou
 * ici et l'appel a esp_websocket_client_start() juste apres, une
 * moonraker_ws_arreter() concurrente pourrait deja avoir commence a detruire
 * CE MEME handle -- inevitable sans tenir le verrou pendant tout l'appel
 * bloquant, ce qui risquerait a son tour un interblocage avec la tache WS
 * elle-meme (voir moonraker_ws_arreter()). Le cas reel vise ici (3f, un
 * arret qui n'est PAS simultane a la microseconde pres avec le reveil de
 * cette minuterie) est ferme ; une course a la microseconde entre les deux
 * ne l'est pas et n'a pas de solution sans lock plus intrusif. */
static void minuterie_reconnexion_cb(void *arg)
{
    (void)arg;

    xSemaphoreTake(g_verrou, portMAX_DELAY);
    if (!g_ws_demarre || g_client == NULL) {
        /* moonraker_ws_arreter() est passee entre l'armement de cette
         * minuterie et son declenchement : rien a reconnecter, et surtout
         * NE PAS se re-armer -- sans ce retour immediat, un backend arrete
         * verrait cette minuterie se redeclencher pour toujours. */
        g_reconnexion_armee = false;
        xSemaphoreGive(g_verrou);
        return;
    }
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
    esp_websocket_client_handle_t client = g_client;
    xSemaphoreGive(g_verrou);

    esp_err_t erreur = esp_websocket_client_start(client);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "relance du client WS impossible (%s) ; nouvelle tentative programmee",
                       esp_err_to_name(erreur));
        xSemaphoreTake(g_verrou, portMAX_DELAY);
        if (g_ws_demarre) {
            /* Re-verifie sous verrou : un arret aurait pu survenir PENDANT
             * l'appel (non bloquant en principe, mais rien ne l'exige) a
             * esp_websocket_client_start() ci-dessus -- meme garde qu'a
             * l'entree de ce callback, jamais un re-armement inconditionnel. */
            g_reconnexion_armee = true;
            esp_timer_start_once(g_minuterie_reconnexion, (uint64_t)g_backoff_ms * 1000ULL);
        }
        xSemaphoreGive(g_verrou);
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

/* Arret interne partage entre moonraker_ws_arreter() (API publique) et la
 * garde defensive de moonraker_ws_demarrer() (rappele sans arreter()
 * intermediaire) -- fix round 1 (revue tache 5, M2) : UNE SEULE
 * implementation de la sequence "sans risque" (arreter la minuterie, geler
 * g_ws_demarre puis detacher g_client SOUS VERROU avant de les toucher hors
 * verrou), jamais deux copies qui pourraient diverger, l'une corrigee et
 * l'autre non (c'etait exactement le defaut avant ce round : la garde de
 * demarrer() faisait un stop()/destroy() direct, sans jamais passer par
 * cette synchronisation).
 *
 * Le verrou n'est JAMAIS tenu pendant esp_websocket_client_stop()/destroy()
 * eux-memes (potentiellement bloquants, la tache WS elle-meme peut avoir
 * besoin de ce meme verrou dans traiter_data() pour progresser jusqu'a son
 * point d'arret -- le tenir ici risquerait un interblocage symetrique a
 * celui de C1). Le detachement de g_client (snapshot + mise a NULL) SOUS
 * verrou, lui, ferme la fenetre "callback utilise un handle deja detruit"
 * decrite dans minuterie_reconnexion_cb() -- pas la fenetre plus etroite
 * documentee dans ce meme commentaire (lecture sous verrou puis appel a
 * esp_websocket_client_start() hors verrou juste apres), assumee. */
static void ws_teardown(void)
{
    if (g_minuterie_reconnexion != NULL) {
        esp_timer_stop(g_minuterie_reconnexion); /* sans effet si deja arretee ; empeche tout FUTUR armement */
    }

    if (g_verrou == NULL) {
        /* demarrer() n'a jamais ete appelee (ou a echoue avant de creer le
         * verrou) : rien n'a jamais ete mis en route, rien a detacher. */
        g_connecte = false;
        return;
    }

    esp_websocket_client_handle_t client_a_detruire = NULL;
    xSemaphoreTake(g_verrou, portMAX_DELAY);
    g_ws_demarre = false;
    g_reconnexion_armee = false;
    client_a_detruire = g_client;
    g_client = NULL; /* plus aucun lecteur (minuterie_reconnexion_cb(), commande()) ne verra ce handle */
    xSemaphoreGive(g_verrou);

    if (client_a_detruire != NULL) {
        esp_websocket_client_stop(client_a_detruire);
        esp_websocket_client_destroy(client_a_detruire);
    }
    g_connecte = false;
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
     * client deja cree -- ws_teardown() ci-dessus, jamais un stop()/destroy()
     * direct (voir son commentaire : c'etait la moitie non corrigee de M2). */
    ws_teardown();

    g_hote = *hote;
    memset(&g_boite, 0, sizeof(g_boite));
    memset(&g_correlateur, 0, sizeof(g_correlateur));
    g_connecte = false;
    g_klippy_pret = true;
    g_backoff_ms = MOONRAKER_WS_BACKOFF_INITIAL_MS;
    g_reconnexion_armee = false;
    g_compteur_reconnexions = 0;
    g_dernier_journal_reco_us = 0;
    g_compteur_debordements = 0;
    g_dernier_journal_debordement_us = 0;
    g_compteur_fragmentations_ws = 0;
    g_dernier_journal_fragmentation_us = 0;
    g_id_abonnement = 0;
    g_id_macros = 0;
    g_id_fichiers = 0;
    g_macros_a_demander = false;
    g_id_suivant = 1;
    g_tampon_len = 0;
    g_tampon_deborde = false;

    char url[BACKEND_HOTE_LONGUEUR_MAX + 32];
    construire_url(url, sizeof(url), &g_hote);

    esp_websocket_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.uri = url;
    config.disable_auto_reconnect = true; /* backoff exponentiel gere par CE fichier, pas la bibliotheque */
    /* Pile de la tache WS : le DEFAUT esp_websocket_client est 4 Ko, INSUFFISANT
     * ici. ws_event_handler() -> traiter_message_complet() -> rpc_fusionner_*()/
     * rpc_lire_fichiers() tournent tous DANS cette tache, et le handler ci-dessus
     * pose un `etat_klipper_t copie = g_boite.etat` sur la pile (jusqu'a deux
     * copies vivantes a la fois avec le local de rpc_fusionner_*), plus la
     * recursion de cJSON_ParseWithLength() sur le payload.
     *
     * ATTENTION -- taille de l'etat DOUBLEE depuis le sous-projet navigateur de
     * fichiers : sizeof(etat_klipper_t) est passe de 1808 a ~3856 octets (champ
     * `fichiers[32][64]`, +2 Ko). Deux copies = ~7,7 Ko de pile, contre ~3,6 Ko
     * avant. Les petites fixtures vkp tenaient dans 16 Ko, mais l'etat REEL
     * complet d'une vraie imprimante (Freebox/CR-10/Snapmaker) faisait de nouveau
     * deborder la pile -> crash + reboot PILE A LA CONNEXION Moonraker (constate
     * sur le materiel reel). D'ou 32 Ko ici : marge confortable pour 2x3856 +
     * cJSON profond sur un gros instantane. (Fix propre a venir : sortir
     * `fichiers[]` de l'etat toujours-copie pour revenir a ~1808 octets.)
     * buffer_size : 4 Ko (defaut 1 Ko) reduit la fragmentation applicative des
     * grosses trames (le reassemblage g_tampon_msg la gere de toute facon). */
    config.task_stack = 32768;
    config.buffer_size = 4096;

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

    /* Publie g_client comme "en service" -- fix round 1 (revue tache 5, M2) :
     * SOUS le meme verrou que minuterie_reconnexion_cb()/ws_teardown()
     * relisent/ecrivent g_ws_demarre et g_client, pour que la barriere
     * memoire du mutex garantisse qu'un lecteur qui voit g_ws_demarre==true
     * voit aussi le g_client entierement initialise ci-dessus (jamais un
     * fragment de son ecriture, meme sans verrou explicite pendant la mise
     * en place elle-meme : aucun lecteur concurrent ne peut exister tant que
     * ce drapeau reste false). */
    xSemaphoreTake(g_verrou, portMAX_DELAY);
    g_ws_demarre = true;
    xSemaphoreGive(g_verrou);

    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u url=%s)", hote->adresse, (unsigned)hote->port, url);
    return ESP_OK;
}

void moonraker_ws_arreter(void)
{
    ws_teardown(); /* voir son commentaire : sequence partagee avec la garde de moonraker_ws_demarrer() */
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
     * mutex NON recursif (voir sa prise a la ligne suivante) : un appel a
     * prochain_id() alors que ce site tient deja g_verrou aurait bloque
     * cette tache indefiniment sur son propre verrou (auto-interblocage,
     * aucune assertion FreeRTOS ne le detecte sur ce chemin). Verifie
     * apres correctif : chaque paire xSemaphoreTake(g_verrou)/xSemaphoreGive(g_verrou)
     * de ce fichier relue une a une (voir le rapport de tache pour la liste
     * complete) -- prochain_id() est desormais TOUJOURS appelee hors de
     * toute section critique tenue par ce meme fichier. */
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

        /* Fix round 1 (revue tache 5, L2) : une deconnexion PENDANT
         * l'attente n'obtiendra plus jamais de reponse corrélée (la tache WS
         * va ré-identifier/ré-abonner a la reconnexion, avec de NOUVEAUX id,
         * jamais celui-ci) -- attendre quand meme le timeout_ms complet
         * (jusqu'a MOONRAKER_WS_COMMANDE_TIMEOUT_MS, voir backend_moonraker.c)
         * ferait patienter l'utilisateur pour rien sur un signal deja
         * disponible. Sort au tick de sondage suivant (<= MOONRAKER_WS_SONDAGE_MS,
         * 20 ms) avec un code honnete -- ESP_ERR_INVALID_STATE, pas
         * ESP_ERR_TIMEOUT, pour ne pas laisser croire que Moonraker n'a
         * simplement pas eu le temps de repondre. */
        if (!g_connecte) {
            xSemaphoreTake(g_verrou, portMAX_DELAY);
            g_correlateur.en_cours = false;
            xSemaphoreGive(g_verrou);
            JOURNAL_ALERTE(TAG, "commande WS %s : deconnecte pendant l'attente de la reponse", methode);
            return ESP_ERR_INVALID_STATE;
        }

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
