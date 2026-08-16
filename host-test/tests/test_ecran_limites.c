/* Sous-projet "panneaux KlipperScreen", tache 5 : l'ecran Limits -- quatre
 * lignes (Max Velocity, Max Acceleration, Square Corner Velocity, Accel to
 * Decel) chacune boutons -/+ vers klipper_gcode_limite_vitesse() (tache 1),
 * SANS bouton Reset (voir ecran_limites.h). Construction directe (calloc du
 * contexte a la taille du descripteur, puis ECRAN_LIMITES.construire())
 * plutot que navigation_empiler() -- meme choix que test_ecran_reglage_fin.c,
 * pour la meme raison (tester uniquement le contrat de cet ecran).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_reglage_fin()/suite_ecran_niveau_lit(), ce
 * fichier trace le meme seam ui_commander() -> source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_limites.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "klipper_gcode.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_limites(void)
{
    printf("suite : ecran limites\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_limites() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_LIMITES.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_limites_ctx_t *ctx = (ecran_limites_ctx_t *)brut;
    ECRAN_LIMITES.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------- */
    for (int i = 0; i < ECRAN_LIMITES_LIGNE_NB; i++) {
        VERIFIER(ctx->valeurs[i] != NULL);
    }
    for (int i = 0; i < ECRAN_LIMITES_BOUTON_NB; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
    }

    char action[32];
    char arguments[192];

    /* --- mettre_a_jour() : les quatre valeurs, format exact du brief. ----- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.limite_velocity = 250.0f;
    etat.limite_accel = 4000.0f;
    etat.limite_square_corner = 5.0f;
    etat.limite_accel_to_decel = 2000.0f;
    ECRAN_LIMITES.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_VELOCITY]), "250 mm/s");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_ACCEL]), "4000 mm/s^2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_SQV]), "5 mm/s");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_ACCEL_TO_DECEL]), "2000 mm/s^2");

    /* --- +Velocity, pas fixe 10 : 250 + 10 = 260 -> SET_VELOCITY_LIMIT
     * VELOCITY=260 (brief, scenario de test explicite). ---------------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_VELOCITY_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT VELOCITY=260") != NULL);
    source_etat_sim_cycle();

    /* --- -Velocity : la valeur mise en cache par mettre_a_jour() (250) n'a
     * pas bouge (bouton_cb() ne l'ecrit jamais) -- 250 - 10 = 240. --------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_VELOCITY_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT VELOCITY=240") != NULL);
    source_etat_sim_cycle();

    /* --- +Accel, pas fixe 100 : 4000 + 100 = 4100. ------------------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_ACCEL_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT ACCEL=4100") != NULL);
    source_etat_sim_cycle();

    /* --- -Accel : 4000 - 100 = 3900. --------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_ACCEL_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT ACCEL=3900") != NULL);
    source_etat_sim_cycle();

    /* --- +SQV, pas fixe 1 : 5 + 1 = 6. -------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_SQV_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=6") != NULL);
    source_etat_sim_cycle();

    /* --- -SQV : 5 - 1 = 4. --------------------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_SQV_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=4") != NULL);
    source_etat_sim_cycle();

    /* --- +Accel to Decel, pas fixe 100 : 2000 + 100 = 2100. ----------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_ACCEL_TO_DECEL_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT ACCEL_TO_DECEL=2100") != NULL);
    source_etat_sim_cycle();

    /* --- -Accel to Decel : 2000 - 100 = 1900. -------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_ACCEL_TO_DECEL_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT ACCEL_TO_DECEL=1900") != NULL);
    source_etat_sim_cycle();

    /* --- bornes [1, 100000] : +Velocity pres du plafond sature a 100000,
     * -SQV pres du plancher sature a 1 -- jamais un SET_VELOCITY_LIMIT hors
     * bornes envoye. ---------------------------------------------------------- */
    etat.limite_velocity = 99995.0f;
    etat.limite_square_corner = 0.5f;
    ECRAN_LIMITES.mettre_a_jour(&etat, false, ctx);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_VELOCITY_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT VELOCITY=100000") != NULL); /* 99995+10=100005, sature */
    source_etat_sim_cycle();

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[ECRAN_LIMITES_BOUTON_SQV_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=1") != NULL); /* round(0.5)=1, 1-1=0, sature a 1 */
    source_etat_sim_cycle();

    /* --- valeur pas encore recue (0.0f, "pas recu" -- voir etat_klipper.h) :
     * affichee "-", pas "0 mm/s^2" -- et grisee au meme titre qu'une donnee
     * perimee (voir le commentaire de tete du .h). ---------------------------- */
    memset(&etat, 0, sizeof(etat));
    ECRAN_LIMITES.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_VELOCITY]), "-");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_ACCEL]), "-");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_SQV]), "-");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeurs[KLIPPER_LIM_ACCEL_TO_DECEL]), "-");
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[KLIPPER_LIM_VELOCITY], 0), lv_color_hex(0x6B7280)));

    /* --- grisage sur donnees_perimees, style RESOLU (round-trip
     * reversible) -- meme lecon que ecran_reglage_fin.c/tuile_griser(). ------ */
    etat.limite_velocity = 250.0f;
    etat.limite_accel = 4000.0f;
    etat.limite_square_corner = 5.0f;
    etat.limite_accel_to_decel = 2000.0f;
    ECRAN_LIMITES.mettre_a_jour(&etat, false, ctx);
    for (int i = 0; i < ECRAN_LIMITES_LIGNE_NB; i++) {
        VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[i], 0), lv_color_hex(0x6B7280)));
    }

    ECRAN_LIMITES.mettre_a_jour(&etat, true, ctx);
    for (int i = 0; i < ECRAN_LIMITES_LIGNE_NB; i++) {
        VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[i], 0), lv_color_hex(0x6B7280)));
    }

    ECRAN_LIMITES.mettre_a_jour(&etat, false, ctx);
    for (int i = 0; i < ECRAN_LIMITES_LIGNE_NB; i++) {
        VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->valeurs[i], 0), lv_color_hex(0x6B7280)));
    }

    lv_obj_delete(parent);
    free(brut);
}
