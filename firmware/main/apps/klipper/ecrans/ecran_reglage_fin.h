/* Écran Fine Tune (sous-projet "panneaux KlipperScreen", tâche 2) : trois
 * réglages fins que KlipperScreen regroupe sur un seul panneau -- vitesse
 * d'impression (M220), flux d'extrusion (M221), et décalage Z persistant
 * (babystepping, SET_GCODE_OFFSET) -- réunis ici pour la même raison que le
 * panneau qu'il reprend : les trois se règlent typiquement PENDANT une
 * impression en cours, à petits pas répétés, jamais isolément.
 *
 * Layout (voir fine_tune.png du brief) : trois lignes label + valeur relue +
 * boutons `-`/`+` (Z-offset, Speed, Flow, dans cet ordre) ; la ligne
 * Z-offset porte en plus un bouton "Reset" dédié. En dessous, DEUX
 * sélecteurs génériques (`selecteur_choix.h`, réutilisé tel quel --
 * voir ecran_deplacer.c pour le même widget appliqué au pas de jog/à la
 * vitesse) : un pas en pourcent {1, 5, 10, 25} PARTAGÉ par Speed et Flow (un
 * seul sélecteur, pas deux -- le brief autorise explicitement cette
 * mutualisation, "un seul si l'implémenteur juge la double échelle
 * gérable" ; ici les deux réglages partagent la même échelle homogène, un
 * second sélecteur identique n'aurait aucune valeur ajoutée), et un pas en
 * mm {0.01, 0.05} propre à Z (échelle différente, µm plutôt que %, PAS
 * mutualisable avec le premier).
 *
 * Différence structurelle avec Z d'une part et Speed/Flow d'autre part :
 *   - Z passe par klipper_gcode_offset_z(pas_um, false), qui produit
 *     SET_GCODE_OFFSET Z_ADJUST=<pas> MOVE=1 -- Z_ADJUST est déjà un
 *     ajustement RELATIF côté Klipper (contrairement à Z=<valeur>,
 *     absolu) : le bouton +/- envoie donc directement ±pas, sans jamais
 *     avoir besoin de relire `babystep_z_um` au moment du clic.
 *   - Speed/Flow passent par klipper_gcode_vitesse_impression/flux(pct),
 *     qui prennent un pourcentage ABSOLU (M220 S<pct>/M221 S<pct>) : le
 *     bouton +/- doit donc calculer `pct_courant ± pas` puis borner
 *     [1, 300] avant d'envoyer -- `pct_courant` est relu depuis
 *     `vitesse_pct_connue`/`flux_pct_connue`, la DERNIÈRE valeur vue par
 *     mettre_a_jour(), jamais depuis l'état backend au moment du clic --
 *     même discipline que `outil_actif_connu` dans ecran_temperatures.c
 *     ("ce que le clic lit doit être ce que le dernier rendu a montré à
 *     l'utilisateur").
 *
 * `ecran_reglage_fin_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_ventilateurs_ctx_t/ecran_deplacer_ctx_t (voir leurs en-têtes) :
 * host-test/tests/test_ecran_reglage_fin.c relit les libellés/couleurs
 * directement, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "selecteur_choix.h"

/* Pas du sélecteur %, ORDRE FIXE {1, 5, 10, 25} -- partagé par Speed ET Flow
 * (voir le commentaire de tête). Index par défaut 1 (=5) : c'est la valeur
 * que le brief utilise explicitement dans son scénario de test ("simuler
 * clic +Speed (pas 5)") -- host-test/tests/test_ecran_reglage_fin.c et
 * bouton_cb() (le .c) ne doivent donc jamais diverger sur ce défaut. */
#define ECRAN_REGLAGE_FIN_PAS_PCT_NB      4
#define ECRAN_REGLAGE_FIN_PAS_PCT_DEFAUT  1

/* Pas du sélecteur Z (mm), ORDRE FIXE {0.01, 0.05} (10/50 µm). Index par
 * défaut 0 (=0.01 mm) : le pas le plus fin, un babystep involontaire trop
 * large est plus gênant qu'un babystep trop lent à ajuster. */
#define ECRAN_REGLAGE_FIN_PAS_Z_NB     2
#define ECRAN_REGLAGE_FIN_PAS_Z_DEFAUT 0

