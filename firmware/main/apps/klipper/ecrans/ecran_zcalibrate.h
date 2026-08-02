/* Écran Z Calibrate (sous-projet "panneaux KlipperScreen", tâche 3) :
 * l'assistant de calibration Z que KlipperScreen expose pour PROBE_CALIBRATE
 * et Z_ENDSTOP_CALIBRATE -- deux macros Klipper qui ouvrent chacune une
 * session interactive côté firmware (la buse s'approche du lit, l'opérateur
 * affine sa hauteur par petits pas via TESTZ, puis referme la session par
 * ACCEPT ou ABORT). Ce panneau ne fait que poster ces cinq commandes
 * (PROBE_CALIBRATE / Z_ENDSTOP_CALIBRATE / TESTZ / ACCEPT / ABORT, toutes
 * déjà construites par klipper_gcode_calibration_z()/klipper_gcode_testz(),
 * tâche 1) -- il ne modélise jamais lui-même l'état de la session (ouverte ou
 * non, quelle étape), exactement comme klipper_gcode.h ne l'expose pas non
 * plus dans etat_klipper_t (voir son commentaire de tête, "REGARDE JAMAIS le
 * contenu d'un gcode_response").
 *
 * Layout (voir zcalibrate.png du brief) : une ligne d'info en haut (valeur Z
 * relue à gauche, "Probe Offset: -" à droite), puis DEUX boutons pleine
 * largeur "Start Probe"/"Start Endstop" (ouvrent la session), un sélecteur de
 * pas générique (`selecteur_choix.h`, tâche 1 -- même widget que
 * ecran_deplacer.c/ecran_reglage_fin.c) {.01, .05, .1, .5, 1, 5} mm, deux
 * boutons "Raise Nozzle"/"Lower Nozzle" (TESTZ ±pas), et enfin deux boutons
 * "Accept"/"Abort" qui referment la session.
 *
 * IMPORTANT -- "Probe Offset: -" est un PLACEHOLDER, jamais une valeur
 * mesurée : Klipper répond à PROBE_CALIBRATE/ACCEPT par un message console
 * ("probe_offset: -1.234" par ex.), mais rien dans ce dépôt ne capture
 * aujourd'hui le flux `gcode_response` de Moonraker (ni klipper_gcode.h, ni
 * etat_klipper_t, ni moonraker_ws.c) -- ce fil existe côté transport
 * (moonraker_ws.c reçoit bien les notifications JSON-RPC) mais n'est
 * ni parsé ni stocké nulle part que cet écran pourrait relire. Afficher une
 * valeur ici reviendrait à l'inventer : exactement le défaut C3 que le reste
 * de ce dépôt (voir ecran.h, commentaire de `mettre_a_jour`) existe pour
 * empêcher structurellement. "-" reste donc affiché EN PERMANENCE, jamais
 * grisé (rien à griser -- ce n'est pas une donnée pouvant devenir périmée),
 * jusqu'à ce qu'une future tâche câble la capture de `gcode_response` bout en
 * bout (backend_moonraker.c -> etat_klipper_t -> ce label).
 *
 * Grisage (C3) : SEULE la valeur Z relue (`valeur_z`) grise sur
 * `donnees_perimees` -- même politique minimale que la ligne
 * position/outil actif de ecran_deplacer.c. Les six boutons d'action ne sont
 * JAMAIS grisés : ils restent sûrs même sur un état périmé (PROBE_CALIBRATE/
 * Z_ENDSTOP_CALIBRATE/TESTZ/ACCEPT/ABORT ne dépendent d'aucune valeur lue
 * ici), exactement le même raisonnement que la grille de menu du hub
 * (ecran_accueil_hub.c) et le pad de jog de ecran_deplacer.c (voir son
 * commentaire de tête, "ÉCART délibéré").
 *
 * `ecran_zcalibrate_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_reglage_fin_ctx_t/ecran_deplacer_ctx_t (voir leurs en-têtes) :
 * host-test/tests/test_ecran_zcalibrate.c relit les libellés/couleurs
 * directement, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "selecteur_choix.h"

/* Pas du sélecteur Z (mm), ORDRE FIXE {.01, .05, .1, .5, 1, 5} (brief) --
 * converti en µm par PAS_UM[] (le .c) pour klipper_gcode_testz(). Index par
 * défaut 0 (=0.01 mm) : même raisonnement que
 * ECRAN_REGLAGE_FIN_PAS_Z_DEFAUT (ecran_reglage_fin.h) -- le pas le plus fin
 * par défaut, un TESTZ involontaire trop large est plus gênant qu'un pas
 * trop lent à ajuster. */
