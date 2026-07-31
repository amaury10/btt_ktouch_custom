/* Tâche 4 (refonte accueil/déplacer) : l'écran Déplacer -- construction
 * directe (calloc du contexte à la taille du descripteur, puis
 * ECRAN_DEPLACER.construire()) plutôt que navigation_empiler() -- même choix
 * que suite_ecran_accueil_idle() (host-test/tests/test_ecran_accueil_idle.c),
 * pour la même raison (tester uniquement le contrat de cet écran, pas celui
 * de la pile de navigation).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulée démarrée,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- même garde
 * d'ordonnancement que suite_ecran_accueil_idle_jog()/_home(), qui trace le
 * même seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_deplacer.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_deplacer(void)
{
    printf("suite : ecran deplacer\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_deplacer() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_DEPLACER.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_deplacer_ctx_t *ctx = (ecran_deplacer_ctx_t *)brut;
    ECRAN_DEPLACER.construire(parent, ctx);

    /* --- tous les widgets sont crees --------------------------------- */
    for (int i = 0; i < ECRAN_DEPLACER_JOG_NB; i++) {
        VERIFIER(ctx->jog_boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas.racine != NULL);
    for (int i = 0; i < 4; i++) {
        VERIFIER(ctx->selecteur_pas.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas.index_actif == ECRAN_DEPLACER_PAS_DEFAUT); /* 1 mm par defaut */
    VERIFIER(ctx->selecteur_vitesse.racine != NULL);
    for (int i = 0; i < 3; i++) {
        VERIFIER(ctx->selecteur_vitesse.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_vitesse.index_actif == ECRAN_DEPLACER_VITESSE_DEFAUT); /* Moyen par defaut */
    for (int i = 0; i < ECRAN_DEPLACER_HOME_NB; i++) {
        VERIFIER(ctx->home_boutons[i] != NULL);
    }
    VERIFIER(ctx->position != NULL);
    VERIFIER(ctx->outil_actif_nom != NULL);

    /* --- Pas=10 (indice 2), Vitesse=Rapide (indice 2) -- X+ -> "G1 X10
     * F6000" (VITESSE_XY[2]=6000, brief), enveloppe SAVE/RESTORE_GCODE_STATE
     * (klipper_gcode_jog()). ------------------------------------------- */
    lv_obj_send_event(ctx->selecteur_pas.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_pas.index_actif == 2);
    lv_obj_send_event(ctx->selecteur_vitesse.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_vitesse.index_actif == 2);

    char action[32];
    char arguments[192];

    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_DEPLACER_JOG_X_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SAVE_GCODE_STATE") != NULL);
    VERIFIER(strstr(arguments, "G1 X10 F6000") != NULL);
    VERIFIER(strstr(arguments, "RESTORE_GCODE_STATE") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- signe negatif : X- construit bien "X-10", pas juste "X10" avec un
     * moins ailleurs dans la chaine. -------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_DEPLACER_JOG_X_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 X-10 F6000") != NULL);
    source_etat_sim_cycle();

    /* --- Z utilise VITESSE_Z, pas VITESSE_XY : "G1 Z10 F1200"
     * (VITESSE_Z[2]=1200, brief), meme pas (10mm) toujours selectionne. ---- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_DEPLACER_JOG_Z_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 Z10 F1200") != NULL);
    source_etat_sim_cycle();

    /* --- Home Y : clic direct, AUCUNE confirmation sur cet ecran (voir le
     * commentaire de tete de ecran_deplacer.h, "ECART delibere n2") --
     * "G28 Y" part immediatement. ------------------------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_DEPLACER_HOME_Y], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "G28 Y") != NULL);
    source_etat_sim_cycle();

    /* --- Home All : masque 0x7 -> "G28" plein, jamais un axe isole. ------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_DEPLACER_HOME_ALL], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "\"script\":\"G28\"") != NULL);
    source_etat_sim_cycle();

    /* --- mettre_a_jour() : ligne position + outil actif (brief step 3),
     * meme format que ecran_accueil_idle.c (formater_axe : "--" si l'axe
     * n'est pas reference). ------------------------------------------------ */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.outil_actif = 0;
    etat.position[0] = 12.3f;
    etat.position[1] = 45.6f;
    etat.position[2] = 7.8f;
    etat.axes_references = 0x1u | 0x2u | 0x4u;
    ECRAN_DEPLACER.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:12.3 Y:45.6 Z:7.8");
    VERIFIER_TEXTE(lv_label_get_text(ctx->outil_actif_nom), "Active: T0");

    /* axe partiellement reference : seul Y l'est */
    etat.axes_references = 0x2u;
    ECRAN_DEPLACER.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:-- Y:45.6 Z:--");

    /* aucun extrudeur present -> "Active: --" */
    etat.nb_extrudeurs = 0;
    etat.extrudeurs[0].presente = false;
    ECRAN_DEPLACER.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->outil_actif_nom), "Active: --");

    /* --- perime : grise (position + outil actif), puis redevient normal --
     * style RESOLU, meme lecon que tuile_griser()/ecran_accueil_idle.c
     * (round-trip reversible). ---------------------------------------------*/
    ECRAN_DEPLACER.mettre_a_jour(&etat, true, ctx);
    lv_color_t gris_position = lv_obj_get_style_text_color(ctx->position, 0);
    VERIFIER(lv_color_eq(gris_position, lv_color_hex(0x6B7280)));
    lv_color_t gris_outil = lv_obj_get_style_text_color(ctx->outil_actif_nom, 0);
    VERIFIER(lv_color_eq(gris_outil, lv_color_hex(0x6B7280)));

    ECRAN_DEPLACER.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->position, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->outil_actif_nom, 0), lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
