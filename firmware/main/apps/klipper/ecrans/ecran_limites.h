/* Écran Limits (sous-projet "panneaux KlipperScreen", tâche 5) : quatre
 * limites globales de vitesse/accélération que Klipper expose sur l'objet
 * `toolhead` (max_velocity, max_accel, square_corner_velocity,
 * max_accel_to_decel) -- KlipperScreen les regroupe sur un seul panneau
 * "Limits" pour la même raison que Fine Tune regroupe Speed/Flow/Z-offset :
 * des réglages qu'on ajuste à petits pas, en observant l'effet sur
 * l'impression en cours.
 *
 * Layout (voir limits.png du brief) : QUATRE lignes label + valeur relue +
 * boutons `-`/`+`, dans l'ordre Max Velocity / Max Acceleration / Square
 * Corner Velocity / Accel to Decel -- le MÊME ordre que l'énum
 * `klipper_lim_champ_t` (klipper_gcode.h), ce qui permet d'utiliser cette
 * énum directement comme index de ligne (voir PAS[]/ecran_limites_ctx_t
 * ci-dessous, pas de table de correspondance séparée). Pas de sélecteur de
 * pas : chaque ligne a un pas FIXE, imposé par le brief --
 *   Max Velocity            ±10   mm/s
 *   Max Acceleration        ±100  mm/s^2
 *   Square Corner Velocity  ±1    mm/s
 *   Accel to Decel          ±100  mm/s^2
 *
 * Écart assumé vis-à-vis de KlipperScreen (documenté explicitement par le
 * brief, pas un oubli) : AUCUN bouton "Reset" dans cette v1. Le Reset de
 * KlipperScreen relit `printer.configfile.settings` (la valeur de config,
 * pas la valeur courante) pour revenir à la limite déclarée dans
 * printer.cfg -- ce jalon n'a pas de canal pour lire configfile.settings
 * (rpc_lire_macros() lit `configfile.config`, une structure différente, à
 * un autre usage). Ajouter un Reset qui renverrait vers une valeur ARBITRAIRE
 * (pas celle de printer.cfg) serait pire que pas de bouton du tout -- un
 * utilisateur qui clique "Reset" sur une machine CoreXY à square_corner
 * élevé ne doit pas se retrouver avec une valeur Voron par défaut. Chaque
 * ligne garde donc uniquement `-`/`+` autour de la valeur relue depuis
 * `etat_klipper_t.limite_*` (core/etat_klipper.h, tâche 5).
 *
 * Grisage (C3) et "pas encore reçu" : la même couleur grise sert aux DEUX
 * cas (donnees_perimees ET valeur encore à 0, c'est-à-dire jamais reçue de
 * Klipper -- voir etat_klipper.h) -- un choix délibéré plutôt que deux
 * traitements distincts, documenté dans le .c (mettre_a_jour()) : dans les
 * deux cas le nombre affiché n'est pas une lecture fraîche fiable, la
 * distinction n'aurait aucune valeur actionnable pour l'utilisateur. Une
 * valeur à 0 s'affiche en plus comme "-" (jamais "0 mm/s", qui laisserait
 * croire à une limite réellement nulle).
 *
 * `ecran_limites_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_reglage_fin_ctx_t (voir son .h) : host-test/tests/test_ecran_limites.c
 * relit les libellés/couleurs directement, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "klipper_gcode.h"
#include "lvgl.h"

/* Quatre lignes, ORDRE FIXE -- identique à klipper_lim_champ_t
 * (klipper_gcode.h), réutilisée directement comme index de ligne. */
#define ECRAN_LIMITES_LIGNE_NB 4

/* Huit boutons +/- (deux par ligne), ORDRE FIXE -- réutilisé par la boucle de
 * construction (BOUTON_DEFS dans le .c) et par
 * host-test/tests/test_ecran_limites.c, même convention que
 * ECRAN_REGLAGE_FIN_BOUTON_* (ecran_reglage_fin.h). */
#define ECRAN_LIMITES_BOUTON_VELOCITY_NEG       0
#define ECRAN_LIMITES_BOUTON_VELOCITY_POS       1
#define ECRAN_LIMITES_BOUTON_ACCEL_NEG          2
#define ECRAN_LIMITES_BOUTON_ACCEL_POS          3
#define ECRAN_LIMITES_BOUTON_SQV_NEG            4
#define ECRAN_LIMITES_BOUTON_SQV_POS            5
#define ECRAN_LIMITES_BOUTON_ACCEL_TO_DECEL_NEG 6
#define ECRAN_LIMITES_BOUTON_ACCEL_TO_DECEL_POS 7
#define ECRAN_LIMITES_BOUTON_NB                 8

/* user_data d'un rappel de clic +/- : le contexte de l'écran (pour relire la
 * dernière valeur connue de CE champ, AU MOMENT DU CLIC -- jamais depuis
 * l'état backend directement, même discipline que
 * ecran_reglage_fin_bouton_info_t pour Speed/Flow) et le champ + signe que ce
 * bouton précis représente. */
typedef struct {
    struct ecran_limites_ctx_s *ctx;   /* jamais NULL une fois construire() passé */
    klipper_lim_champ_t         champ;
    int8_t                       signe; /* +1 ou -1 */
} ecran_limites_bouton_info_t;

typedef struct ecran_limites_ctx_s {
    /* Valeurs relues, une par ligne, INDEXÉES par klipper_lim_champ_t (donc
     * dans l'ordre du panneau : Velocity, Accel, SQV, Accel to Decel). */
    lv_obj_t *valeurs[ECRAN_LIMITES_LIGNE_NB];

    /* Huit boutons +/- : voir ECRAN_LIMITES_BOUTON_* plus haut pour
     * l'indexation. `bouton_infos[i].ctx` pointe toujours vers ce contexte
     * -- posé une fois par construire(), jamais recalculé. */
    lv_obj_t                     *boutons[ECRAN_LIMITES_BOUTON_NB];
    ecran_limites_bouton_info_t   bouton_infos[ECRAN_LIMITES_BOUTON_NB];

    /* Dernières valeurs vues par mettre_a_jour(), INDEXÉES par
     * klipper_lim_champ_t, relues par bouton_cb() AU MOMENT DU CLIC -- jamais
     * depuis l'état backend directement (même discipline que
     * vitesse_pct_connue/flux_pct_connue dans ecran_reglage_fin_ctx_t). Une
     * valeur à 0.0f signifie "pas encore reçue", même convention que
     * etat_klipper_t (core/etat_klipper.h) : bouton_cb() ne la traite pas
     * différemment, elle sert simplement de base à `± pas`. */
    float valeurs_connues[ECRAN_LIMITES_LIGNE_NB];
} ecran_limites_ctx_t;

extern const ecran_desc_t ECRAN_LIMITES;
