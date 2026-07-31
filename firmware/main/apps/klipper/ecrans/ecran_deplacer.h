/* Écran Déplacer (tâche 4, refonte accueil/déplacer) : la croix de jog
 * XY/Z, EN GRAND, sur son propre écran dédié -- contrairement au pad compact
 * de ecran_accueil_idle.c (54x24 px, partagé avec le reste de l'accueil),
 * ici le jog EST tout l'écran : six boutons de 110x90 px minimum (bien
 * au-dessus des 44 px de cible tactile minimale imposés par la tâche),
 * disposés en croix (Y+ en haut, X-/(centre)/X+ au milieu, Y- en bas) +
 * colonne Z séparée à côté, un sélecteur de Pas (0.1/1/10/100 mm) et un
 * sélecteur de Vitesse (Lent/Moyen/Rapide) via le widget générique
 * selecteur_choix.h (tâche 1), et une rangée Home (All/X/Y/Z).
 *
 * Réutilise le layout/la logique de ecran_accueil_idle.c (voir son
 * commentaire de tête et JOG_DEFS/jog_bouton_cb/HOME_DEFS/home_bouton_cb) :
 * même idiome axe+signe pour le pad, même wrapper JSON pour le gcode via
 * ui_commander(BACKEND_ACTION_GCODE, ...). Les fonctions ne sont pas
 * partagées (statiques à chaque fichier .c, même choix que le reste de ce
 * dépôt -- voir pomper_transitions_style()/dernier_msgbox() redéfinis dans
 * chaque fichier de host-test/tests/ pour le même principe) : ce fichier a
 * sa propre copie, adaptée à sa mise en page et à ses vitesses.
 *
 * ÉCART délibéré par rapport à ecran_accueil_idle.c : AUCUNE désactivation
 * (LV_STATE_DISABLED) des boutons de jog/homing ici, ni sur `donnees_perimees`
 * ni sur "axe non référencé" -- le brief de cette tâche (task-4-brief.md,
 * step 3) ne demande explicitement à `mettre_a_jour()` que de rafraîchir la
 * ligne de position + l'outil actif (grisés si `donnees_perimees`), rien de
 * plus. Ajouter le grisage par axe de ecran_accueil_idle.c ici aurait été de
 * la sur-ingénierie non demandée par ce brief précis, et rien ne prouverait
 * qu'elle marche (le scénario de test de la tâche ne l'exerce jamais).
 *
 * ÉCART délibéré n°2 : AUCUNE confirmation avant un "Home" ici (contrairement
 * à ecran_accueil_idle.c, qui ouvre un dialogue si l'axe est déjà référencé) :
 * cet écran EST l'écran de contrôle manuel dédié -- un opérateur qui vient ici
 * taper "Home" agit déjà en connaissance de cause, contrairement à l'accueil
 * (où le pad de jog n'est qu'un raccourci secondaire au milieu d'un tableau de
 * bord de température). Le brief (step 1/step 3) ne décrit d'ailleurs qu'un
 * clic direct -- "cliquer Home Y -> contient G28 Y" -- jamais un dialogue à
 * confirmer en plus. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "selecteur_choix.h"

/* Six boutons de jog, ORDRE FIXE (brief : "ordre X-/X+/Y-/Y+/Z+/Z-") --
 * réutilisé par les rappels de clic (jog_infos[i]) et par host-test/tests/
 * test_ecran_deplacer.c, même convention que ECRAN_ACCUEIL_IDLE_JOG_*
 * (ecran_accueil_idle.h). */
#define ECRAN_DEPLACER_JOG_X_NEG 0
#define ECRAN_DEPLACER_JOG_X_POS 1
#define ECRAN_DEPLACER_JOG_Y_NEG 2
#define ECRAN_DEPLACER_JOG_Y_POS 3
#define ECRAN_DEPLACER_JOG_Z_POS 4
#define ECRAN_DEPLACER_JOG_Z_NEG 5
#define ECRAN_DEPLACER_JOG_NB    6

