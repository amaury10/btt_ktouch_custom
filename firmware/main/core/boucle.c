#include "boucle.h"

#include <string.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "boucle_cycle.h"
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

/* Plus petit tas libre jamais observé par boucle_tache() (voir son log en
 * fin de boucle ci-dessous) : UINT32_MAX au départ, jamais réellement
 * atteinte par esp_get_free_heap_size(), donc le premier cycle journalise
 * toujours. Lue et écrite uniquement par cette tâche (aucun accès
 * concurrent, donc pas de mutex nécessaire) — même principe que
 * s_marge_pile_min dans web.c. */
static uint32_t               s_tas_libre_min = UINT32_MAX;

/* Protège uniquement l'instant de la permutation (etat_store_valider()) et
 * la copie faite par boucle_etat_copier() — jamais le rafraîchissement
 * complet (la requête HTTP), qui reste hors verrou pour ne jamais faire
 * attendre un appelant (typiquement l'interface LVGL du sous-jalon 2b)
 * plusieurs secondes derrière un POST. En dehors de ces deux instants très
 * courts (un memcmp et un memcpy de la taille de etat_klipper_t), le tampon
 * avant est stable : etat_store_tampon_arriere() ne touche jamais que le
 * tampon arrière, jamais celui qu'un lecteur consulte. */
static SemaphoreHandle_t      g_mutex_etat;

/* Tâche 9 : dernier échec de commande en attente d'être consommé par
 * boucle_commande_echec() (voir son commentaire dans boucle.h), protégé par
 * le MÊME g_mutex_etat que le reste de ce triplet d'état partagé entre la
 * tâche d'interrogation et un appelant concurrent -- jamais un second mutex
 * pour une aussi petite section critique (un snprintf et deux booléens). */
static char g_echec_action[BOUCLE_ACTION_MAX];
static bool g_echec_en_attente = false;

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

            /* Tâche 9 : mémorise cet échec pour qu'habillage_pomper() puisse
             * le remonter à l'écran via ui_commande_echec() -- sans ceci, un
             * échec survenu APRÈS que ui_commander() a déjà rendu ESP_OK à
             * son appelant ne serait jamais visible que dans ce journal.
             * Sous mutex : lu depuis une autre tâche (LVGL) par
             * boucle_commande_echec(), jamais protégé autrement ici. */
            xSemaphoreTake(g_mutex_etat, portMAX_DELAY);
            strlcpy(g_echec_action, cmd.action, sizeof(g_echec_action));
            g_echec_en_attente = true;
            xSemaphoreGive(g_mutex_etat);
        }
    }
}

