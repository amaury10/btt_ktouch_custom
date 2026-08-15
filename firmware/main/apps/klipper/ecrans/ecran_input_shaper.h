/* Écran Input Shaper (spec 2026-08-15-bed-mesh-input-shaper-design.md) --
 * remplace le stub : affiche et règle type + fréquence du shaper par axe.
 *
 * Choix d'interaction : le bouton de type CYCLE dans la liste (zv, mzv,
 * zvd, ei, 2hump_ei, 3hump_ei) et envoie SET_INPUT_SHAPER à chaque tap --
 * le libellé affiché vient TOUJOURS de l'état distant (etat_klipper_t,
 * fusionné du flux WS), jamais d'un état local : l'écran ne peut pas
 * diverger de l'imprimante (et un selecteur_choix n'a pas de setter pour
 * refléter l'état distant, voir son header). La fréquence passe par le
 * clavier numérique, bornes 10-150 Hz. Réglage VOLATILE côté Klipper
 * (perdu au redémarrage firmware) : mention fixe à l'écran. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *bouton_type_x;  /* libellé = type courant distant */
    lv_obj_t *bouton_freq_x;  /* libellé = "NN.N Hz" */
    lv_obj_t *bouton_type_y;
    lv_obj_t *bouton_freq_y;
    /* Copies du dernier état affiché -- relues par les rappels de tap
     * (cycle = type courant + 1) et par le clavier (valeur initiale). */
    char  type_x[12];
    char  type_y[12];
    float freq_x;
    float freq_y;
    bool  axe_y_en_saisie; /* le clavier ouvert vise Y (sinon X) */
} ecran_input_shaper_ctx_t;

extern const ecran_desc_t ECRAN_INPUT_SHAPER;
