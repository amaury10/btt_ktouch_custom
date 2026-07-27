#include "boucle.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "etat_store.h"
#include "journal.h"

static const char *TAG = "boucle";

/* Période visée entre deux rafraîchissements, mesurée d'une fin de cycle à
 * l'autre (vTaskDelay simple, pas vTaskDelayUntil) : un cycle lent — une
 * requête HTTP qui traîne jusqu'à MOONRAKER_DELAI_MS — retarde d'autant le
 * suivant plutôt que d'enchaîner sans pause pour rattraper le retard. Sur un
 * réseau qui va déjà mal, enchaîner les requêtes sans respirer est la
 * dernière chose à faire. */
#define BOUCLE_PERIODE_MS 1000u

/* Profondeur de la file de commandes, imposée par le brief : appuyer sur
 * pause/reprendre/annuler/urgence en rafale (un utilisateur qui martèle un
 * bouton pendant une commande lente) ne doit pas se perdre silencieusement
 * avant d'avoir de quoi remplir un cycle de boucle. Au-delà, boucle_commander
 * rend ESP_ERR_NO_MEM plutôt que d'attendre : voir le commentaire en tête de
 * boucle.h sur la raison de ne jamais bloquer l'appelant. */
#define BOUCLE_FILE_PROFONDEUR 4

/* Tailles de tampon d'une commande en file. BOUCLE_ACTION_MAX couvre large
 * les noms actuels ("arret_urgence" fait 14 octets) et les futurs sans qu'on
 * ait à y revenir. BOUCLE_ARGUMENTS_MAX existe pour respecter le contrat de
 * backend_desc_t (arguments_json), même si aucune des quatre actions
 * connues de backend_moonraker.c n'en consomme aujourd'hui. */
#define BOUCLE_ACTION_MAX    32
#define BOUCLE_ARGUMENTS_MAX 128

/* Pile et priorité de la tâche d'interrogation, reprises de la seule autre
 * tâche créée par ce firmware (rescue.c, tache_sur_echeance) : 8192 octets
 * couvre confortablement esp_http_client en clair (pas de TLS, donc pas de
 * pile mbedtls) plus l'analyse cJSON du corps de réponse, avec de la marge ;
 * tskIDLE_PRIORITY + 5 place cette tâche nettement au-dessus de l'IDLE mais
 * sans rivaliser avec les tâches internes du pilote WiFi ni la tâche LVGL. */
#define BOUCLE_PILE_OCTETS 8192
#define BOUCLE_PRIORITE    (tskIDLE_PRIORITY + 5)

/* Seuils de liaison_init() : à une interrogation par seconde, 3 échecs
 * consécutifs (~3 s) suffisent à distinguer une vraie dégradation d'un paquet
 * isolé perdu sur le réseau local (voir le commentaire de liaison_echec()) ;
 * 10 échecs (~10 s) signalent une coupure réelle sans réagir au moindre creux
 * passager d'un réseau WiFi domestique. */
#define BOUCLE_SEUIL_DEGRADE     3
#define BOUCLE_SEUIL_HORS_LIGNE 10

typedef struct {
    char action[BOUCLE_ACTION_MAX];
    char arguments_json[BOUCLE_ARGUMENTS_MAX];
    bool avec_arguments;
} boucle_commande_t;

static etat_store_t          g_store;
static liaison_t              g_liaison;
static QueueHandle_t          g_file_commandes;
static TaskHandle_t           g_tache;
static const backend_desc_t  *g_desc;
static bool                   g_demarre = false;

/* Dépile et exécute toutes les commandes en attente. Appelée par la tâche
 * d'interrogation elle-même, jamais par l'appelant de boucle_commander() —
 * c'est ce qui tient la promesse du brief : un appui bouton n'attend jamais
 * la commande HTTP qu'il déclenche. */
