/* rail.h — le rail persistant d'accès rapide : une colonne verticale de
 * quatre boutons (Accueil, Home, Macros, STOP) présente en permanence sur le
 * côté de l'écran (jalon refonte accueil/déplacer). Généralisation d'un
 * même besoin que selecteur_pas.h/selecteur_choix.h -- N boutons dans une
 * colonne plutôt qu'une rangée -- mais avec une différence de fond : ce
 * widget ne fait NI navigation NI gcode, et un clic ne bascule PAS lui-même
 * un état "actif" (contrairement à selecteur_pas_t/selecteur_choix_t, où le
 * bouton cliqué DEVIENT l'actif). Le rail se contente de dispatcher
 * `sur_action(action, ctx)` -- c'est l'intégration (tâche future de ce plan)
 * qui décide quoi en faire (naviguer, ouvrir une confirmation d'arrêt
 * d'urgence...) et qui appelle en retour `rail_marquer_actif()` pour
 * indiquer quel écran est actuellement affiché. Comme selecteur_pas.h/
 * selecteur_choix.h, ce fichier ne connaît rien de klipper_gcode.h ni de
 * ui_commander() : un futur fork non-Klipper le réutilise tel quel pour
 * n'importe quel rail de raccourcis à quatre entrées.
 *
 * Structure à champs publics vivant dans le contexte de l'écran/de
 * l'habillage, JAMAIS allouée par ce widget — même forme que
 * selecteur_pas_t/selecteur_choix_t/tuile_t/progression_t : ce fichier ne
 * fait jamais qu'y ranger des lv_obj_t* déjà créés, et l'appelant garde le
 * droit de repositionner/redimensionner `racine` après coup (même politique
 * que progression_t). */
#pragma once
#include "lvgl.h"

typedef enum {
    RAIL_ACCUEIL,
    RAIL_HOME,
    RAIL_MACROS,
    RAIL_STOP,
    RAIL_NB
} rail_action_t;

typedef struct {
    lv_obj_t     *racine;
    lv_obj_t     *boutons[RAIL_NB];
    void        (*sur_action)(rail_action_t, void *);
    void         *ctx;
} rail_t;

/* Crée les cinq objets LVGL (racine + RAIL_NB boutons, colonne ~58 px de
 * large) dans `parent`, et les range dans `r` (déjà alloué par l'appelant,
 * voir le commentaire de tête). `boutons[i]` est toujours le bouton de
 * l'action `i` (donc `boutons[RAIL_STOP]` est le bouton STOP, quel que soit
 * l'ordre de création interne) -- STOP est rouge et affiché en bas de la
 * colonne, visuellement séparé des trois autres. Un clic sur `boutons[i]`
 * appelle `sur_action(i, ctx)` si `sur_action` n'est pas NULL (NULL est
 * autorisé à la création -- un clic ne fait alors simplement rien, même
 * garde que `cible`/`s` dans bouton_pas_cb() de selecteur_pas.c) ; ce widget
 * ne fait jamais lui-même de navigation ni d'envoi gcode.
 *
 * `r` ou `parent` NULL : ne fait rien plutôt que déréférencer -- `r->racine`
 * reste NULL (si `r` est non-NULL) et aucun objet LVGL n'est créé (même
 * contrat que selecteur_choix_creer()). */
void rail_creer(rail_t *r, lv_obj_t *parent, void (*sur_action)(rail_action_t, void *), void *ctx);

/* Surligne le bouton de l'action `action` (bordure, indépendante de la
 * couleur de fond propre à chaque bouton -- donc sans jamais "banaliser" le
 * rouge de STOP) et retire le surlignage de tous les autres. `action ==
 * RAIL_NB` : aucun bouton surligné (aucun écran du rail n'est l'écran
 * courant). `r` NULL : ne fait rien plutôt que déréférencer. */
void rail_marquer_actif(rail_t *r, rail_action_t action);
