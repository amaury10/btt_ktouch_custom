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
 * Tache 5 (sous-projet "refonte IHM KlipperScreen") : bouton "Fine Tune",
 * a la place ou KlipperScreen le met reellement -- l'ecran de statut
 * d'impression (Job Status), pas le sous-menu Configuration (voir
 * ecran_menu_reglages.c, qui ne le referme plus). Pure navigation
 * (`navigation_empiler(&ECRAN_REGLAGE_FIN)`), meme idiome que le bouton
 * Macros de la tache 6 -- pas de passage par ui_commander().
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
#include <stdint.h>

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
    /* Tache 6 (jalon 3a) : navigue vers ECRAN_MACROS -- visible SEULEMENT si
     * `nb_macros > 0` (mis a jour a chaque mettre_a_jour(), voir
     * ecran_accueil.c) : « jamais un bouton mort » (meme principe que le
     * bouton Imprimer de la spec, differe tant que 3d n'est pas livre). */
    lv_obj_t      *bouton_macros;
    /* Tache 5 (sous-projet "refonte IHM KlipperScreen") : navigue vers
     * ECRAN_REGLAGE_FIN -- toujours visible (contrairement a bouton_macros
     * ci-dessus) : cet ecran n'apparait que pendant une impression active
     * (voir le commentaire de tete de ce fichier), Fine Tune y est donc
     * TOUJOURS pertinent, meme raisonnement que bouton_pause/_annuler/
     * _urgence qui ne se masquent jamais non plus. */
    lv_obj_t      *bouton_reglage_fin;
    /* Deux valeurs mémorisées par mettre_a_jour(), relues par les rappels de
     * clic (voir ecran_accueil.c) : quelle action Pause doit envoyer, et si
     * les trois boutons doivent ignorer un clic tant que les données sont
     * périmées (LV_STATE_DISABLED bloque déjà le tactile réel, voir
     * lv_indev.c -- ce booléen est une garde défensive supplémentaire,
     * exercée directement par host-test/tests/test_commandes.c via
     * lv_obj_send_event(), qui ne passe jamais par l'entrée tactile). */
    bool            en_pause;
    bool            donnees_perimees;

    /* Feature "Miniatures gcode", tâche B (intégration ESP) : thumbnail du
     * fichier en cours, lu depuis le store dédié miniature.h (HORS
     * etat_klipper_t, voir son commentaire de tête). `miniature_dsc` est le
     * descripteur LVGL passé à lv_image_set_src() -- DOIT vivre aussi
     * longtemps que le widget peut y faire référence (jamais une variable
     * locale de mettre_a_jour(), qui sortirait de portée dès son retour) :
     * stocké ici, dans le contexte de l'écran, comme `progression`/`buse`
     * ci-dessus. `miniature_generation` mémorise la dernière génération du
     * store affichée (0 = jamais) pour ne reconstruire `miniature_dsc`/
     * rappeler lv_image_set_src() que sur un changement réel -- voir
     * ecran_accueil.c, mettre_a_jour(). */
    lv_obj_t       *miniature_image;
    lv_image_dsc_t  miniature_dsc;
    uint32_t        miniature_generation;
    /* Pointeur RÉELLEMENT posé sur `miniature_image` et visible (NULL si
     * l'image est masquée) -- passé à miniature_purger() en fin de cycle pour
     * que le store refuse de libérer un tampon que le widget peut encore
     * décoder au dessin (lv_image_set_src() est paresseux). Voir
     * ecran_accueil.c, mettre_a_jour_miniature(), et miniature.h. */
    const uint8_t  *miniature_affichee;
} ecran_accueil_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL;
