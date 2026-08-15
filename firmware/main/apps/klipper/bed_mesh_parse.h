/* Parseur PUR du sous-objet JSON "bed_mesh" de Moonraker (spec 2026-08-15-
 * bed-mesh-input-shaper-design.md) -- présent dans l'instantané d'abonnement
 * ET dans les notify_status_update. Fusion PAR CHAMP (même politique que
 * rpc_fusionner_* : un notify partiel ne détruit jamais ce qu'il ne porte
 * pas), troncature signalée au-delà de BED_MESH_MAX, bornes z_min/z_max
 * recalculées quand la matrice change. Testé host (test_bed_mesh.c). */
#pragma once

#include <stdbool.h>

#include "bed_mesh_store.h"

struct cJSON;

/* Rend vrai si `objet` est un objet JSON (même vide) et que la fusion a eu
 * lieu ; faux si `mesh`/`objet` NULL ou `objet` non-objet (mesh inchangé).
 * Une `probed_matrix` vide ([]) éteint `present` (cas BED_MESH_CLEAR). */
bool bed_mesh_fusionner_json(bed_mesh_t *mesh, const struct cJSON *objet);
