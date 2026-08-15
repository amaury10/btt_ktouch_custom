/* Sous-menu Configuration (sous-projet "panneaux KlipperScreen", tache 8) :
 * la grille qui relie onze panneaux de reglage entre eux -- Z Calibrate, Bed
 * Level, Limits, Retraction (tache 2-6), Network (existant, sous-projet 7
 * tache 4) et les six stubs Power/Bed Mesh/Input Shaper/Spoolman/Updater/
 * Console (tache 7). Sans ce sous-menu, aucun de ces panneaux n'est
 * atteignable depuis le hub -- c'est l'epine dorsale de navigation de tout le
 * catalogue KlipperScreen de ce depot.
 *
 * Fine Tune n'est PLUS ici (sous-projet "refonte IHM KlipperScreen", tache 5) :
 * KlipperScreen le place dans le flux d'impression (Job Status), pas dans
 * Configuration -- voir ecran_accueil.c, qui l'a recu a la place. La grille
 * reste 3 colonnes x 4 lignes (12 cases) pour ne pas retoucher sa geometrie
 * deja revue ; la douzieme case (derniere rangee) reste simplement vide.
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

/* Grille 3 colonnes x 4 lignes (12 cases, ONZE peuplees -- voir le
 * commentaire de tete sur Fine Tune), ORDRE FIXE -- meme convention de
 * tableau que `menu_boutons[i]`/ECRAN_ACCUEIL_HUB_MENU_* dans
 * ecran_accueil_hub.h : `boutons[i]` est TOUJOURS le bouton de la case `i`,
 * quel que soit l'ordre de creation interne. Ordre choisi : les quatre
 * panneaux de reglage propres a ce jalon (Z Calibrate, Bed Level, Limits,
 * Retraction), puis Network (existant), puis les six stubs dans l'ordre du
 * brief de la tache 7. */
#define ECRAN_MENU_REGLAGES_CASE_ZCALIBRATE    0
#define ECRAN_MENU_REGLAGES_CASE_BED_LEVEL     1
#define ECRAN_MENU_REGLAGES_CASE_LIMITS        2
#define ECRAN_MENU_REGLAGES_CASE_RETRACTION    3
#define ECRAN_MENU_REGLAGES_CASE_NETWORK       4
#define ECRAN_MENU_REGLAGES_CASE_POWER         5
#define ECRAN_MENU_REGLAGES_CASE_BED_MESH      6
#define ECRAN_MENU_REGLAGES_CASE_INPUT_SHAPER  7
#define ECRAN_MENU_REGLAGES_CASE_SPOOLMAN      8
#define ECRAN_MENU_REGLAGES_CASE_UPDATER       9
#define ECRAN_MENU_REGLAGES_CASE_CONSOLE      10
/* Gestion de parc (2026-08-15) : navigation vers l'ecran Parc. */
#define ECRAN_MENU_REGLAGES_CASE_PRINTERS     11
#define ECRAN_MENU_REGLAGES_NB                12

typedef struct ecran_menu_reglages_ctx_s {
    lv_obj_t *zone_grille;
    lv_obj_t *boutons[ECRAN_MENU_REGLAGES_NB];
} ecran_menu_reglages_ctx_t;

extern const ecran_desc_t ECRAN_MENU_REGLAGES;
