/* Écran Bed Level (sous-projet "panneaux KlipperScreen", tâche 4) : le
 * panneau le plus simple de la série -- une grille 2x2 de quatre boutons qui
 * postent chacun une commande FIXE, déjà construite par
 * klipper_gcode_niveau_lit() (tâche 1) : "Screws Adjust"
 * (SCREWS_TILT_CALCULATE), "Z-Tilt" (Z_TILT_ADJUST), "QGL"
 * (QUAD_GANTRY_LEVEL), "Disable Motors" (M84). Aucun paramètre à relire au
 * clic, aucun sélecteur de pas -- contrairement à ecran_reglage_fin.c/
 * ecran_zcalibrate.c, ce panneau n'a besoin d'aucun état interne au-delà des
 * quatre boutons eux-mêmes.
 *
 * Layout (voir bed_level.png du brief) : une grille 2x2 de boutons texte
 * pleine taille, ORDRE FIXE "Screws Adjust, Z-Tilt, QGL, Disable Motors"
 * (brief). La visualisation "coins" du screenshot (quatre indicateurs de
 * hauteur par vis, mis à jour au fil de SCREWS_TILT_CALCULATE) est
 * DÉLIBÉRÉMENT ABSENTE -- Klipper ne renvoie ces hauteurs que via un message
 * console (`gcode_response` de Moonraker), et rien dans ce dépôt ne capture
 * aujourd'hui ce flux (ni klipper_gcode.h, ni etat_klipper_t, ni
 * moonraker_ws.c) -- même constat exact que le placeholder "Probe Offset: -"
 * de ecran_zcalibrate.h (voir son commentaire de tête). Construire cette
 * visualisation ici reviendrait à inventer une donnée : reporté à une future
 * tâche qui câblerait la capture de `gcode_response` bout en bout.
 *
 * AUCUNE valeur relue, AUCUN grisage (pas de C3) : `mettre_a_jour` peut
 * rester NULL (voir ecran.h, commentaire de `mettre_a_jour`, "peut être NULL
 * si l'écran n'a rien à rafraîchir"). Les quatre commandes sont un envoi PUR,
 * toujours sûr même sur un état backend périmé ou inconnu -- exactement le
 * même raisonnement que la grille de menu du hub (ecran_accueil_hub.c,
 * "la grille de menu N'EST JAMAIS grisée") et les six boutons d'action de
 * ecran_zcalibrate.c ("ne dépendent d'aucune valeur lue ici").
 *
 * `ecran_niveau_lit_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_zcalibrate_ctx_t/ecran_reglage_fin_ctx_t (voir leurs en-têtes) :
 * host-test/tests/test_ecran_niveau_lit.c relit les widgets directement,
 * sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"

/* Quatre boutons, ORDRE FIXE (brief : "Screws Adjust, Z-Tilt, QGL, Disable
 * Motors") -- réutilisé par la boucle de construction (BOUTON_DEFS dans le
 * .c) et par host-test/tests/test_ecran_niveau_lit.c pour retrouver le bon
 * bouton, même convention que ECRAN_ZCALIBRATE_BOUTON_xxx /
 * ECRAN_REGLAGE_FIN_BOUTON_xxx. */
#define ECRAN_NIVEAU_LIT_BOUTON_SCREWS  0
#define ECRAN_NIVEAU_LIT_BOUTON_ZTILT   1
#define ECRAN_NIVEAU_LIT_BOUTON_QGL     2
#define ECRAN_NIVEAU_LIT_BOUTON_DISABLE 3
#define ECRAN_NIVEAU_LIT_BOUTON_NB      4

typedef struct ecran_niveau_lit_ctx_s {
    /* Quatre boutons : voir ECRAN_NIVEAU_LIT_BOUTON_* plus haut pour
     * l'indexation. `user_data` de chaque bouton est directement l'action
     * klipper_lit_action_t associée (castée en lv_obj_user_data via un
     * tableau statique dans le .c) -- pas de sous-structure séparée, un seul
     * enum suffit à retrouver la bonne branche (contrairement à
     * ecran_reglage_fin_bouton_info_t/ecran_zcalibrate_bouton_info_t, qui
     * doivent aussi relire `ctx` pour un pas/une valeur connue -- inutile
     * ici, aucun bouton n'a besoin du contexte). */
    lv_obj_t *boutons[ECRAN_NIVEAU_LIT_BOUTON_NB];
} ecran_niveau_lit_ctx_t;

extern const ecran_desc_t ECRAN_NIVEAU_LIT;