static void boucle_traiter_commandes(void)
{
    boucle_commande_t cmd;
    while (xQueueReceive(g_file_commandes, &cmd, 0) == pdTRUE) {
        /* g_desc->commande() attend un `void *etat` non constant par
         * contrat (backend.h), mais aucun backend connu n'y écrit — c'est le
         * tampon arrière du magasin, remis à zéro juste après par
         * etat_store_tampon_arriere() avant le prochain rafraîchissement,
         * donc une écriture éventuelle ici ne survivrait de toute façon pas
         * jusqu'à l'affichage. On évite ainsi tout pointeur constant
         * détourné ou tampon dédié supplémentaire pour un paramètre que le
         * contrat exige mais qu'aucun backend n'utilise réellement. */
        void *etat = etat_store_tampon_arriere(&g_store);
        const char *arguments = cmd.avec_arguments ? cmd.arguments_json : NULL;

        esp_err_t erreur = g_desc->commande(etat, cmd.action, arguments);
        if (erreur == ESP_OK) {
            JOURNAL_INFO(TAG, "commande %s executee", cmd.action);
        } else {
            JOURNAL_ALERTE(TAG, "commande %s en echec (%s)", cmd.action, esp_err_to_name(erreur));
        }
    }
}

static void boucle_tache(void *parametre)
{
    (void)parametre;

    for (;;) {
        boucle_traiter_commandes();

        /* Remis à zéro ici, après boucle_traiter_commandes() : une commande
         * qui aurait (contre le contrat) écrit dans le tampon arrière ne
         * doit jamais contaminer le prochain rafraîchissement. */
        void *arriere = etat_store_tampon_arriere(&g_store);
        esp_err_t erreur = g_desc->rafraichir(arriere);

        if (erreur == ESP_OK) {
            liaison_succes(&g_liaison);
            /* Validé seulement en cas de succès : c'est ce qui tient la
             * promesse de moonraker_parse.h (« sortie n'est pas modifiée en
             * cas d'échec, l'appelant garde son dernier état connu ») au
             * niveau du magasin double tampon. Le tampon arrière vient
             * d'être remis à zéro par etat_store_tampon_arriere() ci-dessus
             * ; si rafraichir() a échoué avant de le remplir (panne réseau,
             * JSON illisible), il est resté à zéro. Le valider quand même
             * permuterait ce zéro à la place du dernier état réellement
             * connu, effaçant l'écran au lieu de le griser — exactement ce
             * que liaison.h interdit ("un écran ne montre jamais de boîte
             * d'erreur réseau, il grise ses données périmées"). Ne pas
             * valider laisse le tampon avant (donc etat_store_lire()) sur
             * le dernier succès, pendant que liaison_echec() ci-dessous fait
             * savoir à l'habillage que ces données sont périmées. */
            etat_store_valider(&g_store);
        } else {
            liaison_echec(&g_liaison);
        }

        vTaskDelay(pdMS_TO_TICKS(BOUCLE_PERIODE_MS));
    }
}

