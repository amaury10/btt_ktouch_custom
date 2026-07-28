/* Point d'entrée du simulateur : construit l'habillage (tâche 4 — barre
 * d'état, bandeau de notifications) et l'écran de départ -- ECRAN_ACCUEIL
 * (tâche 6) ou ECRAN_CONFIGURATION (tâche 8, --scenario 7/8, voir plus bas) --
 * fait tourner la boucle simulée (source_etat_sim.c) contre backend_factice
 * (ou un backend qui échoue toujours, pour démontrer l'état dégradé/hors
 * ligne), puis soit capture le tout en PNG, soit l'affiche dans une fenêtre
 * SDL interactive.
 *
 * C'est ici que la chaîne complète tourne pour la première fois : backend
 * factice -> boucle_cycle() -> magasin d'état -> génération -> ECRAN_ACCUEIL
 * (voir apps/klipper/ecrans/ecran_accueil.h). Jusqu'à la tâche 5, ce fichier
 * assemblait un écran jouet local (ECRAN_DEMO) pour prouver que l'habillage
 * transmet réellement etat/generation/liaison à un écran empilé ; la tâche 6
 * le remplace par l'écran réel plutôt que de garder les deux, exactement
 * comme ce fichier avait lui-même remplacé la mire de vérification de la
 * tâche 1 (voir task-1-report.md) — un écran jouet qui a rempli son rôle une
 * fois ne mérite pas un second indicateur de ligne de commande pour choisir
 * entre lui et l'écran réel. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "afficheur.h"
#include "lvgl.h"

#include "backend.h"
#include "backend_factice.h"
#include "clavier.h"
#include "confirmation.h"
#include "ecran_accueil.h"
#include "ecran_configuration.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* --- Backend qui échoue systématiquement -------------------------------- *
 * Sert uniquement à démontrer, dans le simulateur, l'état dégradé/hors
 * ligne de l'habillage : backend_factice.c ne produit jamais d'échec (voir
 * son en-tête), donc rien ne fait naturellement progresser liaison_t au-delà
 * de LIAISON_EN_LIGNE. Ce backend-jouet, local à ce fichier, ne touche
 * jamais le réseau — il rend ESP_FAIL immédiatement — et laisse
 * boucle_cycle()/liaison.c (le vrai code, pas une simulation de leur effet)
 * faire progresser la liaison vers DEGRADEE (3 échecs) puis HORS_LIGNE (10
 * échecs) exactement comme le ferait un hôte injoignable sur cible. */
static esp_err_t echec_demarrer(void *etat, const backend_hote_t *hote)
{
    (void)hote;
    memset(etat, 0, sizeof(etat_klipper_t));
    return ESP_OK;
}

static esp_err_t echec_rafraichir(void *etat)
{
    (void)etat;
    return ESP_FAIL;
}

static void echec_arreter(void *etat)
{
    (void)etat;
}

static esp_err_t echec_commande(void *etat, const char *action, const char *arguments_json)
{
    (void)etat;
    (void)action;
    (void)arguments_json;
    return ESP_ERR_NOT_SUPPORTED;
}

static const backend_desc_t BACKEND_ECHEC_DESC = {
    .nom = "echec-demo",
    .taille_etat = sizeof(etat_klipper_t),
    .demarrer = echec_demarrer,
    .rafraichir = echec_rafraichir,
    .arreter = echec_arreter,
    .commande = echec_commande,
};

/* --- Démonstration du clavier modal et du dialogue de confirmation --------
 * (tâche 7) : --scenario 5/6 ci-dessous, en mode capture uniquement. Ces
 * rappels ne sont jamais invoqués par une capture hors écran (rien n'y
 * simule un appui tactile, voir --scenario ci-dessous et host-test/tests/
 * test_clavier.c pour la façon dont les événements sont simulés côté tests) ;
 * ils n'existent que pour satisfaire la signature de clavier_ouvrir()/
 * confirmation_ouvrir(), qui refusent un rappel NULL (voir clavier.h). */
static void demo_clavier_rappel(const char *valeur, void *contexte)
{
    (void)valeur;
    (void)contexte;
}

static void demo_confirmation_rappel(bool confirme, void *contexte)
{
    (void)confirme;
    (void)contexte;
}

