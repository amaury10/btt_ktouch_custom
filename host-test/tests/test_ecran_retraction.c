/* Sous-projet "panneaux KlipperScreen", tache 6 : l'ecran Retraction -- quatre
 * lignes (Retract Length, Retract Speed, Unretract Extra, Unretract Speed)
 * chacune boutons -/+ vers klipper_gcode_retraction_longueur()/
 * klipper_gcode_retraction_vitesse() (tache 1) selon le domaine d'unite de la
 * ligne (voir DOMAINE[] dans ecran_retraction.c), SANS bouton Reset (voir
 * ecran_retraction.h). Construction directe (calloc du contexte a la taille
 * du descripteur, puis ECRAN_RETRACTION.construire()) plutot que
 * navigation_empiler() -- meme choix que test_ecran_limites.c, pour la meme
 * raison (tester uniquement le contrat de cet ecran).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_limites()/suite_ecran_reglage_fin(), ce
 * fichier trace le meme seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_retraction.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "klipper_gcode.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_retraction(void)
{
    printf("suite : ecran retraction\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_retraction() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_RETRACTION.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_retraction_ctx_t *ctx = (ecran_retraction_ctx_t *)brut;
    ECRAN_RETRACTION.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------- */
    for (int i = 0; i < ECRAN_RETRACTION_LIGNE_NB; i++) {
        VERIFIER(ctx->valeurs[i] != NULL);
    }
    for (int i = 0; i < ECRAN_RETRACTION_BOUTON_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
    }

    char action[32];
    char arguments[192];

    /* --- mettre_a_jour() : les quatre valeurs, format exact du brief
     * (longueurs a 2 decimales, vitesses en entier). --------------------- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.retr_length = 1.5f;
    etat.retr_speed = 40.0f;
    etat.retr_unretract_extra = 0.3f;
    etat.retr_unretract_speed = 25.0f;
    ECRAN_RETRACTION.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_LENGTH]), "1.50 mm");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_SPEED]), "40 mm/s");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_EXTRA]), "0.30 mm");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_UNRETRACT_SPEED]), "25 mm/s");

    /* --- +Length, pas fixe 0.1 mm (100 um) : 1.5 -> 1.6 -> SET_RETRACTION
     * RETRACT_LENGTH=1.6 (brief, scenario de test explicite). -------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_LENGTH_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_RETRACTION RETRACT_LENGTH=1.6") != NULL);
    source_etat_sim_cycle();

    /* --- -Length : la valeur mise en cache par mettre_a_jour() (1.5) n'a
     * pas bouge (bouton_cb() ne l'ecrit jamais) -- 1.5 - 0.1 = 1.4. --------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_LENGTH_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION RETRACT_LENGTH=1.4") != NULL);
    source_etat_sim_cycle();

    /* --- +Speed, pas fixe 5 mm/s : 40 -> 45 -> SET_RETRACTION
     * RETRACT_SPEED=45 (brief, scenario de test explicite). ----------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_SPEED_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION RETRACT_SPEED=45") != NULL);
    source_etat_sim_cycle();

    /* --- -Speed : 40 - 5 = 35. --------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_SPEED_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION RETRACT_SPEED=35") != NULL);
    source_etat_sim_cycle();

    /* --- +Extra, pas fixe 0.1 mm (100 um) : 0.3 -> 0.4. --------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_EXTRA_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION UNRETRACT_EXTRA_LENGTH=0.4") != NULL);
    source_etat_sim_cycle();

    /* --- -Extra : 0.3 - 0.1 = 0.2. ------------------------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_EXTRA_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION UNRETRACT_EXTRA_LENGTH=0.2") != NULL);
    source_etat_sim_cycle();

    /* --- +Unretract Speed, pas fixe 5 mm/s : 25 -> 30. ---------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_UNRETRACT_SPEED_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION UNRETRACT_SPEED=30") != NULL);
    source_etat_sim_cycle();

    /* --- -Unretract Speed : 25 - 5 = 20. ------------------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_UNRETRACT_SPEED_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION UNRETRACT_SPEED=20") != NULL);
    source_etat_sim_cycle();

    /* --- bornes [0, 20000] um / [1, 1000] mm/s : +Length pres du plafond
     * sature a 20000 um (20 mm), -Speed pres du plancher sature a 1 mm/s --
     * jamais un SET_RETRACTION hors bornes envoye. ---------------------------- */
    etat.retr_length = 19.95f;  /* 19950 um ; +100 = 20050, sature a 20000 (20 mm) */
    etat.retr_speed = 3.0f;     /* 3 - 5 = -2, sature a 1 */
    ECRAN_RETRACTION.mettre_a_jour(&etat, false, ctx);

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_LENGTH_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION RETRACT_LENGTH=20") != NULL);
    source_etat_sim_cycle();

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_RETRACTION_BOUTON_SPEED_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_RETRACTION RETRACT_SPEED=1") != NULL);
    source_etat_sim_cycle();

    /* --- valeur pas encore recue (0.0f, "pas recu" OU firmware_retraction
     * absent -- voir etat_klipper.h/ecran_retraction.h) : affichee "-", pas
     * "0.00 mm"/"0 mm/s" -- et grisee au meme titre qu'une donnee perimee
     * (voir le commentaire de tete du .h). ------------------------------------ */
    memset(&etat, 0, sizeof(etat));
    ECRAN_RETRACTION.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_LENGTH]), "-");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_SPEED]), "-");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_EXTRA]), "-");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_RETR_UNRETRACT_SPEED]), "-");
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[KLIPPER_RETR_LENGTH], 0), lv_color_hex(0x6B7280)));

    /* --- grisage sur donnees_perimees, style RESOLU (round-trip
     * reversible) -- meme lecon que ecran_limites.c/ecran_reglage_fin.c/
     * tuile_griser(). ---------------------------------------------------------- */
    etat.retr_length = 1.5f;
    etat.retr_speed = 40.0f;
    etat.retr_unretract_extra = 0.3f;
    etat.retr_unretract_speed = 25.0f;
    ECRAN_RETRACTION.mettre_a_jour(&etat, false, ctx);
    for (int i = 0; i < ECRAN_RETRACTION_LIGNE_NB; i++) {
        VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[i], 0), lv_color_hex(0x6B7280)));
    }

    ECRAN_RETRACTION.mettre_a_jour(&etat, true, ctx);
    for (int i = 0; i < ECRAN_RETRACTION_LIGNE_NB; i++) {
        VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[i], 0), lv_color_hex(0x6B7280)));
    }

    ECRAN_RETRACTION.mettre_a_jour(&etat, false, ctx);
    for (int i = 0; i < ECRAN_RETRACTION_LIGNE_NB; i++) {
        VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[i], 0), lv_color_hex(0x6B7280)));
    }

    lv_obj_delete(parent);
    free(brut);
}
