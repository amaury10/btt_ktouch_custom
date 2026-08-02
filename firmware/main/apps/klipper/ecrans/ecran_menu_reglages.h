/* Sous-menu Configuration (sous-projet "panneaux KlipperScreen", tache 8) :
 * la grille qui relie les douze panneaux de reglage entre eux -- Fine Tune,
 * Z Calibrate, Bed Level, Limits, Retraction (tache 2-6), Network (existant,
 * sous-projet 7 tache 4) et les six stubs Power/Bed Mesh/Input
 * Shaper/Spoolman/Updater/Console (tache 7). Sans ce sous-menu, aucun de ces
 * panneaux n'est atteignable depuis le hub -- c'est l'epine dorsale de
 * navigation de tout le catalogue KlipperScreen de ce depot.
 *
 * ATTENTION COLLISION DE NOMS : `ECRAN_CONFIGURATION` / ecran_configuration.h
 * EXISTENT DEJA et designent l'ecran de PREMIERE configuration de l'appareil
 * (adresse imprimante + type machine + Save, montre au boot si non
 * configure -- voir app_main.c/reglages_configures()) -- ce fichier n'y
 * touche pas. Ce sous-menu-ci s'appelle donc ECRAN_MENU_REGLAGES (id
 * "menu_reglages"), meme si son titre AFFICHE est "Configuration" (le
 * libelle KlipperScreen attendu par l'utilisateur pour ce sous-menu -- voir
 * task-8-brief.md).
 *
 * Idiome repris de la grille de menu de ecran_accueil_hub.c (cases a taille
 * fixe, position calculee, `_Static_assert` de non-debordement + clearance
 * du bandeau) mais SANS zone de temperature au-dessus : 12 cases (3 colonnes
 * x 4 lignes) qui remplissent directement la largeur du contenu. Menu
 * purement statique : `mettre_a_jour = NULL` (rien a rafraichir, rien a
 * griser -- meme choix que la grille de menu du hub, qui ne grise jamais ses
 * boutons), `detruire = NULL` (aucune ressource au-dela du contexte lui-meme).
 *
 * `ecran_menu_reglages_ctx_t` est expose ici plutot qu'opaque, meme raison
 * que ecran_accueil_hub_ctx_t (voir son en-tete) : host-test/tests/
 * test_ecran_menu_reglages.c relit `boutons[i]` pour prouver le libelle et le
 * rappel de clic attaches, sans jamais regarder un pixel. */
#pragma once

#include "ecran.h"
#include "lvgl.h"

/* Grille 3 colonnes x 4 lignes, ORDRE FIXE -- meme convention de tableau que
 * `menu_boutons[i]`/ECRAN_ACCUEIL_HUB_MENU_* dans ecran_accueil_hub.h :
 * `boutons[i]` est TOUJOURS le bouton de la case `i`, quel que soit l'ordre
 * de creation interne. Ordre choisi : les cinq panneaux de reglage propres a
 * ce jalon (Fine Tune, Z Calibrate, Bed Level, Limits, Retraction), puis
 * Network (existant), puis les six stubs dans l'ordre du brief de la tache 7. */
#define ECRAN_MENU_REGLAGES_CASE_FINE_TUNE     0
#define ECRAN_MENU_REGLAGES_CASE_ZCALIBRATE    1
#define ECRAN_MENU_REGLAGES_CASE_BED_LEVEL     2
#define ECRAN_MENU_REGLAGES_CASE_LIMITS        3
#define ECRAN_MENU_REGLAGES_CASE_RETRACTION    4
#define ECRAN_MENU_REGLAGES_CASE_NETWORK       5
#define ECRAN_MENU_REGLAGES_CASE_POWER         6
#define ECRAN_MENU_REGLAGES_CASE_BED_MESH      7
#define ECRAN_MENU_REGLAGES_CASE_INPUT_SHAPER  8
#define ECRAN_MENU_REGLAGES_CASE_SPOOLMAN      9
#define ECRAN_MENU_REGLAGES_CASE_UPDATER      10
#define ECRAN_MENU_REGLAGES_CASE_CONSOLE      11
#define ECRAN_MENU_REGLAGES_NB                12

typedef struct ecran_menu_reglages_ctx_s {
    lv_obj_t *zone_grille;
    lv_obj_t *boutons[ECRAN_MENU_REGLAGES_NB];
} ecran_menu_reglages_ctx_t;

extern const ecran_desc_t ECRAN_MENU_REGLAGES;