int main(int argc, char **argv)
{
    const char *chemin_capture = NULL;
    int cycles = 5;
    int scenario = 1;
    bool echec = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            chemin_capture = argv[++i];
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--echec") == 0) {
            echec = true;
        }
    }

    afficheur_mode_t mode = (chemin_capture != NULL) ? AFFICHEUR_HORS_ECRAN : AFFICHEUR_FENETRE;
    if (!afficheur_demarrer(mode)) {
        fprintf(stderr, "echec du demarrage de l'afficheur (mode %s)\n",
                mode == AFFICHEUR_FENETRE ? "fenetre" : "hors ecran");
        return 1;
    }

    lv_obj_t *racine = lv_screen_active();
    habillage_construire(racine);
    /* --scenario 7/8 (tâche 8) : empile ECRAN_CONFIGURATION PAR-DESSUS
     * ECRAN_ACCUEIL (jamais seul), exactement la topologie que app_main.c
     * construit sur un appareil jamais configuré (reglages_configures()
     * faux) -- voir README §Options pour la numérotation complète. Empiler
     * ECRAN_CONFIGURATION seul (comme ce fichier le faisait avant la revue de
     * la tâche 8, round 1, Q1) rendrait son bouton Save un cul-de-sac :
     * navigation_accueil() est un no-op à profondeur 1, Save n'aurait donc
     * aucun endroit où revenir. Ces deux numéros ne correspondent à aucun
     * scénario du backend factice (voir backend_factice_scenario() plus bas,
     * qui les traite comme "tout autre numéro", exactement comme 5/6 déjà). */
    navigation_empiler(&ECRAN_ACCUEIL);
    bool ecran_config = (scenario == 7 || scenario == 8);
    if (ecran_config) {
        navigation_empiler(&ECRAN_CONFIGURATION);
    }

    const backend_desc_t *backend = echec ? &BACKEND_ECHEC_DESC : backend_factice_desc();
    if (!echec) {
        backend_factice_scenario(scenario);
    }
    if (!source_etat_sim_demarrer(backend)) {
        fprintf(stderr, "echec du demarrage de la boucle simulee\n");
    }

    /* --scenario 9 (tache 9, mode capture uniquement) : demontre l'echec
     * ASYNCHRONE d'une commande -- ui_commander() l'accepte tout de suite
     * (ESP_OK), mais son execution reelle, plus tard par la boucle simulee,
     * echoue deliberement (backend_factice_commande_echoue(true), tache 9) --
     * exactement le chemin qu'un simple appui bouton ne peut pas montrer ici
     * (aucune simulation d'entree tactile en mode capture, voir le
     * commentaire de tete de ce fichier). Empilee AVANT la boucle de cycles
     * ci-dessous : le premier appel a source_etat_sim_cycle() l'execute et la
     * fait echouer, et le habillage_pomper() du meme tour de boucle remonte
     * cet echec au bandeau de notification (voir ui_commande_echec() dans
     * ui/source_etat.h et son polling par habillage_pomper()) -- sans
     * attendre le "host connected" que --cycles positif declenche par
     * ailleurs plus bas (voir la note sur ce bandeau-la). Ne correspond a
     * aucun scenario du backend factice (retombe sur le comportement du
     * scenario 3, "printing" plausible -- voir backend_factice_scenario()) :
     * Pause a un sens contre un etat "printing", meme si aucun champ de cet
     * etat n'influence l'echec force lui-meme. */
    if (scenario == 9) {
        backend_factice_commande_echoue(true);
        esp_err_t erreur_commande = ui_commander(BACKEND_ACTION_PAUSE, NULL);
        if (erreur_commande != ESP_OK) {
            /* esp_err_to_name() n'existe pas dans shim/esp_err.h (doublure
             * minimale, voir son commentaire de tete) : le code numerique
             * suffit ici, ce n'est qu'un message de diagnostic sur stderr. */
            fprintf(stderr, "scenario 9 : ui_commander a refuse la commande (code %d)\n",
                    (int)erreur_commande);
        }
    }

    if (chemin_capture != NULL) {
        /* --cycles fait avancer la boucle simulée d'autant de "secondes"
         * avant la capture : un cycle = un rafraîchissement du backend +
         * validation du magasin d'état, exactement ce que ferait
         * boucle_tache() une fois par seconde sur cible (voir
         * source_etat_sim.c). */
        for (int i = 0; i < cycles; i++) {
            source_etat_sim_cycle();
            habillage_pomper();
        }
        if (cycles == 0) {
            habillage_pomper();
        }
        /* scenario 9 exclu : la boucle ci-dessus a deja fait remonter, via
         * habillage_pomper(), le bandeau d'echec de commande que ce scenario
         * existe pour capturer -- un "host connected" ecrirait par-dessus
         * (habillage_notifier() REMPLACE, jamais n'empile, voir son
         * commentaire dans habillage.h) avant meme la capture. */
        if (cycles > 0 && scenario != 9) {
            habillage_notifier(echec ? "connection lost" : "host connected", echec);
        }

        /* Démonstration tâche 7 : --scenario 5/6 ouvre respectivement le
         * clavier modal et le dialogue de confirmation par-dessus l'écran
         * d'accueil déjà construit ci-dessus, pour que la capture qui suit
         * les montre réellement à l'écran (spec §6 : le plus gros morceau
         * partagé du jalon, jusqu'ici invisible dans le simulateur). Ces
         * numéros ne correspondent à aucun scénario du backend factice (voir
         * backend_factice_scenario() plus haut, qui les a déjà reçus et
         * traités comme "tout autre numéro", README §Options) : le fond
         * derrière les modales est donc celui du scénario 3 (valeurs
         * extrêmes mais plausibles), un arrière-plan quelconque puisque
         * seule la modale elle-même importe pour cette capture. */
        if (scenario == 5) {
            clavier_ouvrir("Host address", "192.168.1.42", CLAVIER_TEXTE, demo_clavier_rappel, NULL);
        } else if (scenario == 6) {
            /* confirmation_ouvrir_ex(), pas confirmation_ouvrir() : reprend
             * EXACTEMENT la copie que le vrai bouton Cancel envoie depuis
             * ecran_accueil.c (fix round 1, revue tache 9, LOW) -- un declin
             * par defaut "Cancel" ferait deux boutons qui commencent tous les
             * deux par le meme mot a cote de l'action "Cancel print". */
            confirmation_ouvrir_ex("Cancel print?",
                                    "This will stop the current print. This cannot be undone.",
                                    "Cancel print", true, "Keep printing", demo_confirmation_rappel, NULL);
        } else if (scenario == 8) {
            /* Tâche 8 : clavier ouvert par-dessus ECRAN_CONFIGURATION (déjà
             * empilé plus haut, voir `ecran_config`), avec une adresse
             * pré-remplie comme si elle venait d'être saisie -- même
             * technique que le scénario 5 sur l'écran d'accueil : la valeur
             * initiale de clavier_ouvrir() apparaît dans la textarea
             * exactement comme une saisie tactile l'aurait laissée. */
            clavier_ouvrir("Printer address", "192.168.1.42", CLAVIER_TEXTE, demo_clavier_rappel, NULL);
        }

        /* Un cycle de pompe LVGL suffit à laisser rendre l'écran une
         * première fois avant la capture (délai nul : aucune animation
         * n'est en jeu ici, voir l'ancienne mire de la tâche 1). */
        afficheur_pomper(0);
        if (!afficheur_capturer(chemin_capture)) {
            fprintf(stderr, "echec de la capture vers %s\n", chemin_capture);
            afficheur_arreter();
            return 1;
        }
        printf("capture ecrite : %s (%dx%d)\n", chemin_capture, AFFICHEUR_LARGEUR, AFFICHEUR_HAUTEUR);
        afficheur_arreter();
        return 0;
    }

    /* Mode fenêtre : boucle jusqu'à fermeture (Ctrl+C ou fermeture de la
     * fenêtre par le gestionnaire de fenêtres). Un cycle de boucle simulée
     * par seconde écoulée, habillage_pomper() à chaque image — même ordre
     * que le brief impose pour la démonstration du simulateur. */
    uint32_t accumulateur_ms = 0;
    for (;;) {
        afficheur_pomper(16);
        habillage_pomper();
        usleep(16 * 1000);
        accumulateur_ms += 16;
        if (accumulateur_ms >= 1000) {
            source_etat_sim_cycle();
            accumulateur_ms = 0;
        }
    }
}
