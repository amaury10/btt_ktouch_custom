/* Sous-projet "refonte IHM KlipperScreen", tache 3 : l'ecran Homing -- quatre
 * boutons (Home All/X/Y/Z) vers klipper_gcode_home() (existant). Construction
 * directe (calloc du contexte a la taille du descripteur, puis
 * ECRAN_HOMING.construire()) plutot que navigation_empiler() -- meme choix
 * que test_ecran_niveau_lit.c, pour la meme raison (tester uniquement le
 * contrat de cet ecran).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_niveau_lit(), ce fichier trace le meme
 * seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_homing.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_homing(void)
{
    printf("suite : ecran homing\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_homing() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_HOMING.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_homing_ctx_t *ctx = (ecran_homing_ctx_t *)brut;
    ECRAN_HOMING.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    for (int i = 0; i < ECRAN_HOMING_BOUTON_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
    }

    /* --- aucune mise a jour : envoi pur, jamais de grisage (voir le
     * commentaire de tete du .h). ------------------------------------------ */
    VERIFIER(ECRAN_HOMING.mettre_a_jour == NULL);

    char action[32];
    char arguments[192];

    /* --- Home All -> G28 (sans axe). --------------------------------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_HOMING_BOUTON_ALL], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"G28\"") != NULL);
    source_etat_sim_cycle();

    /* --- Home X -> G28 X. ---------------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_HOMING_BOUTON_X], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"G28 X\"") != NULL);
    source_etat_sim_cycle();

    /* --- Home Y -> G28 Y. ---------------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_HOMING_BOUTON_Y], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"G28 Y\"") != NULL);
    source_etat_sim_cycle();

    /* --- Home Z -> G28 Z. ---------------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_HOMING_BOUTON_Z], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"G28 Z\"") != NULL);
    source_etat_sim_cycle();

    lv_obj_delete(parent);
    free(brut);
}
