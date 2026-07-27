#include "backend_factice.h"

#include <string.h>

#include "journal.h"

/* Étiquette de journalisation : convention reprise du reste du firmware
 * (voir app_main.c, rescue.c), pour que /log reste lisible par module. */
static const char *TAG = "backend_factice";

/* Scénario courant, choisi par backend_factice_scenario(). Un seul backend
 * factice tourne à la fois dans le socle : un compteur statique suffit, et
 * évite au descripteur de porter un état qui n'a pas sa place dans
 * etat_klipper_t. */
static int g_scenario = 0;

/* Progression du scénario 1, portée ICI plutôt que relue depuis `etat` : le
 * socle remet `etat` à zéro avant CHAQUE appel de rafraichir() (voir le
 * contrat documenté sur backend_desc_t::rafraichir dans backend.h), donc
 * `e->progression` y vaut toujours 0 en entrée — un fait qui n'était pas
 * documenté avant la revue de fin de jalon 2a, et que ce fichier lisait à
 * l'envers (CRITICAL 1) : la progression restait figée à
 * FACTICE_PAS_PROGRESSION pour toujours, etat_store_valider() ne détectait
 * plus aucun changement après le premier cycle, et generation cessait
 * d'avancer. Un compteur statique de fichier, comme g_scenario juste
 * au-dessus, est la façon correcte de porter un état d'un appel à l'autre. */
static float g_progression_scenario1 = 0.0f;

/* Durée totale supposée du scénario 1, choisie arbitrairement mais fixe :
 * elle permet de recalculer temps_restant_s de façon cohérente avec la
 * progression, sans jamais tirer de nombre aléatoire. */
#define FACTICE_DUREE_IMPRESSION_S 3600u

/* Pas d'avancement de la progression à chaque rafraîchissement du scénario 1.
 * Une valeur ronde, assez grande pour qu'un test la voie bouger en deux
 * appels, assez petite pour laisser un simulateur regarder une impression
 * progresser sur plusieurs secondes. */
#define FACTICE_PAS_PROGRESSION 0.01f

void backend_factice_scenario(int numero)
{
    g_scenario = numero;
}

static esp_err_t backend_factice_demarrer(void *etat, const backend_hote_t *hote)
{
    etat_klipper_t *e = (etat_klipper_t *)etat;
    /* Remise à zéro défensive : le brief ne l'exige pas (le socle est censé
     * fournir un état déjà nul), mais un backend qui ne suppose rien de l'état
     * qu'on lui tend est moins susceptible de propager un débris d'une session
     * précédente si un futur appelant oublie de le faire. Ce n'est pas un
     * oubli du contrat « demarrer ne fait que journaliser » : c'est un ajout
     * volontaire, au-delà du minimum. */
    memset(e, 0, sizeof(*e));
    JOURNAL_INFO(TAG, "demarrage (hote=%s port=%u)",
                 hote != NULL ? hote->adresse : "?",
                 hote != NULL ? (unsigned)hote->port : 0u);
    return ESP_OK;
}

static esp_err_t backend_factice_rafraichir(void *etat)
{
    etat_klipper_t *e = (etat_klipper_t *)etat;
    etat_klipper_t nouveau;
    memset(&nouveau, 0, sizeof(nouveau));

    switch (g_scenario) {
    case 0:
        /* Repos : rien n'imprime, rien ne chauffe. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "standby");
        break;

    case 1: {
        /* Impression en cours : la progression avance à partir de sa valeur
         * précédente, portée par g_progression_scenario1 (fichier-statique,
         * voir sa déclaration plus haut) — jamais relue depuis `etat`, que le
         * socle remet à zéro avant cet appel. Pas de compteur caché "de
         * plus" au sens propre du terme : c'est le seul état que porte ce
         * scénario, et surtout pas de rand(). Au-delà de 1.0 elle reboucle à
         * 0, comme une nouvelle impression qui démarrerait. */
        g_progression_scenario1 += FACTICE_PAS_PROGRESSION;
        if (g_progression_scenario1 > 1.0f) {
            g_progression_scenario1 = 0.0f;
        }
        float progression = g_progression_scenario1;

        snprintf(nouveau.etat, sizeof(nouveau.etat), "printing");
        nouveau.buse_actuelle = 210.0f;
        nouveau.buse_consigne = 210.0f;
        nouveau.plateau_actuel = 60.0f;
        nouveau.plateau_consigne = 60.0f;
        snprintf(nouveau.fichier, sizeof(nouveau.fichier), "piece_test.gcode");
        nouveau.progression = progression;
        nouveau.temps_restant_s =
            (uint32_t)((1.0f - progression) * (float)FACTICE_DUREE_IMPRESSION_S + 0.5f);
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = false;
        break;
    }

    case 2:
        /* Pause : l'impression existe toujours mais n'avance plus. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "paused");
        nouveau.buse_actuelle = 210.0f;
        nouveau.buse_consigne = 210.0f;
        nouveau.plateau_actuel = 60.0f;
        nouveau.plateau_consigne = 60.0f;
        snprintf(nouveau.fichier, sizeof(nouveau.fichier), "piece_test.gcode");
        nouveau.progression = 0.5f;
        nouveau.temps_restant_s = FACTICE_DUREE_IMPRESSION_S / 2u;
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = true;
        break;

    case 3:
    default:
        /* Valeurs extrêmes : nom de fichier au maximum de sa capacité et
         * température de buse largement au-dessus du raisonnable, pour
         * vérifier qu'un affichage ne déborde nulle part. Sert aussi de
         * scénario par défaut pour tout numéro inconnu — mieux vaut une
         * valeur voyante qu'un comportement silencieusement indéfini. */
        snprintf(nouveau.etat, sizeof(nouveau.etat), "printing");
        memset(nouveau.fichier, 'x', KLIPPER_FICHIER_MAX - 1);
        nouveau.fichier[KLIPPER_FICHIER_MAX - 1] = '\0';
        nouveau.buse_actuelle = 350.0f;
        nouveau.buse_consigne = 350.0f;
        nouveau.plateau_actuel = 150.0f;
        nouveau.plateau_consigne = 150.0f;
        nouveau.progression = 0.99f;
        nouveau.temps_restant_s = KLIPPER_TEMPS_RESTANT_MAX_S;
        nouveau.impression_en_cours = true;
        nouveau.impression_en_pause = false;
        break;
    }

    *e = nouveau;
    return ESP_OK;
}

static void backend_factice_arreter(void *etat)
{
    (void)etat;
    JOURNAL_INFO(TAG, "arret");
}

static esp_err_t backend_factice_commande(void *etat, const char *action,
                                           const char *arguments_json)
{
    (void)etat;
    (void)arguments_json;

    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0 ||
        strcmp(action, BACKEND_ACTION_REPRENDRE) == 0 ||
        strcmp(action, BACKEND_ACTION_ANNULER) == 0 ||
        strcmp(action, BACKEND_ACTION_URGENCE) == 0) {
        JOURNAL_INFO(TAG, "commande %s", action);
        return ESP_OK;
    }

    /* Une action inconnue doit échouer fort et explicitement, pour que
     * l'interface puisse griser un bouton en connaissant la raison — jamais
     * l'ignorer en silence. */
    JOURNAL_ALERTE(TAG, "commande inconnue %s", action);
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t g_backend_factice_desc = {
    .nom = "factice",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = backend_factice_demarrer,
    .rafraichir = backend_factice_rafraichir,
    .arreter = backend_factice_arreter,
    .commande = backend_factice_commande,
};

const backend_desc_t *backend_factice_desc(void)
{
    return &g_backend_factice_desc;
}
