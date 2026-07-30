/* Implémentation simulateur de la façade d'état (voir
 * firmware/main/ui/source_etat.h) : possède elle-même le magasin d'état, la
 * liaison et une petite file de commandes à profondeur bornée — un décalque
 * mono-thread de core/boucle.c, sans FreeRTOS. Voir source_etat_sim.h pour
 * les deux fonctions supplémentaires (source_etat_sim_demarrer/_cycle) qui
 * pilotent ce module depuis simulateur/main.c et depuis le harnais de tests
 * hôte. */
#include "source_etat_sim.h"

#include <stdio.h>
#include <string.h>

#include "source_etat.h"

#include "boucle_cycle.h"
#include "etat_store.h"
#include "journal.h"
#include "liaison.h"

static const char *TAG = "source_etat_sim";

/* Mêmes valeurs et même raison d'être que BOUCLE_FILE_PROFONDEUR/
 * BOUCLE_ACTION_MAX/BOUCLE_ARGUMENTS_MAX dans core/boucle.c : profondeur 4
 * pour absorber une rafale de boutons pressés pendant un cycle, tampons
 * assez larges pour "arret_urgence" et pour un futur arguments_json. Non
 * partagées avec boucle.c (qui reste privé à sa propre traduction) : ce
 * fichier n'a pas vocation à dépendre de constantes internes d'un module
 * qu'il n'inclut jamais. */
#define FILE_PROFONDEUR 4
#define ACTION_MAX       32
#define ARGUMENTS_MAX   128

typedef struct {
    char action[ACTION_MAX];
    char arguments_json[ARGUMENTS_MAX];
    bool avec_arguments;
} commande_t;

static etat_store_t          g_store;
static liaison_t             g_liaison;
static const backend_desc_t *g_desc = NULL;
static bool                  g_demarre = false;

/* File circulaire à taille fixe, sans allocation : profondeur bornée comme
 * la file FreeRTOS de boucle.c, sans avoir besoin de xQueueCreate() côté PC. */
static commande_t g_file[FILE_PROFONDEUR];
static size_t      g_file_tete = 0;
static size_t      g_file_taille = 0;

/* Tâche 9 : miroir mono-thread de g_echec_action/g_echec_en_attente dans
 * core/boucle.c -- même contrat, sans mutex puisque rien ici ne peut être
 * préempté entre deux instructions (voir le commentaire de tête de ce
 * fichier). */
static char g_echec_action[ACTION_MAX];
static bool g_echec_en_attente = false;

static bool file_pousser(const commande_t *cmd)
{
    if (g_file_taille >= FILE_PROFONDEUR) {
        return false;
    }
    size_t indice = (g_file_tete + g_file_taille) % FILE_PROFONDEUR;
    g_file[indice] = *cmd;
    g_file_taille++;
    return true;
}

static bool file_depiler(commande_t *dest)
{
    if (g_file_taille == 0) {
        return false;
    }
    *dest = g_file[g_file_tete];
    g_file_tete = (g_file_tete + 1) % FILE_PROFONDEUR;
    g_file_taille--;
    return true;
}

