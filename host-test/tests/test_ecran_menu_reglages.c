/* Sous-projet "panneaux KlipperScreen", tache 8 : le sous-menu Configuration
 * (ECRAN_MENU_REGLAGES, id "menu_reglages" -- PAS ECRAN_CONFIGURATION, voir
 * ecran_menu_reglages.h pour la note de collision de noms) -- grille de 11
 * cases qui relient chacune un panneau de reglage distinct (Fine Tune est
 * parti vers ecran_accueil.c, sous-projet "refonte IHM KlipperScreen" tache 5
 * -- voir test_ecran_accueil.c).
 *
 * Deux parties, meme technique que test_ecran_accueil_hub.c (voir son
 * commentaire de tete pour le detail du choix) : la premiere construit
 * ECRAN_MENU_REGLAGES directement (calloc du contexte a la taille du
 * descripteur) pour prouver que `construire()` cree bien les 11 boutons avec
 * les bons libelles ; la seconde empile REELLEMENT ECRAN_MENU_REGLAGES via
 * navigation_empiler() et clique sur chaque case pour prouver que le rappel
 * attache empile bien le panneau attendu (et depile a nouveau). Une
 * troisieme partie prouve que la tuile "Configuration" du hub
 * (ecran_accueil_hub.c, sous-projet "refonte IHM KlipperScreen" tache 4,
 * main_panel) navigue reellement vers ECRAN_MENU_REGLAGES -- couverture
 * conservee ici par coherence historique (tache 8) meme si
 * test_ecran_accueil_hub.c verifie deja les cinq tuiles du hub, dont
 * celle-ci, de facon exhaustive.
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_accueil_hub(), meme si ce fichier n'envoie
 * en realite aucun gcode (tous les rappels de clic ici ne font que
 * navigation_empiler()) -- garde conservee par coherence avec le reste de ce
 * depot plutot que par necessite stricte. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_accueil_hub.h"
#include "ecran_menu_reglages.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* Une entree par case, ORDRE = ordre de remplissage de la grille (voir
 * ECRAN_MENU_REGLAGES_CASE_* dans ecran_menu_reglages.h) : libelle attendu +
 * id de l'ecran que le clic doit empiler -- verifie a la fois la partie 1
 * (construction, libelles) et la partie 2 (navigation reelle) ci-dessous,
 * une seule table plutot que 24 lignes dupliquees. */
static const struct {
    const char *libelle;
    const char *id_cible;
} CASES_ATTENDUES[ECRAN_MENU_REGLAGES_NB] = {
    [ECRAN_MENU_REGLAGES_CASE_ZCALIBRATE]    = { "Z Calibrate",   "zcalibrate" },
    [ECRAN_MENU_REGLAGES_CASE_BED_LEVEL]     = { "Bed Level",     "niveau_lit" },
    [ECRAN_MENU_REGLAGES_CASE_LIMITS]        = { "Limits",        "limites" },
    [ECRAN_MENU_REGLAGES_CASE_RETRACTION]    = { "Retraction",    "retraction" },
    [ECRAN_MENU_REGLAGES_CASE_NETWORK]       = { "Network",       "wifi" },
    [ECRAN_MENU_REGLAGES_CASE_POWER]         = { "Power",         "power" },
    [ECRAN_MENU_REGLAGES_CASE_BED_MESH]      = { "Bed Mesh",      "bed_mesh" },
    [ECRAN_MENU_REGLAGES_CASE_INPUT_SHAPER]  = { "Input Shaper",  "input_shaper" },
    [ECRAN_MENU_REGLAGES_CASE_SPOOLMAN]      = { "Spoolman",      "spoolman" },
    [ECRAN_MENU_REGLAGES_CASE_UPDATER]       = { "Updater",       "updater" },
    [ECRAN_MENU_REGLAGES_CASE_CONSOLE]       = { "Console",       "console" },
};

void suite_ecran_menu_reglages(void)
{
    printf("suite : ecran menu reglages\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_menu_reglages() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* ---------------------------------------------------------------------
     * Partie 1 : construction directe -- les 11 widgets sont crees, avec
     * les bons libelles. --------------------------------------------------- */
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_MENU_REGLAGES.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_menu_reglages_ctx_t *ctx = (ecran_menu_reglages_ctx_t *)brut;
    ECRAN_MENU_REGLAGES.construire(parent, ctx);

    VERIFIER(ctx->zone_grille != NULL);
    for (int i = 0; i < ECRAN_MENU_REGLAGES_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
        VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->boutons[i], 0)), CASES_ATTENDUES[i].libelle);
    }

    lv_obj_delete(parent);
    free(brut);

    /* ---------------------------------------------------------------------
     * Partie 2 : navigation reelle -- empile ECRAN_MENU_REGLAGES, clique
     * chacune des 11 cases, verifie la pile. Voir le commentaire de tete de
     * ce fichier pour pourquoi une construction separee est necessaire ici
     * (meme raison que test_ecran_accueil_hub.c). ------------------------- */
    navigation_init(lv_screen_active()); /* pile vide, meme technique defensive que les autres suites */
    VERIFIER(navigation_empiler(&ECRAN_MENU_REGLAGES) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "menu_reglages");

    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *conteneur = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(conteneur != NULL);
    /* zone_grille est le seul enfant direct du conteneur (aucune zone de
     * temperature au-dessus, contrairement au hub -- voir le commentaire de
     * tete de ecran_menu_reglages.c). */
    VERIFIER(lv_obj_get_child_count(conteneur) == 1);
    lv_obj_t *zone_grille = lv_obj_get_child(conteneur, 0);
    VERIFIER(zone_grille != NULL);
    VERIFIER(lv_obj_get_child_count(zone_grille) == ECRAN_MENU_REGLAGES_NB);

    for (int i = 0; i < ECRAN_MENU_REGLAGES_NB; i++) {
        lv_obj_t *bouton = lv_obj_get_child(zone_grille, i);
        VERIFIER(bouton != NULL);
        VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton, 0)), CASES_ATTENDUES[i].libelle);

        lv_obj_send_event(bouton, LV_EVENT_CLICKED, NULL);
        VERIFIER(navigation_profondeur() == 2);
        VERIFIER_TEXTE(navigation_id_courant(), CASES_ATTENDUES[i].id_cible);

        navigation_depiler();
        VERIFIER(navigation_profondeur() == 1);
        VERIFIER_TEXTE(navigation_id_courant(), "menu_reglages");
    }

    /* ---------------------------------------------------------------------
     * Partie 3 : la tuile "Configuration" du hub (ecran_accueil_hub.c,
     * main_panel) navigue reellement vers ECRAN_MENU_REGLAGES -- meme
     * technique de parcours/clic que test_ecran_accueil_hub.c pour ses
     * autres tuiles. --------------------------------------------------- */
    navigation_init(lv_screen_active());
    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL_HUB) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);

    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    navigation_mettre_a_jour(&etat, false);

    lv_obj_t *ecran_hub = lv_screen_active();
    lv_obj_t *conteneur_hub = lv_obj_get_child(ecran_hub, lv_obj_get_child_count(ecran_hub) - 1);
    VERIFIER(conteneur_hub != NULL);
    lv_obj_t *zone_menu = lv_obj_get_child(conteneur_hub, lv_obj_get_child_count(conteneur_hub) - 1);
    VERIFIER(zone_menu != NULL);
    lv_obj_t *bouton_configuration = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION);
    VERIFIER(bouton_configuration != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_configuration, 0)), "Configuration");

    lv_obj_send_event(bouton_configuration, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "menu_reglages");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);
}
