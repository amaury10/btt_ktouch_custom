/* Sous-projet 4 (decoupage KlipperScreen), tache 2 : l'ecran Ventilateurs --
 * slider + prereglages (Off/25/50/75/100) + saisie numerique, TROIS moyens
 * de regler la MEME valeur (le ventilateur de piece, ventilateurs[0]), tous
 * vers klipper_gcode_ventilateur() (tache 1). Voir ecran_ventilateurs.h pour
 * le contrat exact.
 *
 * Modele direct : test_ecran_temperatures.c (clavier + parsing borne, memes
 * helpers dernier_enfant_calque_superieur()/enfant_de_classe() redefinis
 * localement -- meme choix que le reste de ce harnais, voir leur commentaire
 * de tete) et test_ecran_deplacer.c (construction directe, trace du seam
 * ui_commander() -> source_etat_sim).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_deplacer()/suite_ecran_temperatures(), le
 * clavier passe par ui_commander()/source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_ventilateurs.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* Retrouve le dernier enfant de lv_layer_top() -- meme helper que
 * test_ecran_temperatures.c/test_clavier.c : le clavier (clavier.h) se
 * construit toujours sur cette couche, par-dessus l'ecran actif entier. */
static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

/* Retrouve le premier descendant DIRECT de `parent` dont la classe LVGL est
 * `classe` -- copie locale du meme helper que test_ecran_temperatures.c
 * (redefinie ici plutot que partagee, meme choix que le reste de ce
 * harnais). */
static lv_obj_t *enfant_de_classe(lv_obj_t *parent, const lv_obj_class_t *classe)
{
    if (parent == NULL) {
        return NULL;
    }
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *enfant = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(enfant, classe)) {
            return enfant;
        }
    }
    return NULL;
}