bool source_etat_sim_demarrer(const backend_desc_t *desc)
{
    if (desc == NULL || desc->demarrer == NULL || desc->rafraichir == NULL ||
        desc->arreter == NULL || desc->commande == NULL) {
        JOURNAL_ERREUR(TAG, "source_etat_sim_demarrer : descripteur incomplet");
        return false;
    }
    if (g_demarre) {
        JOURNAL_ALERTE(TAG, "source_etat_sim_demarrer appele une seconde fois ; ignore");
        return false;
    }
    if (!etat_store_init(&g_store, desc->taille_etat)) {
        JOURNAL_ERREUR(TAG, "etat_store_init a echoue (taille=%u)", (unsigned)desc->taille_etat);
        return false;
    }

    /* Mêmes seuils par défaut que boucle_demarrer() (voir liaison.h pour la
     * raison de ces deux nombres précis). */
    liaison_init(&g_liaison, LIAISON_SEUIL_DEGRADE_DEFAUT, LIAISON_SEUIL_HORS_LIGNE_DEFAUT);
    g_desc = desc;
    g_file_tete = 0;
    g_file_taille = 0;
    /* Fix round 1 (revue tache 9, LOW) : reinitialisation incomplete avant
     * ce fix -- la file etait remise a zero mais pas le sceau d'echec de
     * commande, laissant g_echec_en_attente survivre a un (hypothetique)
     * redemarrage. Un seul demarrage existe reellement dans ce process (voir
     * la garde g_demarre juste au-dessus), donc ce n'etait pas observable en
     * pratique -- complete quand meme pour que l'etat de depart soit
     * explicitement propre, plutot que de compter sur l'initialisation
     * statique a zero du fichier. */
    g_echec_action[0] = '\0';
    g_echec_en_attente = false;

    void *initial = etat_store_tampon_arriere(&g_store);
    esp_err_t erreur = desc->demarrer(initial, NULL);
    if (erreur != ESP_OK) {
        JOURNAL_ERREUR(TAG, "demarrer() du backend %s a echoue",
                       desc->nom != NULL ? desc->nom : "?");
        etat_store_liberer(&g_store);
        g_desc = NULL;
        return false;
    }
    etat_store_valider(&g_store);

    g_demarre = true;
    JOURNAL_INFO(TAG, "boucle simulee demarree (backend=%s)", desc->nom != NULL ? desc->nom : "?");
    return true;
}

/* Dépile et exécute toutes les commandes en attente, avant le rafraîchissement
 * du cycle — même ordre que boucle_traiter_commandes() dans core/boucle.c. */
static void traiter_commandes(void)
{
    commande_t cmd;
    while (file_depiler(&cmd)) {
        void *etat = etat_store_tampon_arriere(&g_store);
        const char *arguments = cmd.avec_arguments ? cmd.arguments_json : NULL;

        esp_err_t erreur = g_desc->commande(etat, cmd.action, arguments);
        if (erreur == ESP_OK) {
            JOURNAL_INFO(TAG, "commande %s executee", cmd.action);
        } else {
            JOURNAL_ALERTE(TAG, "commande %s en echec", cmd.action);
            /* Meme miroir que core/boucle.c, y compris la protection de
             * l'echec d'arret d'urgence (fix round 1, revue tache 9,
             * MEDIUM 1) : un seul emplacement, dernier-ecrit-gagne, SAUF
             * qu'un echec BACKEND_ACTION_URGENCE deja en attente n'est
             * jamais ecrase par l'echec d'une AUTRE action -- une rafale qui
             * draine plusieurs echecs d'affilee (la file entiere, profondeur
             * 4, sans pompage entre deux -- rien ne l'exige, voir
             * traiter_commandes() appelee une seule fois par cycle) ne doit
             * jamais faire disparaitre l'echec de l'action la plus critique
             * du firmware derriere celui d'une pause ou d'une annulation. Un
             * second echec d'urgence peut en revanche remplacer le premier
             * (meme action). Deux echecs NON-urgence dans la meme rafale
             * restent dernier-ecrit-gagne, comme avant ce fix -- seul le cas
             * urgence est protege ici. */
            bool garder_urgence_en_attente = g_echec_en_attente &&
                strcmp(g_echec_action, BACKEND_ACTION_URGENCE) == 0 &&
                strcmp(cmd.action, BACKEND_ACTION_URGENCE) != 0;
            if (!garder_urgence_en_attente) {
                snprintf(g_echec_action, sizeof(g_echec_action), "%s", cmd.action);
                g_echec_en_attente = true;
            }
        }
    }
}

void source_etat_sim_cycle(void)
{
    if (!g_demarre) {
        return;
    }

    traiter_commandes();

    bool succes = boucle_cycle(&g_store, &g_liaison, g_desc);
    if (succes) {
        etat_store_valider(&g_store);
    }
}

