/* Sous-projet "refonte IHM KlipperScreen", tache 3 : l'ecran Homing -- quatre
 * boutons (Home All/X/Y/Z) vers klipper_gcode_home() (existant, tache 1 du
 * jalon panneaux). Aucun parametre a relire au clic au-dela du masque
 * d'axes FIGE par bouton, aucun etat interne au-dela des quatre boutons
 * eux-memes -- meme constat exact que ecran_niveau_lit.h (ce panneau est son
 * jumeau structurel : copie de l'idiome, seule la table d'actions change).
 *
 * Layout (brief task-3) : une grille 2x2 de boutons texte pleine taille,
 * ORDRE FIXE "Home All, Home X, Home Y, Home Z" -- geometrie verifiee par
 * _Static_assert, meme discipline que ecran_niveau_lit.c/ecran_reglage_fin.c/
 * ecran_zcalibrate.c/ecran_accueil_hub.c.
 *
 * AUCUNE valeur relue, AUCUN grisage (pas de C3) : `mettre_a_jour` reste NULL
 * (voir ecran.h, commentaire de `mettre_a_jour`, "peut etre NULL si l'ecran
 * n'a rien a rafraichir"). Les quatre commandes sont un envoi PUR, DIRECT,
 * SANS confirmation -- le homing est non destructif (meme politique que
 * l'ancien bouton Home du rail, voir le brief task-3), exactement le meme
 * raisonnement que la grille de menu du hub (ecran_accueil_hub.c, "la grille
 * de menu N'EST JAMAIS grisee") et les quatre boutons de ecran_niveau_lit.c.
 *
 * `ecran_homing_ctx_t` est expose ici plutot qu'opaque, meme raison que
 * ecran_niveau_lit_ctx_t (voir son en-tete) : host-test/tests/
 * test_ecran_homing.c relit les widgets directement, sans jamais regarder un
 * pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"

/* Quatre boutons, ORDRE FIXE (brief : "Home All, Home X, Home Y, Home Z") --
 * reutilise par la boucle de construction (ACTIONS/LIBELLES dans le .c) et
 * par host-test/tests/test_ecran_homing.c pour retrouver le bon bouton,
 * meme convention que ECRAN_NIVEAU_LIT_BOUTON_xxx. */
#define ECRAN_HOMING_BOUTON_ALL 0
#define ECRAN_HOMING_BOUTON_X   1
#define ECRAN_HOMING_BOUTON_Y   2
#define ECRAN_HOMING_BOUTON_Z   3
#define ECRAN_HOMING_BOUTON_NB  4

typedef struct ecran_homing_ctx_s {
    /* Quatre boutons : voir ECRAN_HOMING_BOUTON_* plus haut pour
     * l'indexation. `user_data` de chaque bouton est directement le masque
     * d'axes uint8_t associe (voir MASQUES dans le .c) -- pas de
     * sous-structure separee, meme choix que ecran_niveau_lit_ctx_t (aucun
     * bouton n'a besoin du contexte). */
    lv_obj_t *boutons[ECRAN_HOMING_BOUTON_NB];
} ecran_homing_ctx_t;

extern const ecran_desc_t ECRAN_HOMING;
