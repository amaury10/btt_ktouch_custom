/* Écran d'accueil Klipper (tâche 6) : la première fois que la chaîne
 * complète tourne -- backend factice -> boucle_cycle() -> magasin d'état ->
 * génération -> CET écran. Deux tuiles de température (buse, plateau), le
 * nom du fichier en cours, une barre de progression avec le temps restant,
 * et trois boutons de commande CRÉÉS MAIS INERTES (Pause / Cancel / E-STOP).
 * Les câbler est le travail de la tâche 9, une fois la file de commandes en
 * place (voir ui/source_etat.h, ui_commander()) -- les câbler ici ferait
 * passer un appel réseau par un rappel de bouton avant que cette file
 * n'existe, exactement le genre de raccourci que la spécification interdit
 * (jamais de blocage/réseau dans un rappel LVGL).
 *
 * `ecran_accueil_ctx_t` est exposé ici plutôt qu'opaque (contrairement à
 * l'esprit "l'écran ne connaît que void *contexte" de ecran.h) pour deux
 * raisons : il n'embarque que des lv_obj_t* et des sous-widgets déjà
 * publics (tuile_t, progression_t -- voir leurs propres en-têtes, qui sont
 * publics pour la même raison), et host-test/tests/test_ecran_accueil.c a
 * besoin de relire les libellés via lv_label_get_text() pour prouver que
 * mettre_a_jour() écrit ce qu'on attend sans jamais regarder un pixel. */
#pragma once

#include "ecran.h"
#include "lvgl.h"
#include "progression.h"
#include "tuile.h"

typedef struct {
    tuile_t        buse;             /* tuile "Nozzle" */
    tuile_t        plateau;          /* tuile "Bed" */
    lv_obj_t      *fichier;          /* nom de fichier, LV_LABEL_LONG_DOT */
    progression_t  progression;      /* barre + pourcentage centré */
    lv_obj_t      *temps;            /* temps restant, a droite du pourcentage */
    lv_obj_t      *bouton_pause;     /* inerte jusqu'a la tache 9 */
    lv_obj_t      *bouton_annuler;   /* inerte jusqu'a la tache 9 */
    lv_obj_t      *bouton_urgence;   /* inerte jusqu'a la tache 9 */
} ecran_accueil_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL;