esp_err_t boucle_demarrer(const backend_desc_t *desc, const backend_hote_t *hote)
{
    if (desc == NULL || hote == NULL || desc->demarrer == NULL || desc->rafraichir == NULL ||
        desc->arreter == NULL || desc->commande == NULL) {
        /* Les quatre pointeurs sont vérifiés ici, une fois pour toutes : sans
         * ce garde, un backend incomplet planterait plus tard sur un appel de
         * pointeur NULL — soit dans boucle_tache() au premier rafraîchissement,
         * soit dans boucle_traiter_commandes() au premier bouton pressé, l'un
         * comme l'autre bien plus difficiles à relier à leur cause qu'un rejet
         * immédiat ici. */
        return ESP_ERR_INVALID_ARG;
    }
    if (g_demarre) {
        JOURNAL_ALERTE(TAG, "boucle_demarrer appele une seconde fois ; ignore");
        return ESP_ERR_INVALID_STATE;
    }

    if (!etat_store_init(&g_store, desc->taille_etat)) {
        JOURNAL_ERREUR(TAG, "etat_store_init a echoue (taille=%u)", (unsigned)desc->taille_etat);
        return ESP_ERR_NO_MEM;
    }

    g_file_commandes = xQueueCreate(BOUCLE_FILE_PROFONDEUR, sizeof(boucle_commande_t));
    if (g_file_commandes == NULL) {
        JOURNAL_ERREUR(TAG, "xQueueCreate a echoue");
        etat_store_liberer(&g_store);
        return ESP_ERR_NO_MEM;
    }

    liaison_init(&g_liaison, BOUCLE_SEUIL_DEGRADE, BOUCLE_SEUIL_HORS_LIGNE);
    g_desc = desc;

    /* Premier appel à demarrer() sur le tampon arrière (déjà à zéro par
     * etat_store_init()), puis validation : un état initial explicitement
     * publié par le backend plutôt qu'un magasin resté dans l'état où
     * etat_store_init() l'a laissé sans que personne ne l'ait vu passer par
     * demarrer(). */
    void *initial = etat_store_tampon_arriere(&g_store);
    esp_err_t erreur = desc->demarrer(initial, hote);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "demarrer() du backend %s a echoue (%s)",
                       desc->nom != NULL ? desc->nom : "?", esp_err_to_name(erreur));
        vQueueDelete(g_file_commandes);
        g_file_commandes = NULL;
        etat_store_liberer(&g_store);
        return erreur;
    }
    etat_store_valider(&g_store);

    BaseType_t cree = xTaskCreate(boucle_tache, "boucle_klipper", BOUCLE_PILE_OCTETS, NULL,
                                   BOUCLE_PRIORITE, &g_tache);
    if (cree != pdPASS) {
        JOURNAL_ERREUR(TAG, "xTaskCreate a echoue");
        /* demarrer() a reussi juste au-dessus : par symetrie, le backend doit
         * etre prevenu qu'on renonce, au cas ou une future implementation y
         * tiendrait une ressource a liberer (le backend Moonraker actuel n'en
         * a pas besoin, mais rien ne garantit que ce sera toujours le cas). */
        desc->arreter(initial);
        vQueueDelete(g_file_commandes);
        g_file_commandes = NULL;
        etat_store_liberer(&g_store);
        return ESP_ERR_NO_MEM;
    }

    g_demarre = true;
    JOURNAL_INFO(TAG, "boucle demarree (backend=%s hote=%s port=%u)",
                 desc->nom != NULL ? desc->nom : "?", hote->adresse, (unsigned)hote->port);
    return ESP_OK;
}

const void *boucle_etat(void)
{
    if (!g_demarre) {
        return NULL;
    }
    return etat_store_lire(&g_store);
}

uint32_t boucle_generation(void)
{
    if (!g_demarre) {
        return 0;
    }
    return etat_store_generation(&g_store);
}

liaison_etat_t boucle_liaison(void)
{
    if (!g_demarre) {
        return LIAISON_CONNEXION;
    }
    return liaison_etat(&g_liaison);
}

esp_err_t boucle_commander(const char *action, const char *arguments_json)
{
    if (!g_demarre) {
        return ESP_ERR_INVALID_STATE;
    }
    if (action == NULL || action[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(action) >= BOUCLE_ACTION_MAX) {
        JOURNAL_ALERTE(TAG, "action trop longue pour la file (%s)", action);
        return ESP_ERR_INVALID_ARG;
    }
    if (arguments_json != NULL && strlen(arguments_json) >= BOUCLE_ARGUMENTS_MAX) {
        JOURNAL_ALERTE(TAG, "arguments_json trop long pour la file (action=%s)", action);
        return ESP_ERR_INVALID_ARG;
    }

    boucle_commande_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    strlcpy(cmd.action, action, sizeof(cmd.action));
    if (arguments_json != NULL) {
        strlcpy(cmd.arguments_json, arguments_json, sizeof(cmd.arguments_json));
        cmd.avec_arguments = true;
    }

    /* Délai nul : boucle_commander() ne bloque JAMAIS l'appelant, même
     * brièvement — un rappel de bouton (jalon 2b) tourne côté LVGL et ne doit
     * jamais attendre une place en file. Une file pleine est signalée par
     * ESP_ERR_NO_MEM plutôt que d'y patienter. */
    BaseType_t ok = xQueueSend(g_file_commandes, &cmd, 0);
    if (ok != pdTRUE) {
        JOURNAL_ALERTE(TAG, "file de commandes pleine ; commande %s perdue", action);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
