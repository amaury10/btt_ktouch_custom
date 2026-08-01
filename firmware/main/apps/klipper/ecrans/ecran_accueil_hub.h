/* Écran Accueil-hub (tâche 5, refonte accueil/déplacer) : le nouvel écran
 * d'accueil, qui a REMPLACÉ l'ancien ecran_accueil_idle.c (supprimé en
 * tâche 7 -- voir task-5-brief.md, "Où cette tâche s'insère"). Contrairement
 * à l'ancien idle, ce hub n'a NI jog NI homing NI préréglages NI clavier de
 * consigne : juste les tuiles de température multi-tête (mêmes paliers,
 * même géométrie que l'ancien idle -- klipper_paliers.h) et une
 * grille de 6 cases de menu qui renvoient vers les écrans dédiés (Déplacer,
 * Températures et Extruder naviguent réellement ; Ventilateurs/Imprimer/
 * Réglages restent des sous-projets FUTURS, no-op scopé pour l'instant, voir
 * ecran_accueil_hub.c).
 *
 * L'intégration dans une racine `[rail | conteneur-nav]` et le recâblage de
 * la navigation (quel écran s'empile au démarrage) arrivent en tâche 6 --
 * ce fichier ne s'en préoccupe pas : `construire()` reçoit un `parent`
 * plein cadre comme n'importe quel autre écran (voir ecran.h), exactement
 * comme ecran_deplacer.c.
 *
 * `ecran_accueil_hub_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_deplacer_ctx_t (voir son en-tête) : host-test/tests/
 * test_ecran_accueil_hub.c relit les libellés/couleurs via
 * lv_label_get_text()/lv_obj_get_style_text_color() pour prouver ce que
 * mettre_a_jour() écrit sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"

/* Une cellule de température : copie EXACTE de la structure équivalente de
 * l'ancien ecran_accueil_idle.c -- dupliquée ici DÉLIBÉRÉMENT plutôt
 * qu'extraite en helper partagé, parce que l'ancien idle devait être
 * SUPPRIMÉ en tâche 7 (voir task-5-brief.md, section "Réutilisation (DRY)",
 * et task-7-brief.md pour la suppression effective) : la duplication était
 * transitoire, un partage aurait dû être défait un jalon plus tard pour
 * rien. */
typedef struct {
    lv_obj_t *racine;
    lv_obj_t *nom;
    lv_obj_t *valeur;
    lv_obj_t *consigne;
} ecran_accueil_hub_cellule_t;

/* Une par extrudeur possible plus le plateau (voir KLIPPER_EXTRUDEURS_MAX
 * dans etat_klipper.h) -- même raisonnement que dans l'ancien
 * ecran_accueil_idle.c : le pool est dimensionné au pire cas
 * (palier COMPACT, 8 têtes) une fois pour toutes. */
#define ECRAN_ACCUEIL_HUB_CELLULES_MAX (KLIPPER_EXTRUDEURS_MAX + 1)

/* Grille de 6 cases de menu, ORDRE FIXE -- résolution d'ambiguïté déjà
 * tranchée par task-5-brief.md : DEPLACER (ECRAN_DEPLACER, tâche 4),
 * TEMPERATURES (ECRAN_TEMPERATURES, sous-projet 2 tâche 2) et EXTRUDER
 * (ECRAN_EXTRUDER, sous-projet 3 tâche 2) naviguent réellement ; les trois
 * autres pointent vers des écrans de sous-projets FUTURS -- no-op scopé
 * (aucun rappel de clic attaché, voir ecran_accueil_hub.c), PAS un écran
 * placeholder bricolé. Les tuiles de
 * température (`cellules[i].racine`) naviguent elles aussi vers
 * ECRAN_TEMPERATURES au tap, même rappel partagé -- voir
 * ouvrir_temperatures_cb() dans ecran_accueil_hub.c. */
#define ECRAN_ACCUEIL_HUB_MENU_DEPLACER     0
#define ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES 1
#define ECRAN_ACCUEIL_HUB_MENU_EXTRUDER     2
#define ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS 3
#define ECRAN_ACCUEIL_HUB_MENU_IMPRIMER     4
#define ECRAN_ACCUEIL_HUB_MENU_REGLAGES     5
#define ECRAN_ACCUEIL_HUB_MENU_NB           6

typedef struct ecran_accueil_hub_ctx_s {
    ecran_accueil_hub_cellule_t cellules[ECRAN_ACCUEIL_HUB_CELLULES_MAX];

    /* Grille de menu (voir ECRAN_ACCUEIL_HUB_MENU_* ci-dessus pour
     * l'indexation) : `zone_menu` est le conteneur qui les porte,
     * `menu_boutons[i]` est TOUJOURS le bouton de la case `i`, quel que
     * soit l'ordre de création interne -- même convention que
     * `rail_t.boutons[i]` (rail.h, tâche 3). Contenu statique (aucun champ
     * de contexte par bouton, contrairement aux jog/home/preset_infos des
     * autres écrans) : ces boutons n'ont besoin de relire aucun état au
     * moment du clic, DEPLACER et TEMPERATURES n'ayant qu'à empiler
     * respectivement ECRAN_DEPLACER et ECRAN_TEMPERATURES. */
    lv_obj_t *zone_menu;
    lv_obj_t *menu_boutons[ECRAN_ACCUEIL_HUB_MENU_NB];
} ecran_accueil_hub_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL_HUB;