void suite_ecran_ventilateurs(void)
{
    printf("suite : ecran ventilateurs\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_ventilateurs() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* navigation_init() : meme technique defensive que suite_ecran_temperatures()
     * -- detruit tout ecran qu'une suite precedente aurait laisse empile,
     * pour que le bandeau de l'habillage reste retrouvable comme DERNIER
     * enfant de lv_screen_active(). */
    navigation_init(lv_screen_active());
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_VENTILATEURS.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_ventilateurs_ctx_t *ctx = (ecran_ventilateurs_ctx_t *)brut;
    ECRAN_VENTILATEURS.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    VERIFIER(ctx->slider != NULL);
    VERIFIER(ctx->bouton_valeur != NULL);
    VERIFIER(ctx->label_valeur != NULL);
    VERIFIER(ctx->preregalges.racine != NULL);
    for (int i = 0; i < ECRAN_VENTILATEURS_PRESET_NB; i++) {
        VERIFIER(ctx->preregalges.boutons[i] != NULL);
    }
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_valeur), "0 %");
    VERIFIER(lv_slider_get_value(ctx->slider) == 0);
    VERIFIER(ctx->glissement_en_cours == false);

    char action[32];
    char arguments[192];

    /* --- (a) slider a 50 + LV_EVENT_RELEASED : SEUL evenement qui envoie
     * du gcode -- M106 S128 (round(50*255/100)=128). --------------------- */
    lv_slider_set_value(ctx->slider, 50, LV_ANIM_OFF);
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->slider, LV_EVENT_RELEASED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "M106 S128") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (b) LV_EVENT_VALUE_CHANGED seul : AUCUN gcode (anti-flood
     * Klipper), seul le label "NN %" suit -- retour visuel du drag. ------- */
    lv_slider_set_value(ctx->slider, 30, LV_ANIM_OFF);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->slider, LV_EVENT_VALUE_CHANGED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_valeur), "30 %");

    /* --- (c) prereglage 100 -> M106 S255. -------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->preregalges.boutons[ECRAN_VENTILATEURS_PRESET_100], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "M106 S255") != NULL);
    source_etat_sim_cycle();

    /* --- prereglage Off -> M106 S0 : meme chemin que 25/50/75/100, jamais
     * un cas special (voir preset_bouton_cb()). --------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->preregalges.boutons[ECRAN_VENTILATEURS_PRESET_OFF], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "M106 S0") != NULL);
    source_etat_sim_cycle();

    /* --- (d) tap sur le bouton "NN %" : le clavier numerique apparait,
     * prerempli avec la position ACTUELLE du slider (40, pose directement
     * sans evenement -- lv_slider_get_value() reste la seule source lue par
     * valeur_bouton_cb()). ------------------------------------------------- */
    lv_slider_set_value(ctx->slider, 40, LV_ANIM_OFF);
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    lv_obj_send_event(ctx->bouton_valeur, LV_EVENT_CLICKED, NULL);
    lv_obj_t *racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    lv_obj_t *kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_NUMBER);
    VERIFIER_TEXTE(lv_textarea_get_placeholder_text(ta), "40");
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "");

    /* --- (e) valider "25" -> M106 S64 (round(25*255/100)=64). ------------- */
    lv_textarea_set_text(ta, "25");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "M106 S64") != NULL);
    lv_timer_handler(); /* acheve la fermeture asynchrone du clavier */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    source_etat_sim_cycle();

    /* --- (f) "150" (hors borne [0,100]) : notification d'erreur, AUCUN
     * gcode envoye. --------------------------------------------------------- */
    lv_obj_send_event(ctx->bouton_valeur, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "150");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid fan speed (0-100)");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C))); /* erreur */
    lv_timer_handler();

    /* --- (g) "x" (non numerique) : meme resultat. -------------------------- */
    lv_obj_send_event(ctx->bouton_valeur, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "x");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid fan speed (0-100)");
    lv_timer_handler();

    /* --- (h) annuler : rien envoye. ----------------------------------------- */
    lv_obj_send_event(ctx->bouton_valeur, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    VERIFIER(kb != NULL);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    lv_timer_handler();
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* --- (i) mettre_a_jour() : recale slider ET label sur
     * ventilateurs[0].vitesse (0.5 -> 50 %). --------------------------------- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.ventilateurs[0].present = true;
    etat.ventilateurs[0].vitesse = 0.5f;
    ECRAN_VENTILATEURS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_slider_get_value(ctx->slider) == 50);
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_valeur), "50 %");

    /* --- (j) piege d'interaction : pendant un glissement (LV_EVENT_PRESSED
     * sans RELEASED), mettre_a_jour() NE DOIT PAS deplacer le slider -- le
     * label, lui, reste a jour (voir le commentaire de tete du .h). --------- */
    lv_obj_send_event(ctx->slider, LV_EVENT_PRESSED, NULL);
    VERIFIER(ctx->glissement_en_cours == true);
    etat.ventilateurs[0].vitesse = 0.2f;
    ECRAN_VENTILATEURS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_slider_get_value(ctx->slider) == 50); /* inchange, glissement en cours */
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_valeur), "20 %");

    lv_obj_send_event(ctx->slider, LV_EVENT_RELEASED, NULL);
    VERIFIER(ctx->glissement_en_cours == false);
    source_etat_sim_cycle(); /* draine le gcode du relachement, contenu non verifie ici */

    ECRAN_VENTILATEURS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_slider_get_value(ctx->slider) == 20); /* le glissement est termine, mettre_a_jour recale a nouveau */

    /* --- (k) grisage sur donnees_perimees, style RESOLU (round-trip
     * reversible) -- meme lecon que tuile_griser()/le reste de ui/. --------- */
    ECRAN_VENTILATEURS.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->label_valeur, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->slider, LV_PART_INDICATOR), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->slider, LV_PART_KNOB), lv_color_hex(0x6B7280)));

    ECRAN_VENTILATEURS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->label_valeur, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->slider, LV_PART_INDICATOR), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->slider, LV_PART_KNOB), lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
