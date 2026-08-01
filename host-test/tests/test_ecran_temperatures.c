/* Sous-projet 2 (découpage KlipperScreen), tâche 1 : l'écran Températures --
 * tuiles réglables (tap -> clavier numérique) + rangée de 4 préréglages.
 * Reprend la logique de la section "températures" de l'ancien
 * ecran_accueil_idle.c (git ce47e3a, supprimé tâche 7 de la refonte
 * accueil/déplacer) : voir ecran_temperatures.h pour le contrat exact.
 *
 * Modèle direct : suite_ecran_accueil_idle_temp() de l'ancien
 * test_ecran_accueil_idle.c (même git ce47e3a) pour la partie clavier/
 * préréglages, et suite_ecran_accueil_hub() (test_ecran_accueil_hub.c) pour
 * la partie construction directe/widgets/grisage -- cet écran n'a ni jog ni
 * homing ni ligne de position, donc aucune des suites _jog()/_home() de
 * l'ancien idle n'a d'équivalent ici.
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulée démarrée,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- même garde
 * d'ordonnancement que suite_ecran_deplacer()/suite_ecran_accueil_hub(), le
 * clavier passe par ui_commander()/source_etat_sim. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_temperatures.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

static size_t compter_cellules_visibles(const ecran_temperatures_ctx_t *ctx)
{
    size_t total = 0;
    for (size_t i = 0; i < ECRAN_TEMPERATURES_CELLULES_MAX; i++) {
        if (!lv_obj_has_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_HIDDEN)) {
            total++;
        }
    }
    return total;
}

/* Retrouve le dernier enfant de lv_layer_top() -- meme helper que
 * dernier_enfant_calque_superieur() dans l'ancien test_ecran_accueil_idle.c/
 * test_clavier.c : le clavier (clavier.h) se construit toujours sur cette
 * couche, par-dessus l'ecran actif entier. */
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
 * `classe` -- copie locale du meme helper que test_clavier.c/l'ancien
 * test_ecran_accueil_idle.c (redefinie ici plutot que partagee, meme choix
 * que le reste de ce harnais). */
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