static void boucle_tache(void *parametre)
{
    (void)parametre;

    for (;;) {
        boucle_traiter_commandes();

        /* Le corps du cycle (remise à zéro du tampon arrière, appel à
         * desc->rafraichir(), mise à jour de la liaison) vit désormais dans
         * boucle_cycle() (core/boucle_cycle.c), pur et testable sur PC — voir
         * host-test/tests/test_boucle_cycle.c. Cette fonction-ci (le "shell"
         * FreeRTOS) ne garde que ce qui ne peut pas être autrement : la
         * tâche, la file de commandes, et le mutex ci-dessous. */
        bool succes = boucle_cycle(&g_store, &g_liaison, g_desc);

        if (succes) {
            /* Validé seulement en cas de succès : c'est ce qui tient la
             * promesse de moonraker_parse.h (« sortie n'est pas modifiée en
             * cas d'échec, l'appelant garde son dernier état connu ») au
             * niveau du magasin double tampon. Le tampon arrière a été remis
             * à zéro par boucle_cycle() ci-dessus (via
             * etat_store_tampon_arriere()) ; si rafraichir() avait échoué
             * avant de le remplir (panne réseau, JSON illisible), succes
             * vaudrait false et ce bloc ne s'exécuterait pas. Valider quand
             * même permuterait ce zéro à la place du dernier état réellement
             * connu, effaçant l'écran au lieu de le griser — exactement ce
             * que liaison.h interdit ("un écran ne montre jamais de boîte
             * d'erreur réseau, il grise ses données périmées"). Ne pas
             * valider laisse le tampon avant (donc etat_store_lire()) sur
             * le dernier succès, pendant que liaison_echec() (dans
             * boucle_cycle()) a déjà fait savoir à l'habillage que ces
             * données sont périmées si succes est false.
             *
             * Sous mutex : c'est le seul instant où le tampon "avant" change
             * de pointeur. Sans ce verrou, un boucle_etat_copier() concurrent
             * pourrait lire le pointeur juste avant la permutation puis
             * copier depuis un tampon qui devient "arrière" entre-temps —
             * possiblement déjà remis à zéro par le etat_store_tampon_arriere()
             * du cycle suivant avant que la copie n'ait fini. Ce même verrou
             * ne protège JAMAIS l'appel à boucle_cycle() lui-même : il peut
             * contenir une requête HTTP de plusieurs secondes
             * (desc->rafraichir()), et un lecteur concurrent (l'interface
             * LVGL du sous-jalon 2b, via boucle_etat_copier()/
             * boucle_instantane()) ne doit jamais attendre derrière elle. */
            xSemaphoreTake(g_mutex_etat, portMAX_DELAY);
            etat_store_valider(&g_store);
            xSemaphoreGive(g_mutex_etat);
        }

        /* Mesure, pas estimation : voir gestion_state() dans web.c pour le
         * même principe appliqué à la marge de pile, et le commentaire de
         * MOONRAKER_TAMPON_OCTETS dans backend_moonraker.c pour la raison
         * précise (moonraker_parse_status() alloue et libère un arbre cJSON
         * complet à chaque cycle réussi — équilibré, mais une churn de tas
         * bien réelle sur un appareil qui tourne des jours sans redémarrer).
         * Même filtre que web.c, pour la même raison : cette tâche tourne en
         * continu, une trace à CHAQUE cycle chasserait la séquence de
         * démarrage du netlog (16 Kio) en quelques minutes. Une ligne au
         * premier cycle, puis seulement quand le pire jamais observé
         * s'aggrave. */
        uint32_t tas_libre = (uint32_t)esp_get_free_heap_size();
        if (tas_libre < s_tas_libre_min) {
            s_tas_libre_min = tas_libre;
            JOURNAL_INFO(TAG, "boucle_tache : nouveau minimum de tas libre %u octets",
                         (unsigned)tas_libre);
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

    g_mutex_etat = xSemaphoreCreateMutex();
    if (g_mutex_etat == NULL) {
        JOURNAL_ERREUR(TAG, "xSemaphoreCreateMutex a echoue");
        vQueueDelete(g_file_commandes);
        g_file_commandes = NULL;
        etat_store_liberer(&g_store);
        return ESP_ERR_NO_MEM;
    }

    /* Seuils par défaut de liaison.h (voir leur commentaire dans liaison.h,
     * qui les nomme précisément pour que ce fichier n'ait pas à les
     * dupliquer) : à une interrogation par seconde, 3 échecs consécutifs
     * suffisent à distinguer une vraie dégradation d'un paquet isolé perdu
     * sur le réseau local ; 10 échecs signalent une coupure réelle. */
    liaison_init(&g_liaison, LIAISON_SEUIL_DEGRADE_DEFAUT, LIAISON_SEUIL_HORS_LIGNE_DEFAUT);
    g_desc = desc;

    /* Premier appel à demarrer() sur le tampon arrière (déjà à zéro par
     * etat_store_init()), puis validation. Les deux tampons du magasin sont
     * encore à zéro à ce stade (etat_store_init() les a calloc'és) : si
     * demarrer() ne fait qu'imiter cette remise à zéro, comme le font
     * aujourd'hui backend_factice.c et backend_moonraker.c, memcmp() ne voit
     * aucune différence et etat_store_valider() ci-dessous rend false — la
     * génération reste à 0. C'est correct et attendu, pas une publication
     * manquée : un backend qui donnerait un état initial non nul (par
     * exemple relu d'une session précédente) déclencherait, lui, un vrai
     * premier changement, et generation passerait à 1 comme n'importe quel
     * autre. */
    void *initial = etat_store_tampon_arriere(&g_store);
    esp_err_t erreur = desc->demarrer(initial, hote);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "demarrer() du backend %s a echoue (%s)",
                       desc->nom != NULL ? desc->nom : "?", esp_err_to_name(erreur));
        vSemaphoreDelete(g_mutex_etat);
        g_mutex_etat = NULL;
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
        vSemaphoreDelete(g_mutex_etat);
        g_mutex_etat = NULL;
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

bool boucle_etat_copier(void *dest, size_t taille)
{
    if (!g_demarre || dest == NULL) {
        return false;
    }
    if (taille != g_store.taille) {
        JOURNAL_ALERTE(TAG, "boucle_etat_copier : taille %u attendue, %u recue",
                       (unsigned)g_store.taille, (unsigned)taille);
        return false;
    }

    xSemaphoreTake(g_mutex_etat, portMAX_DELAY);
    memcpy(dest, etat_store_lire(&g_store), taille);
    xSemaphoreGive(g_mutex_etat);
    return true;
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

bool boucle_instantane(void *dest, size_t taille, uint32_t *generation, liaison_etat_t *liaison)
{
    if (!g_demarre || dest == NULL) {
        return false;
    }
    if (taille != g_store.taille) {
        JOURNAL_ALERTE(TAG, "boucle_instantane : taille %u attendue, %u recue",
                       (unsigned)g_store.taille, (unsigned)taille);
        return false;
    }

    /* Même verrou que boucle_etat_copier() et etat_store_valider() : la
     * lecture des trois valeurs ci-dessous se fait sans jamais laisser la
     * tâche d'interrogation permuter le magasin d'état entre deux d'entre
     * elles (voir le commentaire de g_mutex_etat plus haut dans ce
     * fichier). */
    xSemaphoreTake(g_mutex_etat, portMAX_DELAY);
    memcpy(dest, etat_store_lire(&g_store), taille);
    if (generation != NULL) {
        *generation = etat_store_generation(&g_store);
    }
    if (liaison != NULL) {
        *liaison = liaison_etat(&g_liaison);
    }
    xSemaphoreGive(g_mutex_etat);
    return true;
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

bool boucle_commande_echec(char *action, size_t taille)
{
    if (!g_demarre || action == NULL || taille == 0) {
        return false;
    }

    bool en_attente;
    xSemaphoreTake(g_mutex_etat, portMAX_DELAY);
    en_attente = g_echec_en_attente;
    if (en_attente) {
        strlcpy(action, g_echec_action, taille);
        g_echec_en_attente = false;
    }
    xSemaphoreGive(g_mutex_etat);
    return en_attente;
}
