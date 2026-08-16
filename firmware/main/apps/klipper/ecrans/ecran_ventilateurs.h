/* Écran Ventilateurs (sous-projet 4, découpage KlipperScreen -- panneau
 * Fan) : la case "Fan" du sous-menu Actions (ecran_actions.c)
 * ouvre CET écran dédié pour régler la vitesse du ventilateur de pièce --
 * même découpage "un panneau KlipperScreen = un écran dédié" que
 * ecran_deplacer.c/ecran_temperatures.c/ecran_extruder.c (sous-projets
 * précédents).
 *
 * Le backend ne suit QUE le ventilateur de pièce (`fan` Klipper ->
 * `ventilateurs[0]`, voir moonraker_rpc.c) -- les `heater_fan`/
 * `controller_fan` sont auto-gérés par Klipper, jamais pilotables, ni ici ni
 * dans KlipperScreen. Cet écran ne connaît donc qu'UN seul ventilateur.
 *
 * TROIS moyens de régler la MÊME valeur (0-100 %), tous vers le même chemin
 * gcode (M106, via klipper_gcode_ventilateur()) :
 *   1. Le slider (`lv_slider` natif LVGL, AUCUN wrapper -- usage unique dans
 *      ce dépôt, une abstraction supplémentaire n'aurait aucun second
 *      appelant). Le gcode ne part QU'AU RELÂCHER (LV_EVENT_RELEASED) --
 *      envoyer à chaque pas de LV_EVENT_VALUE_CHANGED inonderait Klipper de
 *      commandes M106 pendant un simple glissement de doigt. VALUE_CHANGED
 *      ne fait donc QUE rafraîchir le label "NN %" (retour visuel pendant
 *      le drag), jamais de gcode.
 *   2. Les préréglages (Off/25/50/75/100, `selecteur_choix.h`) : un clic
 *      envoie directement le gcode à ce pourcentage.
 *   3. La saisie numérique : le même bouton "NN %" qui sert de retour
 *      visuel au slider est aussi TAPPABLE -- il ouvre un clavier
 *      numérique (`clavier.h`) pré-rempli à la valeur courante, bornée
 *      [0, 100] à la validation (hors borne/non numérique -> notification,
 *      aucun gcode -- même politique que cellule_clavier_rappel() dans
 *      ecran_temperatures.c, bornes différentes).
 *
 * Piège d'interaction (voir mettre_a_jour() dans le .c) : si mettre_a_jour()
 * recalait la position du slider pendant que l'utilisateur est en train de
 * le glisser, la position se battrait avec le doigt. `glissement_en_cours`
 * (posé sur LV_EVENT_PRESSED, relâché sur LV_EVENT_RELEASED) garde
 * mettre_a_jour() de toucher la position du slider tant qu'une interaction
 * est en cours -- le label, lui, reste toujours à jour (il n'y a aucun
 * conflit à afficher la dernière valeur connue du backend en texte pendant
 * que le doigt bouge le slider ailleurs à l'écran).
 *
 * `ecran_ventilateurs_ctx_t` est exposé ici plutôt qu'opaque, même raison
 * que ecran_deplacer_ctx_t/ecran_temperatures_ctx_t/ecran_extruder_ctx_t
 * (voir leurs en-têtes) : host-test/tests/test_ecran_ventilateurs.c relit
 * les widgets/libellés/couleurs directement, sans jamais regarder un
 * pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "selecteur_choix.h"

/* Préréglages, ORDRE FIXE (brief : "Off / 25 / 50 / 75 / 100") -- réutilisé
 * par la boucle de construction (PRESET_DEFS dans le .c) et par
 * host-test/tests/test_ecran_ventilateurs.c pour retrouver le bon bouton. */
#define ECRAN_VENTILATEURS_PRESET_OFF 0
#define ECRAN_VENTILATEURS_PRESET_25  1
#define ECRAN_VENTILATEURS_PRESET_50  2
#define ECRAN_VENTILATEURS_PRESET_75  3
#define ECRAN_VENTILATEURS_PRESET_100 4
#define ECRAN_VENTILATEURS_PRESET_NB  5

/* Bornes de saisie du clavier numérique -- nommées pour que
 * clavier_rappel() (le rappel, dans le .c) et un futur test ne puissent
 * jamais diverger sur la borne haute, même convention que
 * ECRAN_TEMPERATURES_TEMP_MIN/MAX (ecran_temperatures.h). */
#define ECRAN_VENTILATEURS_PCT_MIN 0
#define ECRAN_VENTILATEURS_PCT_MAX 100

/* user_data d'un rappel de clic de préréglage : juste le pourcentage que ce
 * bouton précis envoie -- contrairement à
 * ecran_temperatures_preset_info_t (deux cibles + ctx pour relire l'outil
 * actif au moment du clic), un préréglage de ventilateur n'a besoin de
 * relire aucun état applicatif : le pourcentage est fixe, connu dès la
 * construction. */
typedef struct {
    uint8_t pct;
} ecran_ventilateurs_preset_info_t;

typedef struct ecran_ventilateurs_ctx_s {
    lv_obj_t *slider; /* lv_slider natif, plage [0, 100], AUCUN wrapper (voir le commentaire de tête) */

    /* Bouton "NN %" -- retour visuel du slider (mis à jour à CHAQUE
     * LV_EVENT_VALUE_CHANGED et à chaque mettre_a_jour()) ET cible tactile
     * de la saisie numérique (LV_EVENT_CLICKED -> clavier_ouvrir()). Un seul
     * widget pour les deux rôles, voir le commentaire de tête. */
    lv_obj_t *bouton_valeur;
    lv_obj_t *label_valeur;

    /* Vrai entre LV_EVENT_PRESSED et LV_EVENT_RELEASED sur `slider` -- garde
     * mettre_a_jour() de recaler la position du slider pendant que
     * l'utilisateur le glisse (voir le commentaire de tête). */
    bool glissement_en_cours;

    /* Préréglages (voir ECRAN_VENTILATEURS_PRESET_* plus haut pour
     * l'indexation) : `preregalges` est le widget générique
     * (selecteur_choix.h) qui les porte -- son `index_actif` interne n'a
     * aucun sens persistant ici (la valeur vient de Klipper, jamais d'un
     * choix mémorisé par ce widget, voir le commentaire de tête), seul
     * l'événement de clic de chaque bouton compte. */
    selecteur_choix_t                preregalges;
    ecran_ventilateurs_preset_info_t preset_infos[ECRAN_VENTILATEURS_PRESET_NB];
} ecran_ventilateurs_ctx_t;

extern const ecran_desc_t ECRAN_VENTILATEURS;
