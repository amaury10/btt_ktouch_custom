/* Écran Bed Mesh (spec 2026-08-15-bed-mesh-input-shaper-design.md) --
 * remplace le stub : carte de chaleur de la matrice mesurée
 * (bed_mesh_store.h, alimenté par le flux WS via bed_mesh_parse.h) +
 * boutons Calibrate / Clear (confirmations, gcode via le chemin standard).
 *
 * La grille de cellules est (re)créée à CHAQUE changement de génération du
 * store -- un mesh change à la calibration, pas en continu : recréer
 * nb_x*nb_y petits lv_obj (25 pour un 5x5, 225 au pire) à cette fréquence
 * est plus sobre que 225 objets permanents cachés. Contexte exposé, même
 * raison que les autres écrans (tests host par libellés). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *entete;           /* "Profile: ...  Z: min .. max" ou "No mesh..." */
    lv_obj_t *zone_grille;      /* conteneur ; les cellules sont ses enfants */
    lv_obj_t *bouton_calibrer;
    lv_obj_t *bouton_effacer;
    uint32_t  derniere_generation;
    uint8_t   nb_cellules_x;    /* dimensions de la grille actuellement construite */
    uint8_t   nb_cellules_y;
} ecran_bed_mesh_ctx_t;

extern const ecran_desc_t ECRAN_BED_MESH;
