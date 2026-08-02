/* Ecran Accueil-hub (sous-projet "refonte IHM KlipperScreen", tache 4) :
 * reecriture main_panel -- un resume compact en lecture seule (temperatures,
 * position + outil actif, vitesse/flux, mini-progression d'impression)
 * au-dessus d'une grille de CINQ tuiles de menu (Homing, Temperature,
 * Actions, Configuration, Print). REMPLACE le contenu precedent de ce
 * fichier (pool de tuiles de temperature par palier + grille de 6 cases,
 * voir l'historique git) -- le symbole ECRAN_ACCUEIL_HUB, son id
 * ("accueil_hub") et son titre ("Home") restent INCHANGES (task-4-brief.md :
 * app_main.c l'empile au boot, le rail lit id_accueil = ECRAN_ACCUEIL_HUB.id,
 * le chooser d'habillage (accueil_choix.h) le reference -- aucun des trois
 * ne bouge, seul le CONTENU change).
 *
 * Resume en LECTURE SEULE (aucun tap, contrairement a l'ancien pool de
 * tuiles cliquables) : Temperature se regle desormais via sa tuile de menu,
 * qui ouvre ECRAN_TEMPERATURES -- aucun ciblage par chauffe fait ici. Pas
 * d'indicateur de liaison dans le resume : la barre d'etat de habillage.c en
 * porte deja un (pastille + texte), le redoubler ici violerait §5.3 (un seul
 * endroit qui dit "hors ligne").
 *
 * `ecran_accueil_hub_ctx_t` est expose ici plutot qu'opaque, meme raison que
 * les autres ecrans KlipperScreen de ce dossier : host-test/tests/
 * test_ecran_accueil_hub.c relit les libelles/couleurs via
 * lv_label_get_text()/lv_obj_get_style_text_color() pour prouver ce que
 * mettre_a_jour() ecrit sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"

/* Grille de 5 tuiles de menu, ORDRE FIXE -- table verbatim de
 * task-4-brief.md : Homing -> ECRAN_HOMING, Temperature ->
 * ECRAN_TEMPERATURES, Actions -> ECRAN_ACTIONS, Configuration ->
 * ECRAN_MENU_REGLAGES, Print -> ECRAN_FICHIERS. Reutilise par la boucle de
 * construction (MENU_DEFS dans le .c) et par host-test/tests/
 * test_ecran_accueil_hub.c pour retrouver le bon bouton -- meme convention
 * que ECRAN_MENU_REGLAGES_CASE_xxx (ecran_menu_reglages.h). */
#define ECRAN_ACCUEIL_HUB_MENU_HOMING        0
#define ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE   1
#define ECRAN_ACCUEIL_HUB_MENU_ACTIONS       2
#define ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION 3
#define ECRAN_ACCUEIL_HUB_MENU_PRINT         4
#define ECRAN_ACCUEIL_HUB_MENU_NB            5

typedef struct ecran_accueil_hub_ctx_s {
    /* --- resume (lecture seule, jamais LV_OBJ_FLAG_CLICKABLE) ------------
     * Quatre lignes empilees, chacune un unique lv_label_t : voir
     * ecran_accueil_hub.c pour le format exact de chaque texte. */
    lv_obj_t *temperatures; /* "T0 205.0/210.0  Bed 59.5/60.0", auto-dimensionnee (voir ecran_accueil_hub.c) */
    lv_obj_t *position;     /* "X:.. Y:.. Z:..  T<outil_actif>" (formater_axe -- "--" si non reference) */
    lv_obj_t *vitesse_flux; /* "Speed: NN%  Flow: NN%" */
    lv_obj_t *progression;  /* "Printing: NN%" -- masque (LV_OBJ_FLAG_HIDDEN) hors impression */

    /* --- grille de menu, 5 cases (voir ECRAN_ACCUEIL_HUB_MENU_* ci-dessus
     * pour l'indexation) : `zone_menu` est le conteneur qui les porte,
     * `menu_boutons[i]` est TOUJOURS le bouton de la case `i`, quel que soit
     * l'ordre de creation interne -- meme convention que `rail_t.boutons[i]`
     * (rail.h). Contenu statique : aucune de ces cinq cases n'a besoin de
     * relire un etat au moment du clic, chacune ne fait que
     * navigation_empiler() vers un ecran deja construit. */
    lv_obj_t *zone_menu;
    lv_obj_t *menu_boutons[ECRAN_ACCUEIL_HUB_MENU_NB];
} ecran_accueil_hub_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL_HUB;
