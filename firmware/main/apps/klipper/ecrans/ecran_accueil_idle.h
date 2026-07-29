/* Écran d'accueil Klipper AU REPOS (tâche 3, jalon 3b) : l'état complet de
 * la machine (températures de tous les chauffeurs présents, position,
 * outil actif) affiché selon le palier d'outils courant (klipper_paliers.h)
 * -- machine mono-extrudeur (grandes tuiles), changeur d'outils 2-4 têtes
 * (grille 2x2/2x3, outil actif marqué) ou 5-8 têtes (grille 2x4/2x5
 * compacte, sans consigne inline). Voir accueil_choix.h pour le choix,
 * décidé au démarrage, entre cet écran et ECRAN_ACCUEIL (jalon 2b,
 * impression en cours).
 *
 * Contrôles (pad de jog, homing) et rangée Macros : réservés ici en
 * PLACEHOLDER seulement -- posés pour que la mise en page soit figée et
 * capturée avant que les tâches 4/5/7 n'y accrochent leurs widgets réels.
 *
 * `ecran_accueil_idle_ctx_t` est exposé ici plutôt qu'opaque, même raison
 * que ecran_accueil_ctx_t (voir son en-tête) : host-test/tests/
 * test_ecran_accueil_idle.c relit les libellés via lv_label_get_text() pour
 * prouver ce que mettre_a_jour() écrit sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"

/* Une cellule de température : nom court ("T0".."T7", "Bed"), valeur
 * courante (police du palier), consigne (masquée -- LV_OBJ_FLAG_HIDDEN,
 * jamais NULL -- au palier COMPACT, voir ecran_accueil_idle.c). Toujours
 * créée, jamais NULL : même politique que ctx->bouton_macros dans
 * ecran_accueil.c, visibilité recalculée à chaque mettre_a_jour() plutôt
 * qu'un pointeur à vérifier partout. */
typedef struct {
    lv_obj_t *racine;
    lv_obj_t *nom;
    lv_obj_t *valeur;
    lv_obj_t *consigne;
} ecran_accueil_idle_cellule_t;

/* Une par extrudeur possible plus le plateau (voir KLIPPER_EXTRUDEURS_MAX
 * dans etat_klipper.h) : le pool est dimensionné au pire cas (palier
 * COMPACT, 8 têtes) une fois pour toutes -- jamais redimensionné au fil des
 * mettre_a_jour() successifs, seules la géométrie/police/visibilité de
 * chaque cellule suivent le palier courant. */
#define ECRAN_ACCUEIL_IDLE_CELLULES_MAX (KLIPPER_EXTRUDEURS_MAX + 1)

typedef struct {
    ecran_accueil_idle_cellule_t cellules[ECRAN_ACCUEIL_IDLE_CELLULES_MAX];
    lv_obj_t *position;        /* "X:.. Y:.. Z:.." (1 decimale, "--" si l'axe n'est pas reference) */
    lv_obj_t *outil_actif_nom; /* "Active: T.." / "Active: --" (aucun extrudeur) */
    lv_obj_t *zone_controles;  /* conteneur reserve (pad de jog + homing, taches 4/5) */
    lv_obj_t *label_controles; /* "Controls", placeholder tache 3 */
    lv_obj_t *bouton_macros;   /* placeholder tache 3, cablage reel tache 7 */
    lv_obj_t *label_macros;    /* enfant direct de bouton_macros, pour le regrisage */
} ecran_accueil_idle_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL_IDLE;
