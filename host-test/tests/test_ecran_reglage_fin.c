/* Sous-projet "panneaux KlipperScreen", tache 2 : l'ecran Fine Tune --
 * trois lignes (Z-offset, Speed, Flow) chacune boutons -/+ vers
 * klipper_gcode_offset_z()/vitesse_impression()/flux() (tache 1), plus un
 * bouton Reset dedie sur Z-offset. Construction directe (calloc du contexte
 * a la taille du descripteur, puis ECRAN_REGLAGE_FIN.construire()) plutot
 * que navigation_empiler() -- meme choix que test_ecran_deplacer.c, pour la
 * meme raison (tester uniquement le contrat de cet ecran, aucune saisie
 * clavier ici contrairement a test_ecran_ventilateurs.c/
 * test_ecran_temperatures.c).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_deplacer()/suite_ecran_ventilateurs(), ce
 * fichier trace le meme seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_reglage_fin.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_reglage_fin(void)
{
    printf("suite : ecran reglage fin\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_reglage_fin() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_REGLAGE_FIN.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_reglage_fin_ctx_t *ctx = (ecran_reglage_fin_ctx_t *)brut;
    ECRAN_REGLAGE_FIN.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    VERIFIER(ctx->valeur_z != NULL);
    VERIFIER(ctx->valeur_speed != NULL);
    VERIFIER(ctx->valeur_flow != NULL);
    for (int i = 0; i < ECRAN_REGLAGE_FIN_BOUTON_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
    }
    VERIFIER(ctx->bouton_reset_z != NULL);
    VERIFIER(ctx->selecteur_pct.racine != NULL);
    for (int i = 0; i < ECRAN_REGLAGE_FIN_PAS_PCT_NB; i++) {
        VERIFIER(ctx->selecteur_pct.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pct.index_actif == ECRAN_REGLAGE_FIN_PAS_PCT_DEFAUT); /* pas 5% par defaut */
    VERIFIER(ctx->selecteur_pas_z.racine != NULL);
    for (int i = 0; i < ECRAN_REGLAGE_FIN_PAS_Z_NB; i++) {
        VERIFIER(ctx->selecteur_pas_z.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas_z.index_actif == ECRAN_REGLAGE_FIN_PAS_Z_DEFAUT); /* pas 0.01mm par defaut */
    VERIFIER(ctx->label_pct != NULL);
    VERIFIER(ctx->label_pas_z != NULL);

    /* --- mettre_a_jour() : les trois labels, format exact du brief. ----- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.vitesse_pct = 100;
    etat.flux_pct = 95;
    etat.babystep_z_um = -20;
    ECRAN_REGLAGE_FIN.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_z), "-0.02 mm");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_speed), "100 %");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_flow), "95 %");

    char action[32];
    char arguments[192];

    /* --- +Speed, pas 5% par defaut : 100 + 5 = 105 -> M220 S105 (brief,
     * scenario de test explicite). ---------------------------------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_SPEED_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "M220 S105") != NULL);
    source_etat_sim_cycle();

    /* --- -Speed : la valeur mise en cache par mettre_a_jour() (100) n'a pas
     * bouge (bouton_cb() ne l'ecrit jamais) -- 100 - 5 = 95 -> M220 S95. ---- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_SPEED_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "M220 S95") != NULL);
    source_etat_sim_cycle();

    /* --- +Flow : 95 + 5 = 100 -> M221 S100. ------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_FLOW_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "M221 S100") != NULL);
    source_etat_sim_cycle();

    /* --- -Flow : 95 - 5 = 90 -> M221 S90. ---------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_FLOW_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "M221 S90") != NULL);
    source_etat_sim_cycle();

    /* --- Z+ , pas 0.01mm par defaut -- Z_ADJUST est un ajustement RELATIF
     * cote Klipper (voir le commentaire de tete du .h) : aucune lecture de
     * babystep_z_um necessaire, le pas part directement. ------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_Z_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_GCODE_OFFSET Z_ADJUST=0.01 MOVE=1") != NULL);
    source_etat_sim_cycle();

    /* --- Z- : meme pas, signe oppose. -------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_Z_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_GCODE_OFFSET Z_ADJUST=-0.01 MOVE=1") != NULL);
    source_etat_sim_cycle();

    /* --- changement des deux selecteurs de pas -- pas 25% et 0.05mm. ------ */
    lv_obj_send_event(ctx->selecteur_pct.boutons[3], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_pct.index_actif == 3);
    lv_obj_send_event(ctx->selecteur_pas_z.boutons[1], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_pas_z.index_actif == 1);

    /* +Speed avec le nouveau pas : 100 (cache inchange) + 25 = 125. -------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_SPEED_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "M220 S125") != NULL);
    source_etat_sim_cycle();

    /* Z+ avec le nouveau pas : 0.05mm. -------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_Z_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_GCODE_OFFSET Z_ADJUST=0.05 MOVE=1") != NULL);
    source_etat_sim_cycle();

    /* --- Reset Z-offset : toujours Z=0, quel que soit le pas selectionne. - */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_reset_z, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"SET_GCODE_OFFSET Z=0 MOVE=1\"") != NULL);
    source_etat_sim_cycle();

    /* --- bornes [1, 300] : +Speed pres du plafond sature a 300, -Flow pres
     * du plancher sature a 1 -- jamais de M220/M221 hors bornes envoye
     * (meme politique que clamp(...) dans le brief). Selecteur pct toujours
     * a l'indice 3 (pas 25%) depuis plus haut. ------------------------------ */
    etat.vitesse_pct = 298;
    etat.flux_pct = 2;
    ECRAN_REGLAGE_FIN.mettre_a_jour(&etat, false, ctx);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_SPEED_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "M220 S300") != NULL); /* 298+25=323, sature a 300 */
    source_etat_sim_cycle();

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_FLOW_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "M221 S1") != NULL); /* 2-25=-23, sature a 1 */
    source_etat_sim_cycle();

    /* --- defense contre un etat corrompu : vitesse_pct > 300 (hors du
     * domaine M220/M221) est bornee a 300 avant affichage ET avant mise en
     * cache -- meme politique que consigne_u16() dans
     * ecran_temperatures.c. -------------------------------------------------- */
    etat.vitesse_pct = 500;
    etat.flux_pct = 95;
    ECRAN_REGLAGE_FIN.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_speed), "300 %");

    /* --- grisage sur donnees_perimees, style RESOLU (round-trip
     * reversible) -- meme lecon que tuile_griser()/le reste de ui/. --------- */
    ECRAN_REGLAGE_FIN.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_z, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_speed, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_flow, 0), lv_color_hex(0x6B7280)));

    ECRAN_REGLAGE_FIN.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_z, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_speed, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeur_flow, 0), lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
