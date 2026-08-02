/* Sous-menu Actions (sous-projet "refonte IHM KlipperScreen", tache 2) : la
 * grille "Actions" de KlipperScreen -- sept cases qui renvoient CHACUNE vers
 * un panneau deja construit par un jalon precedent (Move/Extrude/Fan/
 * Temperature/Macros -> navigation_empiler(), meme idiome que
 * ecran_menu_reglages.c), sauf "Disable Motors", qui envoie M84 directement
 * (klipper_gcode_niveau_lit(KLIPPER_LIT_DISABLE), meme idiome cJSON que
 * ecran_niveau_lit.c) -- KlipperScreen ne demande AUCUNE confirmation avant
 * ce bouton precis, ce depot ne l'invente pas non plus (task-2-brief.md,
 * verbatim).
 *
 * Menu purement statique, meme discipline que ecran_menu_reglages.c :
 * `mettre_a_jour = NULL` (rien a rafraichir, rien a griser -- aucune des
 * sept actions ne depend d'une valeur lue ici), `detruire = NULL` (aucune
 * ressource au-dela du contexte lui-meme).
 *
 * PAS cable dans le hub/main_panel ici -- une future tache s'en charge (voir
 * task-2-brief.md, "Do NOT wire this into the hub/main_panel"). Ce fichier
 * n'expose que le descripteur ECRAN_ACTIONS.
 *
 * `ecran_actions_ctx_t` est expose ici plutot qu'opaque, meme raison que
 * ecran_menu_reglages_ctx_t (voir son en-tete) : host-test/tests/
 * test_ecran_actions.c relit `boutons[i]` pour prouver le libelle et le
 * rappel de clic attaches, sans jamais regarder un pixel. */
#pragma once

#include "ecran.h"
#include "lvgl.h"

/* Sept cases, ORDRE FIXE -- meme convention de tableau que `boutons[i]` dans
 * ecran_menu_reglages.h : `boutons[i]` est TOUJOURS le bouton de la case
 * `i`, quel que soit l'ordre de creation interne. Ordre choisi : celui du
 * tableau du brief (task-2-brief.md), verbatim. */
#define ECRAN_ACTIONS_CASE_MOVE    0
#define ECRAN_ACTIONS_CASE_EXTRUDE 1
#define ECRAN_ACTIONS_CASE_FAN     2
#define ECRAN_ACTIONS_CASE_TEMP    3
#define ECRAN_ACTIONS_CASE_MACROS  4
#define ECRAN_ACTIONS_CASE_DISABLE 5
#define ECRAN_ACTIONS_CASE_CONSOLE 6
#define ECRAN_ACTIONS_NB           7

typedef struct ecran_actions_ctx_s {
    lv_obj_t *zone_grille;
    lv_obj_t *boutons[ECRAN_ACTIONS_NB];
} ecran_actions_ctx_t;

extern const ecran_desc_t ECRAN_ACTIONS;
