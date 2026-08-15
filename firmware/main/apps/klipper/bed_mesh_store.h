/* Store dédié du maillage de lit (spec 2026-08-15-bed-mesh-input-shaper-
 * design.md) : la matrice mesurée par BED_MESH_CALIBRATE, écrite par la
 * tâche WS (via bed_mesh_parse.h), lue par l'écran Bed Mesh.
 *
 * POURQUOI hors etat_klipper_t (même choix que klipper_fichiers/
 * usb_fichiers) : une matrice 15×15 de flottants (~900 o) multipliée par
 * toutes les copies statiques ET les piles qui portent l'état serait
 * exactement la maladie « etat_klipper_t vs piles » de la mémoire du
 * projet. Instance UNIQUE en PSRAM, verrou court, génération -- et le
 * struct fait ~1 Ko : tout LECTEUR passe par un scratch PSRAM, jamais une
 * variable locale (contrat de bed_mesh_lire()). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BED_MESH_MAX 15 /* points max par axe retenus (troncature au-delà, signalée) */

typedef struct {
    bool    present;                      /* une matrice valide est chargée */
    char    profil[24];                   /* profile_name ("" si aucun) */
    float   mesh_min_x, mesh_min_y;
    float   mesh_max_x, mesh_max_y;
    uint8_t nb_x, nb_y;                   /* colonnes (X) / lignes (Y), <= BED_MESH_MAX */
    bool    tronquee;                     /* matrice source plus grande que BED_MESH_MAX */
    float   z[BED_MESH_MAX][BED_MESH_MAX];/* [ligne (Y)][colonne (X)] */
    float   z_min, z_max;                 /* bornes de la matrice RETENUE (recalculées au parse) */
} bed_mesh_t;

/* Remplace le contenu du store (copie sous verrou, +1 génération). NULL =
 * no-op silencieux. */
void bed_mesh_definir(const bed_mesh_t *mesh);

/* Copie le store dans `dest` -- ~1 Ko : `dest` DOIT être un scratch
 * PSRAM/statique, jamais une variable de pile (voir le commentaire de
 * tête). NULL = no-op ; store jamais alloué = zéros. */
void bed_mesh_lire(bed_mesh_t *dest);

/* Compteur monotone, +1 à chaque bed_mesh_definir() -- même idiome que
 * usb_fichiers_generation() (redessin sur changement seulement, et somme
 * des générations externes de l'habillage dans app_main). */
uint32_t bed_mesh_generation(void);