/* Quatre boutons de homing, même convention que ECRAN_ACCUEIL_IDLE_HOME_*. */
#define ECRAN_DEPLACER_HOME_ALL 0
#define ECRAN_DEPLACER_HOME_X   1
#define ECRAN_DEPLACER_HOME_Y   2
#define ECRAN_DEPLACER_HOME_Z   3
#define ECRAN_DEPLACER_HOME_NB  4

/* Index par défaut des deux sélecteurs (brief : "Pas mm par indice
 * {0.1, 1, 10, 100}, défaut index 1 (=1 mm). Vitesse défaut index 1
 * (=Moyen)"). Nommés pour que construire()/un futur test ne puissent jamais
 * diverger sur la valeur par défaut. */
#define ECRAN_DEPLACER_PAS_DEFAUT     1
#define ECRAN_DEPLACER_VITESSE_DEFAUT 1

/* user_data d'un rappel de clic de bouton de jog -- même forme que
 * ecran_accueil_idle_jog_info_t : le contexte de l'écran (pour relire le pas
 * ET la vitesse courants au moment du clic, jamais mis en cache ailleurs) et
 * ce que ce bouton précis représente (axe + sens). */
typedef struct {
    struct ecran_deplacer_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    char                          axe;   /* 'X', 'Y' ou 'Z' */
    float                         signe; /* +1.0f ou -1.0f */
} ecran_deplacer_jog_info_t;

/* user_data d'un rappel de clic de bouton de homing : le contexte de l'écran
 * (non utilisé pour l'instant -- pas de confirmation, voir le commentaire de
 * tête -- mais présent par symétrie et pour un futur besoin) et `masque`, la
 * même convention de bits que klipper_gcode_home()/etat_klipper_t (bit0=X
 * bit1=Y bit2=Z, 0x7 pour "All"). */
typedef struct {
    struct ecran_deplacer_ctx_s *ctx;
    uint8_t                       masque;
} ecran_deplacer_home_info_t;

typedef struct ecran_deplacer_ctx_s {
    lv_obj_t *position;        /* "X:.. Y:.. Z:.." (1 décimale, "--" si l'axe n'est pas référencé) */
    lv_obj_t *outil_actif_nom; /* "Active: T.." / "Active: --" (aucun extrudeur) */

    /* Pad de jog : voir ECRAN_DEPLACER_JOG_* plus haut pour l'indexation.
     * `jog_infos[i].ctx` pointe toujours vers ce contexte -- posé une fois
     * par construire(), jamais recalculé. */
    lv_obj_t                   *jog_boutons[ECRAN_DEPLACER_JOG_NB];
    ecran_deplacer_jog_info_t   jog_infos[ECRAN_DEPLACER_JOG_NB];

    /* Sélecteurs génériques (tâche 1) : Pas {0.1, 1, 10, 100} mm, Vitesse
     * {Lent, Moyen, Rapide} -- relus AU MOMENT DU CLIC par jog_bouton_cb(),
     * jamais mis en cache ailleurs (même discipline que selecteur_pas dans
     * ecran_accueil_idle.c). */
    selecteur_choix_t selecteur_pas;
    selecteur_choix_t selecteur_vitesse;
    lv_obj_t         *label_pas;     /* légende au-dessus du sélecteur de pas */
    lv_obj_t         *label_vitesse; /* légende au-dessus du sélecteur de vitesse */

    /* Rangée de homing : voir ECRAN_DEPLACER_HOME_* plus haut pour
     * l'indexation. */
    lv_obj_t                    *home_boutons[ECRAN_DEPLACER_HOME_NB];
    ecran_deplacer_home_info_t   home_infos[ECRAN_DEPLACER_HOME_NB];
} ecran_deplacer_ctx_t;

extern const ecran_desc_t ECRAN_DEPLACER;
