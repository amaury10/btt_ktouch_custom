/* Sous-projet "panneaux KlipperScreen", tache 4 : l'ecran Bed Level -- quatre
 * boutons (Screws Adjust/Z-Tilt/QGL/Disable Motors) vers
 * klipper_gcode_niveau_lit() (tache 1). Construction directe (calloc du
 * contexte a la taille du descripteur, puis ECRAN_NIVEAU_LIT.construire())
 * plutot que navigation_empiler() -- meme choix que
 * test_ecran_zcalibrate.c/test_ecran_reglage_fin.c, pour la meme raison
 * (tester uniquement le contrat de cet ecran).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_zcalibrate()/suite_ecran_reglage_fin(),
 * ce fichier trace le meme seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_niveau_lit.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_niveau_lit(void)
{
    printf("suite : ecran niveau lit\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_niveau_lit() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_NIVEAU_LIT.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_niveau_lit_ctx_t *ctx = (ecran_niveau_lit_ctx_t *)brut;
    ECRAN_NIVEAU_LIT.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    for (int i = 0; i < ECRAN_NIVEAU_LIT_BOUTON_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
    }

    /* --- aucune mise a jour : envoi pur, jamais de grisage (voir le
     * commentaire de tete du .h). ------------------------------------------ */
    VERIFIER(ECRAN_NIVEAU_LIT.mettre_a_jour == NULL);

    char action[32];
    char arguments[192];

    /* --- Screws Adjust -> SCREWS_TILT_CALCULATE. -------------------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_NIVEAU_LIT_BOUTON_SCREWS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"SCREWS_TILT_CALCULATE\"") != NULL);
    source_etat_sim_cycle();

    /* --- Z-Tilt -> Z_TILT_ADJUST. ------------------------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_NIVEAU_LIT_BOUTON_ZTILT], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"Z_TILT_ADJUST\"") != NULL);
    source_etat_sim_cycle();

    /* --- QGL -> QUAD_GANTRY_LEVEL. ------------------------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_NIVEAU_LIT_BOUTON_QGL], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"QUAD_GANTRY_LEVEL\"") != NULL);
    source_etat_sim_cycle();

    /* --- Disable Motors -> M84. --------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_NIVEAU_LIT_BOUTON_DISABLE], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"M84\"") != NULL);
    source_etat_sim_cycle();

    lv_obj_delete(parent);
    free(brut);
}
