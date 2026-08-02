/* Écran Extruder (sous-projet 3, découpage KlipperScreen -- panneau
 * Extruder) : la case "Extrude" du sous-menu Actions (ecran_actions.c) ouvre
 * CET écran dédié pour extruder/rétracter du filament -- même découpage "un
 * panneau KlipperScreen = un écran dédié" que ecran_deplacer.c/
 * ecran_temperatures.c (sous-projet précédent).
 *
 * Décision de périmètre V1 (voir task-2-brief.md, "Décision de périmètre
 * V1") : PAS de sélecteur d'outil ici -- `selecteur_choix` fige son nombre
 * de boutons à la création, alors que `nb_extrudeurs` n'est connu qu'à
 * mettre_a_jour() (recréer le widget à chaud serait fragile), et le
 * multi-tête est spéculatif pour les machines cibles (1-2 têtes) et
 * intestable sur vkp (1 tête). Cet écran opère donc TOUJOURS sur la buse
 * ACTIVE rapportée par l'état (`etat_klipper_t::outil_actif`) --
 * `klipper_gcode_activer_outil()` (tâche 1, jalon 3b) reste disponible pour
 * un futur suivi qui ajouterait la sélection. L'extrusion elle-même
 * (`klipper_gcode_extrude()`, `G1 E`) ne prend d'ailleurs aucun nom de
 * chauffeur : elle agit sur l'extrudeur ACTIF de Klipper, quel qu'il soit.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : une ligne
 * d'état "Actif : T<n>" en haut, suivie de la tuile de température de la
 * buse active (widget partagé tuile.h, LECTURE SEULE -- le RÉGLAGE de la
 * consigne vit dans ecran_temperatures.c, jamais ici), puis deux sélecteurs
 * génériques (Longueur, Vitesse -- selecteur_choix.h, tâche 1) côte à côte,
 * et enfin deux gros boutons (Extruder / Rétracter) qui envoient
 * G1 E<+-longueur> F<vitesse> via klipper_gcode_extrude().
 *
 * `ecran_extruder_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_deplacer_ctx_t/ecran_temperatures_ctx_t (voir leurs en-têtes) :
 * host-test/tests/test_ecran_extruder.c relit les widgets/libellés/couleurs
 * directement, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "selecteur_choix.h"
#include "tuile.h"

/* Index par défaut des deux sélecteurs (brief : "Longueur {5, 10, 25, 50}
 * mm, défaut index 1 (=10mm). Vitesse {Lent, Moyen, Rapide}, défaut index 1
 * (=Moyen)"). Nommés pour que construire()/un futur test ne puissent jamais
 * diverger sur la valeur par défaut -- même convention que
 * ECRAN_DEPLACER_PAS_DEFAUT/ECRAN_DEPLACER_VITESSE_DEFAUT. */
#define ECRAN_EXTRUDER_LONGUEUR_DEFAUT 1
#define ECRAN_EXTRUDER_VITESSE_DEFAUT  1

/* user_data d'un rappel de clic de bouton Extruder/Rétracter -- même forme
 * que ecran_deplacer_jog_info_t : le contexte de l'écran (pour relire la
 * longueur ET la vitesse courantes au moment du clic, jamais mises en cache
 * ailleurs) et le signe que ce bouton précis applique. */
typedef struct {
    struct ecran_extruder_ctx_s *ctx;   /* jamais NULL une fois construire() passé */
    float                         signe; /* +1.0f (Extruder) ou -1.0f (Retracter) */
} ecran_extruder_bouton_info_t;

typedef struct ecran_extruder_ctx_s {
    lv_obj_t *actif_label; /* "Actif : T<n>" / "Actif : --" (aucune buse valide) */
    tuile_t   tuile;       /* temperature actuelle/consigne de la buse active, lecture seule */

    /* Sélecteurs génériques (tâche 1, jalon 3b) : Longueur {5, 10, 25, 50}
     * mm, Vitesse {Lent, Moyen, Rapide} -- relus AU MOMENT DU CLIC par
     * extrude_bouton_cb(), jamais mis en cache ailleurs (même discipline que
     * selecteur_pas/selecteur_vitesse dans ecran_deplacer.c). */
    selecteur_choix_t selecteur_longueur;
    selecteur_choix_t selecteur_vitesse;
    lv_obj_t         *label_longueur; /* légende au-dessus du sélecteur de longueur */
    lv_obj_t         *label_vitesse;  /* légende au-dessus du sélecteur de vitesse */

    lv_obj_t                     *bouton_extruder;
    lv_obj_t                     *bouton_retracter;
    ecran_extruder_bouton_info_t  info_extruder;
    ecran_extruder_bouton_info_t  info_retracter;
} ecran_extruder_ctx_t;

extern const ecran_desc_t ECRAN_EXTRUDER;