#define ECRAN_ZCALIBRATE_PAS_NB     6
#define ECRAN_ZCALIBRATE_PAS_DEFAUT 0

/* Six boutons, ORDRE FIXE (brief : "Start Probe, Start Endstop, Raise
 * Nozzle, Lower Nozzle, Accept, Abort") -- réutilisé par la boucle de
 * construction (BOUTON_DEFS dans le .c) et par
 * host-test/tests/test_ecran_zcalibrate.c pour retrouver le bon bouton,
 * même convention que ECRAN_REGLAGE_FIN_BOUTON_* / ECRAN_DEPLACER_JOG_*. */
#define ECRAN_ZCALIBRATE_BOUTON_START_PROBE   0
#define ECRAN_ZCALIBRATE_BOUTON_START_ENDSTOP 1
#define ECRAN_ZCALIBRATE_BOUTON_RAISE         2
#define ECRAN_ZCALIBRATE_BOUTON_LOWER         3
#define ECRAN_ZCALIBRATE_BOUTON_ACCEPT        4
#define ECRAN_ZCALIBRATE_BOUTON_ABORT         5
#define ECRAN_ZCALIBRATE_BOUTON_NB            6

/* Ce que représente précisément un bouton -- Start Probe/Start Endstop/
 * Accept/Abort postent chacun une commande FIXE (aucun paramètre à relire),
 * Raise/Lower postent TESTZ ±pas (le pas ET son signe sont relus au moment
 * du clic, jamais mis en cache ailleurs -- même discipline que
 * ecran_reglage_fin_bouton_info_t pour Z-offset). Un seul rôle par bouton :
 * pas de struct séparée par catégorie, le rôle suffit à retrouver la bonne
 * branche dans bouton_cb() (le .c). */
typedef enum {
    ECRAN_ZCALIBRATE_ROLE_START_PROBE = 0,
    ECRAN_ZCALIBRATE_ROLE_START_ENDSTOP,
    ECRAN_ZCALIBRATE_ROLE_RAISE,
    ECRAN_ZCALIBRATE_ROLE_LOWER,
    ECRAN_ZCALIBRATE_ROLE_ACCEPT,
    ECRAN_ZCALIBRATE_ROLE_ABORT,
} ecran_zcalibrate_role_t;

/* user_data d'un rappel de clic : le contexte de l'écran (pour Raise/Lower,
 * relire le pas courant au moment du clic) et le rôle de ce bouton précis. */
typedef struct {
    struct ecran_zcalibrate_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    ecran_zcalibrate_role_t        role;
} ecran_zcalibrate_bouton_info_t;

typedef struct ecran_zcalibrate_ctx_s {
    /* Ligne d'info : valeur Z relue (grisée sur donnees_perimees, voir le
     * commentaire de tête) à gauche, placeholder "Probe Offset: -" (jamais
     * modifié après construire(), voir le commentaire de tête) à droite. */
    lv_obj_t *valeur_z;
    lv_obj_t *valeur_probe_offset;

    /* Sélecteur générique (tâche 1) : pas de TESTZ, voir ECRAN_ZCALIBRATE_PAS_*
     * plus haut -- relu AU MOMENT DU CLIC par bouton_cb() pour Raise/Lower
     * uniquement (Start Probe/Start Endstop/Accept/Abort l'ignorent). */
    selecteur_choix_t selecteur_pas;
    lv_obj_t          *label_pas; /* légende au-dessus du sélecteur */

    /* Six boutons : voir ECRAN_ZCALIBRATE_BOUTON_* plus haut pour
     * l'indexation. `bouton_infos[i].ctx` pointe toujours vers ce contexte
     * -- posé une fois par construire(), jamais recalculé. */
    lv_obj_t                        *boutons[ECRAN_ZCALIBRATE_BOUTON_NB];
    ecran_zcalibrate_bouton_info_t   bouton_infos[ECRAN_ZCALIBRATE_BOUTON_NB];
} ecran_zcalibrate_ctx_t;

extern const ecran_desc_t ECRAN_ZCALIBRATE;
