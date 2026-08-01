/* Sous-projet 3 (decoupage KlipperScreen), tache 2 : l'ecran Extruder --
 * construction directe (calloc du contexte a la taille du descripteur, puis
 * ECRAN_EXTRUDER.construire()) plutot que navigation_empiler() -- meme choix
 * que suite_ecran_deplacer()/suite_ecran_temperatures(), pour la meme raison
 * (tester uniquement le contrat de cet ecran, pas celui de la pile de
 * navigation ; le cablage hub -> ECRAN_EXTRUDER est lui teste dans
 * test_ecran_accueil_hub.c, meme separation que pour Deplacer/Temperatures).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- ce fichier trace
 * le meme seam ui_commander() -> source_etat_sim que suite_ecran_deplacer(),
 * meme garde d'ordonnancement. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_extruder.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_extruder(void)
{
    printf("suite : ecran extruder\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_extruder() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_EXTRUDER.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_extruder_ctx_t *ctx = (ecran_extruder_ctx_t *)brut;
    ECRAN_EXTRUDER.construire(parent, ctx);

    /* --- tous les widgets sont crees --------------------------------- */
    VERIFIER(ctx->actif_label != NULL);
    VERIFIER(ctx->tuile.racine != NULL);
    VERIFIER(ctx->selecteur_longueur.racine != NULL);
    for (int i = 0; i < 4; i++) {
        VERIFIER(ctx->selecteur_longueur.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_longueur.index_actif == ECRAN_EXTRUDER_LONGUEUR_DEFAUT); /* 10mm par defaut */
    VERIFIER(ctx->selecteur_vitesse.racine != NULL);
    for (int i = 0; i < 3; i++) {
        VERIFIER(ctx->selecteur_vitesse.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_vitesse.index_actif == ECRAN_EXTRUDER_VITESSE_DEFAUT); /* Moyen par defaut */
    VERIFIER(ctx->bouton_extruder != NULL);
    VERIFIER(ctx->bouton_retracter != NULL);

    /* --- 2 extrudeurs, T0 actif (brief step 1) -------------------------- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 2;
    etat.extrudeurs[0].presente = true;
    etat.extrudeurs[0].actuelle = 205.0f;
    etat.extrudeurs[0].consigne = 210.0f;
    etat.extrudeurs[1].presente = true;
    etat.extrudeurs[1].actuelle = 45.0f;
    etat.extrudeurs[1].consigne = 0.0f;
    etat.outil_actif = 0;
    ECRAN_EXTRUDER.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->actif_label), "Actif : T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->tuile.valeur), "205.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->tuile.consigne), "210.0");

    char action[32];
    char arguments[192];

    /* --- longueur=10 (defaut), vitesse=Moyen (defaut) -- Extruder ->
     * "G1 E10 F300", enveloppe SAVE_GCODE_STATE/M83/RESTORE_GCODE_STATE
     * (klipper_gcode_extrude()). ------------------------------------------ */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_extruder, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SAVE_GCODE_STATE") != NULL);
    VERIFIER(strstr(arguments, "M83") != NULL);
    VERIFIER(strstr(arguments, "G1 E10 F300") != NULL);
    VERIFIER(strstr(arguments, "RESTORE_GCODE_STATE") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- Retracter : signe negatif -> "G1 E-10 F300", pas juste "G1 E10"
     * avec un moins ailleurs dans la chaine. ------------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_retracter, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 E-10 F300") != NULL);
    source_etat_sim_cycle();

    /* --- longueur=50 (indice 3) puis Extruder -> "G1 E50 F300". --------- */
    lv_obj_send_event(ctx->selecteur_longueur.boutons[3], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_longueur.index_actif == 3);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_extruder, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 E50 F300") != NULL);
    source_etat_sim_cycle();

    /* --- vitesse=Rapide (indice 2) puis Extruder -> "G1 E50 F600". ------ */
    lv_obj_send_event(ctx->selecteur_vitesse.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->selecteur_vitesse.index_actif == 2);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_extruder, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 E50 F600") != NULL);
    source_etat_sim_cycle();

    /* --- perime : grise (ligne d'etat + tuile), puis redevient normal --
     * style RESOLU, meme lecon que tuile_griser()/le reste de ce depot
     * (round-trip reversible). --------------------------------------------- */
    ECRAN_EXTRUDER.mettre_a_jour(&etat, true, ctx);
    lv_color_t gris_actif = lv_obj_get_style_text_color(ctx->actif_label, 0);
    VERIFIER(lv_color_eq(gris_actif, lv_color_hex(0x6B7280)));
    lv_color_t gris_tuile = lv_obj_get_style_text_color(ctx->tuile.valeur, 0);
    VERIFIER(lv_color_eq(gris_tuile, lv_color_hex(0x6B7280)));

    ECRAN_EXTRUDER.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->actif_label, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->tuile.valeur, 0), lv_color_hex(0x6B7280)));

    /* --- aucun extrudeur present -> "Actif : --" ------------------------- */
    etat.nb_extrudeurs = 0;
    etat.extrudeurs[0].presente = false;
    ECRAN_EXTRUDER.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->actif_label), "Actif : --");

    lv_obj_delete(parent);
    free(brut);
}