bool ui_etat_instantane(void *dest, size_t taille, uint32_t *generation, liaison_etat_t *liaison)
{
    if (!g_demarre || dest == NULL) {
        return false;
    }
    if (taille != g_store.taille) {
        JOURNAL_ALERTE(TAG, "ui_etat_instantane : taille %u attendue, %u recue",
                       (unsigned)g_store.taille, (unsigned)taille);
        return false;
    }

    /* Pas de verrou : mono-thread, rien ne peut permuter le magasin d'état
     * entre les trois lectures ci-dessous (contrairement à boucle_instantane()
     * côté ESP, qui protège ce même triplet contre la tâche d'interrogation
     * concurrente — voir le commentaire de g_mutex_etat dans core/boucle.c). */
    memcpy(dest, etat_store_lire(&g_store), taille);
    if (generation != NULL) {
        *generation = etat_store_generation(&g_store);
    }
    if (liaison != NULL) {
        *liaison = liaison_etat(&g_liaison);
    }
    return true;
}

esp_err_t ui_commander(const char *action, const char *arguments_json)
{
    if (!g_demarre) {
        return ESP_ERR_INVALID_STATE;
    }
    if (action == NULL || action[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(action) >= ACTION_MAX) {
        JOURNAL_ALERTE(TAG, "action trop longue pour la file (%s)", action);
        return ESP_ERR_INVALID_ARG;
    }
    if (arguments_json != NULL && strlen(arguments_json) >= ARGUMENTS_MAX) {
        JOURNAL_ALERTE(TAG, "arguments_json trop long pour la file (action=%s)", action);
        return ESP_ERR_INVALID_ARG;
    }

    commande_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    /* snprintf plutôt que strlcpy (non standard hors BSD/newlib) : ce
     * fichier est aussi lié par host-test, qui tourne sous la libc du
     * système hôte. Toujours tronqué et terminé par NUL. */
    snprintf(cmd.action, sizeof(cmd.action), "%s", action);
    if (arguments_json != NULL) {
        snprintf(cmd.arguments_json, sizeof(cmd.arguments_json), "%s", arguments_json);
        cmd.avec_arguments = true;
    }

    if (!file_pousser(&cmd)) {
        JOURNAL_ALERTE(TAG, "file de commandes pleine ; commande %s perdue", action);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ui_commande_echec(char *action, size_t taille)
{
    if (!g_demarre || action == NULL || taille == 0 || !g_echec_en_attente) {
        return false;
    }
    snprintf(action, taille, "%s", g_echec_action);
    g_echec_en_attente = false;
    return true;
}

size_t source_etat_sim_file_taille(void)
{
    return g_file_taille;
}

bool source_etat_sim_est_demarre(void)
{
    return g_demarre;
}

void source_etat_sim_reset_echec(void)
{
    g_echec_action[0] = '\0';
    g_echec_en_attente = false;
}

bool source_etat_sim_derniere_commande(char *action, size_t taille_action,
                                        char *arguments_json, size_t taille_arguments)
{
    if (action == NULL || taille_action == 0 || g_file_taille == 0) {
        return false;
    }
    /* Le dernier element POUSSE (pas le prochain a etre depile) : dernier
     * indice occupe de la file circulaire, voir file_pousser() ci-dessus
     * (g_file_tete + g_file_taille - 1, module FILE_PROFONDEUR). */
    size_t indice = (g_file_tete + g_file_taille - 1) % FILE_PROFONDEUR;
    const commande_t *cmd = &g_file[indice];
    snprintf(action, taille_action, "%s", cmd->action);
    if (arguments_json != NULL && taille_arguments > 0) {
        snprintf(arguments_json, taille_arguments, "%s", cmd->avec_arguments ? cmd->arguments_json : "");
    }
    return true;
}
