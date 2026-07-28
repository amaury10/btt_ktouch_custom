/* Écran d'accueil Klipper (tâche 6) : la première fois que la chaîne
 * complète tourne -- backend factice -> boucle_cycle() -> magasin d'état ->
 * génération -> CET écran. Deux tuiles de température (buse, plateau), le
 * nom du fichier en cours, une barre de progression avec le temps restant,
 * et trois boutons de commande (Pause / Cancel / E-STOP).
 *
 * Tâche 9 : les trois boutons sont maintenant câblés via ui_commander()
 * (voir ui/source_etat.h) -- Pause/Cancel/E-STOP empilent respectivement
 * BACKEND_ACTION_PAUSE (ou REPRENDRE selon `etat_impression_en_pause`),
 * ANNULER et URGENCE, ces deux derniers seulement après confirmation
 * (confirmation.h, destructif=true). Rester réseau-libre : ui_commander()
 * ne fait qu'empiler et rendre la main, jamais d'appel bloquant depuis ce
 * rappel LVGL (voir son propre commentaire).
 *
 * `ecran_accueil_ctx_t` est exposé ici plutôt qu'opaque (contrairement à
 * l'esprit "l'écran ne connaît que void *contexte" de ecran.h) pour deux
 * raisons : il n'embarque que des lv_obj_t* et des sous-widgets déjà
 * publics (tuile_t, progression_t -- voir leurs propres en-têtes, qui sont
 * publics pour la même raison), et host-test/tests/test_ecran_accueil.c a
 * besoin de relire les libellés via lv_label_get_text() pour prouver que
 * mettre_a_jour() écrit ce qu'on attend sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>

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
    lv_obj_t      *bouton_pause;     /* libelle "Pause"/"Resume" selon en_pause */
    lv_obj_t      *label_pause;      /* enfant direct de bouton_pause, voir ci-dessus */
    lv_obj_t      *bouton_annuler;   /* passe par confirmation.h avant d'envoyer */
    lv_obj_t      *bouton_urgence;   /* passe par confirmation.h avant d'envoyer */
    /* Deux valeurs mémorisées par mettre_a_jour(), relues par les rappels de
     * clic (voir ecran_accueil.c) : quelle action Pause doit envoyer, et si
     * les trois boutons doivent ignorer un clic tant que les données sont
     * périmées (LV_STATE_DISABLED bloque déjà le tactile réel, voir
     * lv_indev.c -- ce booléen est une garde défensive supplémentaire,
     * exercée directement par host-test/tests/test_commandes.c via
     * lv_obj_send_event(), qui ne passe jamais par l'entrée tactile). */
    bool            en_pause;
    bool            donnees_perimees;
} ecran_accueil_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL;
