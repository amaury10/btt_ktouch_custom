/* Sous-projet "refonte IHM KlipperScreen", tache 2 : le sous-menu Actions
 * (ECRAN_ACTIONS, id "actions") -- grille de 7 cases, six qui empilent un
 * panneau deja construit par un jalon precedent (Move/Extrude/Fan/
 * Temperature/Macros/Console) et une septieme ("Disable Motors") qui envoie
 * M84 directement, sans confirmation (voir ecran_actions.h).
 *
 * Deux parties, meme technique que test_ecran_menu_reglages.c (voir son
 * commentaire de tete pour le detail du choix) : la premiere construit
 * ECRAN_ACTIONS directement (calloc du contexte a la taille du descripteur)
 * pour prouver que `construire()` cree bien les 7 boutons avec les bons
 * libelles ; la seconde empile REELLEMENT ECRAN_ACTIONS via
 * navigation_empiler() et clique sur chaque case de navigation pour prouver
 * que le rappel attache empile bien le panneau attendu (et depile a
 * nouveau), puis clique "Disable Motors" pour prouver qu'il emet M84 SANS
 * toucher la pile de navigation (meme trace de seam ui_commander() ->
 * source_etat_sim que test_ecran_niveau_lit.c).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_menu_reglages()/suite_ecran_niveau_lit(). */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "confirmation.h" /* case Restart Klipper (2026-08-15) : tap -> confirmation -> gcode */
#include "ecran_actions.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* Une entree par case de navigation (les six premieres, voir
 * ecran_actions.h) : libelle attendu + id de l'ecran que le clic doit
 * empiler -- verifie a la fois la partie 1 (construction, libelles) et la
 * partie 2 (navigation reelle) ci-dessous, une seule table plutot que 12
 * lignes dupliquees. "Disable Motors" (index ECRAN_ACTIONS_CASE_DISABLE)
 * n'y figure pas : ce n'est pas une navigation, verifie a part plus bas. */
static const struct {
    const char *libelle;
    const char *id_cible;
} CASES_NAV_ATTENDUES[ECRAN_ACTIONS_NB] = {
    [ECRAN_ACTIONS_CASE_MOVE]    = { "Move",        "deplacer" },
    [ECRAN_ACTIONS_CASE_EXTRUDE] = { "Extrude",     "extruder" },
    [ECRAN_ACTIONS_CASE_FAN]     = { "Fan",         "ventilateurs" },
    [ECRAN_ACTIONS_CASE_TEMP]    = { "Temperature", "temperatures" },
    [ECRAN_ACTIONS_CASE_MACROS]  = { "Macros",      "macros" },
    [ECRAN_ACTIONS_CASE_DISABLE] = { "Disable Motors", NULL }, /* pas une navigation */
    [ECRAN_ACTIONS_CASE_CONSOLE] = { "Console",     "console" },
    /* Huitieme case (2026-08-15) : FIRMWARE_RESTART derriere confirmation --
     * pas une navigation non plus, voir ecran_actions.h. */
    [ECRAN_ACTIONS_CASE_RESTART] = { "Restart Klipper", NULL },
};