void suite_ecran_temperatures(void)
{
    printf("suite : ecran temperatures\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_temperatures() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* navigation_init() : meme technique defensive que suite_ecran_accueil_idle_temp()
     * (l'ancien test) -- detruit tout ecran qu'une suite precedente aurait
     * laisse empile, pour que le bandeau de l'habillage reste retrouvable
     * comme DERNIER enfant de lv_screen_active(). */
    navigation_init(lv_screen_active());
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_TEMPERATURES.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_temperatures_ctx_t *ctx = (ecran_temperatures_ctx_t *)brut;
    ECRAN_TEMPERATURES.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    for (size_t i = 0; i < ECRAN_TEMPERATURES_CELLULES_MAX; i++) {
        VERIFIER(ctx->cellules[i].racine != NULL);
        VERIFIER(ctx->cellules[i].nom != NULL);
        VERIFIER(ctx->cellules[i].valeur != NULL);
        VERIFIER(ctx->cellules[i].consigne != NULL);
    }
    VERIFIER(ctx->zone_preregalges != NULL);
    for (int i = 0; i < ECRAN_TEMPERATURES_PRESET_NB; i++) {
        VERIFIER(ctx->preset_boutons[i] != NULL);
    }
    /* aucune cellule visible tant que mettre_a_jour() n'a jamais tourne */
    VERIFIER(compter_cellules_visibles(ctx) == 0);

    /* --- 2 extrudeurs + plateau (palier MOYEN, brief) : cellules[0]=T0,
     * cellules[1]=T1, cellules[2]=Bed -- 3 tuiles, consigne affichee. ----- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 2;
    etat.extrudeurs[0].presente = true;
    etat.extrudeurs[0].actuelle = 205.0f;
    etat.extrudeurs[0].consigne = 200.0f;
    etat.extrudeurs[1].presente = true;
    etat.extrudeurs[1].actuelle = 45.0f;
    etat.extrudeurs[1].consigne = 0.0f;
    etat.plateau.presente = true;
    etat.plateau.actuelle = 55.0f;
    etat.plateau.consigne = 55.0f;
    etat.outil_actif = 0;
    ECRAN_TEMPERATURES.mettre_a_jour(&etat, false, ctx);

    VERIFIER(compter_cellules_visibles(ctx) == 3);
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].nom), "T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].valeur), "205.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].consigne), "200.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[1].nom), "T1");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].nom), "Bed");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].valeur), "55.0");
    /* outil actif (T0) marque, T1 et le plateau ne le sont jamais */
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[0].racine, 0) > 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[1].racine, 0) == 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[2].racine, 0) == 0);
    VERIFIER(!lv_obj_has_flag(ctx->cellules[0].consigne, LV_OBJ_FLAG_HIDDEN));

    char action[32];
    char arguments[192];

    /* --- (a) tap sur la cellule buse active (T0) : un clavier numerique
     * apparait sur lv_layer_top(), prerempli (placeholder) avec la consigne
     * courante (200). ------------------------------------------------------ */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    lv_obj_t *racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    lv_obj_t *kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_NUMBER);
    VERIFIER_TEXTE(lv_textarea_get_placeholder_text(ta), "200");
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "");

    /* --- (b) valider "210" : BACKEND_ACTION_GCODE, script
     * SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210. -------------------- */
    lv_textarea_set_text(ta, "210");
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210") != NULL);
    lv_timer_handler(); /* acheve la fermeture asynchrone du clavier */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (c) valider "999" (hors borne [0,350]) : notification d'erreur,
     * AUCUN gcode envoye (taille de file inchangee). ------------------------ */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "999");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid temperature (0-350)");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C))); /* erreur */
    lv_timer_handler();

    /* --- (d) saisie non numerique : meme resultat (erreur, rien envoye). -- */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "abc");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid temperature (0-350)");
    lv_timer_handler();

    /* --- (e) bornes EXACTES [0,350] : 350 passe (TARGET=350 envoye), 351
     * est rejete sans rien envoyer -- la borne haute est inclusive. -------- */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "350");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=350") != NULL);
    lv_timer_handler();
    source_etat_sim_cycle();

    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "351");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid temperature (0-350)");
    lv_timer_handler();

    /* --- (f) annuler : rien envoye, clavier disparait sans invoquer le
     * gcode (spec : "valeur NULL (annule) => rien"). ------------------------ */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    VERIFIER(kb != NULL);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    lv_timer_handler();
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* --- cellule plateau ("Bed target"), prerempli 55, valider "70" :
     * HEATER=heater_bed. ----------------------------------------------------*/
    lv_obj_send_event(ctx->cellules[2].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER_TEXTE(lv_textarea_get_placeholder_text(ta), "55");
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "");
    lv_textarea_set_text(ta, "70");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=70") != NULL);
    lv_timer_handler();
    source_etat_sim_cycle();

    /* --- (g) prereglage PLA : DEUX gcodes queues par le MEME clic -- buse
     * ACTIVE (T0, 210) puis plateau (60). source_etat_sim_cycle() depile
     * toute la file d'un coup, donc source_etat_sim_avant_derniere_commande()
     * est necessaire pour inspecter le premier des deux. -------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->preset_boutons[ECRAN_TEMPERATURES_PRESET_PLA], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 2);
    VERIFIER(source_etat_sim_avant_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210") != NULL);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=60") != NULL);
    source_etat_sim_cycle();

    /* --- (h) "Off" : buse active a 0, plateau a 0 -- meme chemin que PLA/
     * PETG/ABS, jamais un cas special (voir preset_bouton_cb()). ------------ */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->preset_boutons[ECRAN_TEMPERATURES_PRESET_OFF], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 2);
    VERIFIER(source_etat_sim_avant_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0") != NULL);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0") != NULL);
    source_etat_sim_cycle();

    /* --- (i) grisage sur donnees_perimees, style RESOLU (round-trip
     * reversible) -- meme lecon que tuile_griser()/ecran_accueil_hub.c. ----- */
    ECRAN_TEMPERATURES.mettre_a_jour(&etat, true, ctx);
    lv_color_t gris_valeur = lv_obj_get_style_text_color(ctx->cellules[2].valeur, 0);
    VERIFIER(lv_color_eq(gris_valeur, lv_color_hex(0x6B7280)));
    lv_color_t gris_nom = lv_obj_get_style_text_color(ctx->cellules[2].nom, 0);
    VERIFIER(lv_color_eq(gris_nom, lv_color_hex(0x6B7280)));

    ECRAN_TEMPERATURES.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->cellules[2].valeur, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->cellules[2].nom, 0), lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}
