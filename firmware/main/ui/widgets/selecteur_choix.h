/* selecteur_choix.h — widget générique « N boutons mutuellement exclusifs » :
 * généralisation de selecteur_pas.h (voir son en-tête complet pour le
 * raisonnement détaillé, repris ici sans le redupliquer) à un nombre
 * variable de libellés (2..8) au lieu des quatre pas fixes {0.1, 1, 10,
 * 100}. Réutilisé par l'écran Déplacer (jalon refonte accueil/déplacer) pour
 * le pas de jog ET la vitesse (Lent/Moyen/Rapide) — deux instances du même
 * widget, chacune avec ses propres libellés, plutôt que deux widgets
 * jumeaux. Comme selecteur_pas.h, ce fichier ne connaît rien de
 * klipper_gcode.h ni de ui_commander() : un futur fork non-Klipper le
 * réutilise tel quel pour n'importe quel réglage à choix discret.
 *
 * Structure à champs publics vivant dans le contexte de l'écran, JAMAIS
 * allouée par ce widget — même forme que selecteur_pas_t/tuile_t/
 * progression_t : ce fichier ne fait jamais qu'y ranger des lv_obj_t* déjà
 * créés, et l'écran appelant garde le droit de repositionner/redimensionner
 * `racine` après coup (même politique que progression_t). */
#pragma once
#include "lvgl.h"

typedef struct {
    lv_obj_t *racine;
    lv_obj_t *boutons[8];
    uint8_t   nb;           /* nombre de boutons réellement créés, 2..8 */
    uint8_t   index_actif;  /* 0..nb-1 */
} selecteur_choix_t;

/* Crée `racine` (FLEX_ROW, thème sombre) et `nb` boutons mutuellement
 * exclusifs aux libellés `libelles[0..nb-1]`, et les range dans `s` (déjà
 * alloué par l'appelant, voir le commentaire de tête). `index_actif` démarre
 * à `defaut`, borné à `nb - 1` (une valeur hors bornes ne fait donc jamais
 * pointer sur un bouton inexistant, elle sélectionne simplement le dernier).
 *
 * `s` ou `parent` ou `libelles` NULL, ou `nb` hors [2, 8] : ne fait rien
 * plutôt que déréférencer — `s->racine` reste NULL (si `s` est non-NULL) et
 * aucun objet LVGL n'est créé. */
void selecteur_choix_creer(selecteur_choix_t *s, lv_obj_t *parent,
                            const char *const *libelles, uint8_t nb, uint8_t defaut);

/* s->index_actif ; 0 si `s` est NULL plutôt que de déréférencer. */
uint8_t selecteur_choix_index(const selecteur_choix_t *s);
