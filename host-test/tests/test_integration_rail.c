/* Tache 6 (refonte accueil/deplacer) : l'INTEGRATION du rail persistant dans
 * le layout racine [rail | conteneur de navigation], et le comportement
 * Klipper de ses quatre actions (apps/klipper/rail_actions.c).
 *
 * Distinct de test_rail.c (le WIDGET seul : dispatch/surlignage, sans
 * navigation ni gcode) : ici on prouve le cablage complet -- le rail SURVIT
 * aux changements d'ecran, et chaque action fait reellement ce qu'elle doit
 * (naviguer, ou emettre le bon gcode via le seam ui_commander() ->
 * source_etat_sim, retrouve par source_etat_sim_derniere_commande()).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit -- le rail
 * y est cree, singleton process-wide, voir habillage.h) ET suite_commandes()
 * (boucle simulee demarree, sinon ui_commander() rend ESP_ERR_INVALID_STATE) :
 * meme garde d'ordonnancement que suite_ecran_deplacer(). */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "confirmation.h"
#include "ecran_accueil_hub.h"
#include "ecran_deplacer.h"
#include "ecran_macros.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "rail.h"
#include "rail_actions.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* Memes helpers de parcours d'arbre que test_commandes.c/test_clavier.c :
 * confirmation.h reste opaque sur ses lv_obj_t* internes, chaque fichier de
 * test qui a besoin d'y entrer les retrouve par parcours (aucune API partagee
 * creee pour l'occasion). Redefinis ici, meme politique que les autres. */
static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

static lv_obj_t *dernier_msgbox(void)
{
    lv_obj_t *fond = dernier_enfant_calque_superieur();
    if (fond == NULL) {
        return NULL;
    }
    return lv_obj_get_child(fond, 0);
}

/* Meme helper que test_rail.c/test_commandes.c : fait avancer l'horloge LVGL
 * pour que la couleur/bordure RESOLUE reflete un lv_obj_add_state() deja
 * applique de facon synchrone (transition de theme, voir leur commentaire). */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

void suite_integration_rail(void)
{
    printf("suite : integration rail\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_integration_rail() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* Branche le comportement Klipper du rail (comme app_main.c le fait) : sans
     * ceci, un clic ne ferait rien (handler NULL, garde de habillage_rail_dispatch()). */
    habillage_definir_action_rail(rail_action_klipper, NULL, ECRAN_ACCUEIL_HUB.id, ECRAN_MACROS.id);

    /* Repart d'une pile propre (meme technique que les sections de
     * test_commandes.c) : navigation_init() detruit tout ecran deja empile.
     * Le rail (g_rail), cree par habillage_construire(), n'est PAS touche --
     * il vit hors de la pile de navigation, c'est tout l'interet du test. */
    navigation_init(lv_screen_active());
    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL_HUB) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), ECRAN_ACCUEIL_HUB.id);

    rail_t *rail = habillage_rail();
    VERIFIER(rail != NULL);
    VERIFIER(rail->racine != NULL);
    for (int i = 0; i < RAIL_NB; i++) {
        VERIFIER(rail->boutons[i] != NULL);
    }

    /* Surlignage : sur l'accueil-hub, le bouton Accueil est actif (bordure),
     * les autres non -- calcule par habillage_pomper() au changement de sommet
     * (compare navigation_id_courant() aux ids injectes ci-dessus). */
    habillage_pomper();
    pomper_transitions_style();
    VERIFIER(lv_obj_get_style_border_width(rail->boutons[RAIL_ACCUEIL], LV_PART_MAIN) > 0);
    VERIFIER(lv_obj_get_style_border_width(rail->boutons[RAIL_MACROS], LV_PART_MAIN) == 0);

    /* --- le rail SURVIT a l'empilement de Deplacer -------------------------- */
    VERIFIER(navigation_empiler(&ECRAN_DEPLACER) == ESP_OK);
    VERIFIER_TEXTE(navigation_id_courant(), "deplacer");
    VERIFIER(rail->racine != NULL); /* toujours la, meme ecran change */
    for (int i = 0; i < RAIL_NB; i++) {
        VERIFIER(rail->boutons[i] != NULL);
    }
    /* Deplacer n'est ni Accueil ni Macros -> aucun bouton surligne (RAIL_NB). */
    habillage_pomper();
    pomper_transitions_style();
    VERIFIER(lv_obj_get_style_border_width(rail->boutons[RAIL_ACCUEIL], LV_PART_MAIN) == 0);

    /* --- clic ACCUEIL depuis Deplacer : revient au fond de pile (hub) ------- */
    lv_obj_send_event(rail->boutons[RAIL_ACCUEIL], LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), ECRAN_ACCUEIL_HUB.id);

    char action[32];
    char arguments[192];

    /* --- clic HOME : G28 DIRECT, sans confirmation ------------------------- */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(rail->boutons[RAIL_HOME], LV_EVENT_CLICKED, NULL);
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* aucune confirmation pour Home */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"G28\"") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- clic MACROS : empile l'ecran macros (le rail navigue via l'app) ---- */
    lv_obj_send_event(rail->boutons[RAIL_MACROS], LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), ECRAN_MACROS.id);
    VERIFIER(rail->racine != NULL); /* le rail persiste aussi par-dessus Macros */
    /* Re-taper Macros alors qu'on y est deja : NE re-empile PAS (garde I1,
     * revue finale) -- la profondeur reste 2, aucune copie accumulee. */
    lv_obj_send_event(rail->boutons[RAIL_MACROS], LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), ECRAN_MACROS.id);
    /* Retour a l'accueil pour la suite (STOP se teste depuis n'importe ou, mais
     * on repart d'un etat connu). */
    lv_obj_send_event(rail->boutons[RAIL_ACCUEIL], LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 1);

    /* --- clic STOP : ouvre une confirmation, N'ENVOIE RIEN tant que non
     * confirme, puis M112 a la confirmation -------------------------------- */
    VERIFIER(!confirmation_est_ouverte());
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(rail->boutons[RAIL_STOP], LV_EVENT_CLICKED, NULL);
    VERIFIER(confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye avant confirmation */

    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    VERIFIER(pied != NULL);
    lv_obj_t *bouton_action = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_action != NULL);
    /* destructif=true : bouton d'action rouge, comme l'exige confirmation.h */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));

    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    /* Le rappel de confirmation.c est SYNCHRONE (voir fermer_et_rappeler()) :
     * la commande est deja en file ici, sans lv_timer_handler() prealable. */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "M112") != NULL);
    lv_timer_handler();      /* acheve la fermeture asynchrone du dialogue */
    source_etat_sim_cycle(); /* draine avant la suite */
    VERIFIER(!confirmation_est_ouverte());

    /* --- STOP, DECLIN : n'envoie rien -------------------------------------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(rail->boutons[RAIL_STOP], LV_EVENT_CLICKED, NULL);
    VERIFIER(confirmation_est_ouverte());
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    pied = lv_msgbox_get_footer(mbox);
    lv_obj_t *bouton_decliner = lv_obj_get_child(pied, 0);
    VERIFIER(bouton_decliner != NULL);
    lv_obj_send_event(bouton_decliner, LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye sur un declin */
    VERIFIER(!confirmation_est_ouverte());
}