/* Quel réglage un bouton +/- précis pilote -- voir le commentaire de tête
 * pour pourquoi Z suit un chemin différent (relatif) de SPEED/FLOW
 * (absolu, relu depuis `*_connue`). */
typedef enum {
    ECRAN_REGLAGE_FIN_CIBLE_Z = 0,
    ECRAN_REGLAGE_FIN_CIBLE_SPEED,
    ECRAN_REGLAGE_FIN_CIBLE_FLOW,
} ecran_reglage_fin_cible_t;

/* Six boutons +/-, ORDRE FIXE -- réutilisé par la boucle de construction
 * (BOUTON_DEFS dans le .c) et par host-test/tests/test_ecran_reglage_fin.c
 * pour retrouver le bon bouton, même convention que ECRAN_DEPLACER_JOG_*
 * (ecran_deplacer.h)/ECRAN_VENTILATEURS_PRESET_* (ecran_ventilateurs.h). */
#define ECRAN_REGLAGE_FIN_BOUTON_Z_NEG     0
#define ECRAN_REGLAGE_FIN_BOUTON_Z_POS     1
#define ECRAN_REGLAGE_FIN_BOUTON_SPEED_NEG 2
#define ECRAN_REGLAGE_FIN_BOUTON_SPEED_POS 3
#define ECRAN_REGLAGE_FIN_BOUTON_FLOW_NEG  4
#define ECRAN_REGLAGE_FIN_BOUTON_FLOW_POS  5
#define ECRAN_REGLAGE_FIN_BOUTON_NB        6

/* user_data d'un rappel de clic +/- : le contexte de l'écran (pour relire le
 * pas ET, pour SPEED/FLOW, la dernière valeur connue, AU MOMENT DU CLIC --
 * jamais mis en cache ailleurs, même idiome que ecran_deplacer_jog_info_t)
 * et ce que ce bouton précis représente (cible + signe). */
typedef struct {
    struct ecran_reglage_fin_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    ecran_reglage_fin_cible_t       cible;
    int8_t                          signe; /* +1 ou -1 */
} ecran_reglage_fin_bouton_info_t;

typedef struct ecran_reglage_fin_ctx_s {
    /* Valeurs relues, une par ligne, dans l'ORDRE du panneau (Z, Speed,
     * Flow) -- voir formater_z_mm()/formater_pct() dans le .c pour le
     * format exact ("100 %", "0.00 mm"). */
    lv_obj_t *valeur_z;
    lv_obj_t *valeur_speed;
    lv_obj_t *valeur_flow;

    /* Six boutons +/- : voir ECRAN_REGLAGE_FIN_BOUTON_* plus haut pour
     * l'indexation. `bouton_infos[i].ctx` pointe toujours vers ce contexte
     * -- posé une fois par construire(), jamais recalculé. */
    lv_obj_t                        *boutons[ECRAN_REGLAGE_FIN_BOUTON_NB];
    ecran_reglage_fin_bouton_info_t  bouton_infos[ECRAN_REGLAGE_FIN_BOUTON_NB];

    /* Bouton dédié de la ligne Z-offset -- son rappel n'a besoin d'aucun
     * `cible`/`signe` (toujours offset_z(0, true)), `ctx` posé directement
     * en user_data plutôt qu'une sous-structure à un seul champ. */
    lv_obj_t *bouton_reset_z;

    /* Sélecteurs génériques (voir le commentaire de tête) : `selecteur_pct`
     * relu par bouton_cb() pour Speed ET Flow, `selecteur_pas_z` pour Z
     * uniquement. */
    selecteur_choix_t selecteur_pct;
    selecteur_choix_t selecteur_pas_z;
    lv_obj_t          *label_pct;    /* légende au-dessus du sélecteur % */
    lv_obj_t          *label_pas_z;  /* légende au-dessus du sélecteur Z */

    /* Dernières valeurs vues par mettre_a_jour(), relues par bouton_cb() AU
     * MOMENT DU CLIC pour Speed/Flow (jamais depuis l'état backend
     * directement -- voir le commentaire de tête, "Différence structurelle").
     * Déjà bornées [0, 300] (voir mettre_a_jour() dans le .c) -- une garde
     * défensive contre un état corrompu, même politique que
     * consigne_u16() dans ecran_temperatures.c. */
    uint16_t vitesse_pct_connue;
    uint16_t flux_pct_connue;
} ecran_reglage_fin_ctx_t;

extern const ecran_desc_t ECRAN_REGLAGE_FIN;
