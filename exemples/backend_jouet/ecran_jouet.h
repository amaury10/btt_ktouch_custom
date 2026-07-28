/* Ecran jouet (tache 11) : contrat de ui/ecran.h rempli sans toucher une
 * ligne de ui/ -- un titre (barre d'etat), la valeur du compteur, un bouton
 * Reset. Voir README.md a cote de ce fichier. */
#pragma once

#include <stdbool.h>

#include "ecran.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *valeur;           /* libelle "Count: N" */
    lv_obj_t *bouton;           /* Reset -- envoie "reset" via ui_commander() */
    bool      donnees_perimees; /* relu par le rappel de clic, meme garde que
                                  * ecran_accueil.c (LV_STATE_DISABLED bloque
                                  * deja le tactile reel, cette valeur reste
                                  * la garde honnete sous un evenement envoye
                                  * directement, voir host-test/tests/
                                  * test_commandes.c) */
} ecran_jouet_ctx_t;

extern const ecran_desc_t ECRAN_JOUET;
