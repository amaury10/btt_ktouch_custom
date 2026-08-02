/* Sous-projet "panneaux KlipperScreen", tache 3 : l'ecran Z Calibrate --
 * six boutons (Start Probe/Start Endstop/Raise Nozzle/Lower Nozzle/Accept/
 * Abort) vers klipper_gcode_calibration_z()/klipper_gcode_testz() (tache 1),
 * plus un selecteur de pas generique pour Raise/Lower. Construction directe
 * (calloc du contexte a la taille du descripteur, puis
 * ECRAN_ZCALIBRATE.construire()) plutot que navigation_empiler() -- meme
 * choix que test_ecran_reglage_fin.c/test_ecran_deplacer.c, pour la meme
 * raison (tester uniquement le contrat de cet ecran).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_reglage_fin()/suite_ecran_deplacer(), ce
 * fichier trace le meme seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_zcalibrate.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_zcalibrate(void)
{
    printf("suite : ecran zcalibrate\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_zcalibrate() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ZCALIBRATE.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_zcalibrate_ctx_t *ctx = (ecran_zcalibrate_ctx_t *)brut;
    ECRAN_ZCALIBRATE.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    VERIFIER(ctx->valeur_z != NULL);
    VERIFIER(ctx->valeur_probe_offset != NULL);
    for (int i = 0; i < ECRAN_ZCALIBRATE_BOUTON_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas.racine != NULL);
    for (int i = 0; i < ECRAN_ZCALIBRATE_PAS_NB; i++) {
        VERIFIER(ctx->selecteur_pas.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas.index_actif == ECRAN_ZCALIBRATE_PAS_DEFAUT); /* pas 0.01mm par defaut */
    VERIFIER(ctx->label_pas != NULL);

    /* --- placeholder Probe Offset : fixe des la construction, jamais
     * modifie par mettre_a_jour() (voir le commentaire de tete du .h). ---- */
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_probe_offset), "Probe Offset: -");

    /* --- mettre_a_jour() : "Z: -" tant que l'axe Z n'est pas reference --- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    ECRAN_ZCALIBRATE.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_z), "Z: -");

    /* --- Z reference : "Z: 1.84" (position[2], deux decimales fixes). ---- */
    etat.axes_references = (1u << 2);
    etat.position[2] = 1.84f;
    ECRAN_ZCALIBRATE.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_z), "Z: 1.84");

    char action[32];
    char arguments[192];

    /* --- Start Probe -> PROBE_CALIBRATE. --------------------------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_START_PROBE], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"PROBE_CALIBRATE\"") != NULL);
    source_etat_sim_cycle();

    /* --- Start Endstop -> Z_ENDSTOP_CALIBRATE. --------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_START_ENDSTOP], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "\"script\":\"Z_ENDSTOP_CALIBRATE\"") != NULL);
    source_etat_sim_cycle();

    /* --- Raise Nozzle, pas 0.01mm par defaut -> TESTZ Z=+0.01. ------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_RAISE], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "TESTZ Z=+0.01") != NULL);
    source_etat_sim_cycle();

    /* --- Lower Nozzle, meme pas -> TESTZ Z=-0.01. -------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_LOWER], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "TESTZ Z=-0.01") != NULL);
    source_etat_sim_cycle();

    /* --- changement de pas -- index 1 (=0.05mm), brief : scenario de test
     * explicite ("clic Raise Nozzle (pas .05) -> TESTZ Z=+0.05"). --------- */
    lv_obj_send_event(ctx->selecteur_pas.boutons[1], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_pas.index_actif == 1);

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_RAISE], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "TESTZ Z=+0.05") != NULL);
    source_etat_sim_cycle();

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_LOWER], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "TESTZ Z=-0.05") != NULL);
    source_etat_sim_cycle();

    /* --- pas 5mm (index 5, le plus grand) -> TESTZ Z=+5 (klipper_gcode_testz
     * trime le zero de fin, meme comportement que le test unitaire de
     * klipper_gcode.c). ------------------------------------------------------ */
    lv_obj_send_event(ctx->selecteur_pas.boutons[5], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_pas.index_actif == 5);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_RAISE], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "TESTZ Z=+5") != NULL);
    source_etat_sim_cycle();

    /* --- Accept -> ACCEPT. ------------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_ACCEPT], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"ACCEPT\"") != NULL);
    source_etat_sim_cycle();

    /* --- Abort -> ABORT. ---------------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_ZCALIBRATE_BOUTON_ABORT], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "\"script\":\"ABORT\"") != NULL);
    source_etat_sim_cycle();

    /* --- grisage sur donnees_perimees, style RESOLU (round-trip
     * reversible) -- SEULE la valeur Z grise, jamais le placeholder Probe
     * Offset (voir le commentaire de tete du .h, "jamais grise -- rien a
     * griser"). Meme idiome que test_ecran_deplacer.c/test_ecran_reglage_fin.c. */
    lv_color_t couleur_probe_avant = lv_obj_get_style_text_color(ctx->valeur_probe_offset, 0);
    ECRAN_ZCALIBRATE.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_z, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_probe_offset, 0), couleur_probe_avant));
    VERIFIER(!lv_color_eq(couleur_probe_avant, lv_color_hex(0x6B7280)));

    ECRAN_ZCALIBRATE.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_z, 0), lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