void suite_ecran_actions(void)
{
    printf("suite : ecran actions\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_actions() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* ---------------------------------------------------------------------
     * Partie 1 : construction directe -- les 7 widgets sont crees, avec les
     * bons libelles. ------------------------------------------------------- */
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACTIONS.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_actions_ctx_t *ctx = (ecran_actions_ctx_t *)brut;
    ECRAN_ACTIONS.construire(parent, ctx);

    VERIFIER(ctx->zone_grille != NULL);
    for (int i = 0; i < ECRAN_ACTIONS_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
        VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->boutons[i], 0)), CASES_NAV_ATTENDUES[i].libelle);
    }

    /* --- aucune mise a jour : sept boutons purement statiques, aucun
     * grisage (meme discipline que ecran_menu_reglages.c/ecran_niveau_lit.c). */
    VERIFIER(ECRAN_ACTIONS.mettre_a_jour == NULL);

    lv_obj_delete(parent);
    free(brut);

    /* ---------------------------------------------------------------------
     * Partie 2 : navigation reelle -- empile ECRAN_ACTIONS, clique chacune
     * des six cases de navigation, verifie la pile. Voir le commentaire de
     * tete de ce fichier pour pourquoi une construction separee est
     * necessaire ici (meme raison que test_ecran_menu_reglages.c). --------- */
    navigation_init(lv_screen_active()); /* pile vide, meme technique defensive que les autres suites */
    VERIFIER(navigation_empiler(&ECRAN_ACTIONS) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "actions");

    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *conteneur = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(conteneur != NULL);
    /* zone_grille est le seul enfant direct du conteneur (aucune zone
     * au-dessus, meme mise en page que ecran_menu_reglages.c). */
    VERIFIER(lv_obj_get_child_count(conteneur) == 1);
    lv_obj_t *zone_grille = lv_obj_get_child(conteneur, 0);
    VERIFIER(zone_grille != NULL);
    VERIFIER(lv_obj_get_child_count(zone_grille) == ECRAN_ACTIONS_NB);

    for (int i = 0; i < ECRAN_ACTIONS_NB; i++) {
        if (i == ECRAN_ACTIONS_CASE_DISABLE || i == ECRAN_ACTIONS_CASE_RESTART) {
            continue; /* pas des navigations -- verifiees separement plus bas */
        }

        lv_obj_t *bouton = lv_obj_get_child(zone_grille, i);
        VERIFIER(bouton != NULL);
        VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton, 0)), CASES_NAV_ATTENDUES[i].libelle);

        lv_obj_send_event(bouton, LV_EVENT_CLICKED, NULL);
        VERIFIER(navigation_profondeur() == 2);
        VERIFIER_TEXTE(navigation_id_courant(), CASES_NAV_ATTENDUES[i].id_cible);

        navigation_depiler();
        VERIFIER(navigation_profondeur() == 1);
        VERIFIER_TEXTE(navigation_id_courant(), "actions");
    }

    /* ---------------------------------------------------------------------
     * Partie 3 : "Disable Motors" -- envoi PUR de M84, SANS confirmation ni
     * changement de la pile de navigation (meme trace de seam
     * ui_commander() -> source_etat_sim que test_ecran_niveau_lit.c). ------ */
    lv_obj_t *bouton_disable = lv_obj_get_child(zone_grille, ECRAN_ACTIONS_CASE_DISABLE);
    VERIFIER(bouton_disable != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_disable, 0)), "Disable Motors");

    char action[32];
    char arguments[192];
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(bouton_disable, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"M84\"") != NULL);
    source_etat_sim_cycle();

    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "actions");

    /* ---------------------------------------------------------------------
     * Partie 4 : "Restart Klipper" (2026-08-15) -- tap -> confirmation
     * OUVERTE (rien n'est envoye), confirmer -> FIRMWARE_RESTART part, pile
     * de navigation inchangee. Meme idiome msgbox que le groupe 4 de
     * test_ecran_fichiers.c (rappel de confirmation.c SYNCHRONE). --------- */
    lv_obj_t *bouton_restart = lv_obj_get_child(zone_grille, ECRAN_ACTIONS_CASE_RESTART);
    VERIFIER(bouton_restart != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_restart, 0)), "Restart Klipper");

    VERIFIER(!confirmation_est_ouverte());
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(bouton_restart, LV_EVENT_CLICKED, NULL);
    VERIFIER(confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye avant confirmation */

    lv_obj_t *calque = lv_layer_top();
    lv_obj_t *fond = lv_obj_get_child(calque, lv_obj_get_child_count(calque) - 1);
    VERIFIER(fond != NULL);
    lv_obj_t *mbox = lv_obj_get_child(fond, 0);
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Restart Klipper?");
    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    VERIFIER(pied != NULL);
    lv_obj_t *bouton_confirmer = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_confirmer != NULL);

    lv_obj_send_event(bouton_confirmer, LV_EVENT_CLICKED, NULL);
    VERIFIER(!confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"FIRMWARE_RESTART\"") != NULL);
    lv_timer_handler();      /* acheve la fermeture asynchrone du dialogue -- meme
                                raison que le groupe 4 de test_ecran_fichiers.c
                                (la suite console exige un calque superieur vide) */
    source_etat_sim_cycle();

    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "actions");

    navigation_depiler();
}
