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

/* 15 -> 25 (retour matériel 2026-08-15) : la CR-10 S5 sonde en 21x21, le
 * plafond de 15 tronquait sa carte réelle. 25 couvre les configs Klipper
 * usuelles ; le struct passe à ~2,6 Ko -- toujours PSRAM-only, le contrat
 * "jamais sur une pile" devient d'autant plus impératif. */
#define BED_MESH_MAX 25 /* points max par axe retenus (troncature au-delà, signalée) */

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

/* Compteur monotone, +1 à chaque bed_mesh_definir() ET à chaque
 * bed_mesh_profils_definir() -- même idiome que usb_fichiers_generation()
 * (redessin sur changement seulement, et somme des générations externes de
 * l'habillage dans app_main). Un seul compteur pour la carte ET la liste de
 * profils : l'écran Bed Mesh consomme les deux, un compteur par source ne
 * lui ferait rien redessiner de plus. */
uint32_t bed_mesh_generation(void);

/* ------------------------------------------------------------------------
 * Liste des profils sauvegardés (feature "liste de profils", 2026-08-15)
 * ------------------------------------------------------------------------
 * Les NOMS seuls -- jamais les matrices : l'objet `profiles` de Moonraker
 * porte chaque profil avec sa matrice complète, c'est précisément ce qu'on
 * a exclu de l'abonnement (voir PARAMS dans moonraker_rpc.c). Rempli par la
 * tâche WS depuis une requête ponctuelle printer.objects.query, lu par
 * l'écran Bed Mesh. ~230 octets : une copie de pile ponctuelle est
 * acceptable (même politique que power_devices_t). */
#define BED_MESH_PROFILS_MAX    8
#define BED_MESH_PROFIL_NOM_MAX 24 /* même taille que bed_mesh_t.profil */

typedef struct {
    uint8_t nb;                                                /* <= BED_MESH_PROFILS_MAX */
    bool    tronques;                                          /* la source en portait plus */
    char    noms[BED_MESH_PROFILS_MAX][BED_MESH_PROFIL_NOM_MAX];
} bed_mesh_profils_t;

/* Remplace la liste (copie sous verrou, +1 génération -- le MÊME compteur
 * que bed_mesh_definir(), voir bed_mesh_generation()). NULL = no-op. */
void bed_mesh_profils_definir(const bed_mesh_profils_t *profils);

/* Copie la liste dans `dest` (~230 o : la pile est acceptable ici,
 * contrairement à bed_mesh_lire()). NULL = no-op ; jamais définie = zéros. */
void bed_mesh_profils_lire(bed_mesh_profils_t *dest);

/* Position machine [x, y] du point [ligne][colonne] de la matrice retenue :
 * interpolation linéaire entre mesh_min et mesh_max (la grille de sondage
 * Klipper est régulière). Axe à un seul point : la borne min. Rend false
 * (sans toucher x/y) si mesh est NULL, absent, ou l'indice hors [0..nb). */
bool bed_mesh_position_point(const bed_mesh_t *mesh, uint8_t ligne, uint8_t colonne,
                             float *x, float *y);
