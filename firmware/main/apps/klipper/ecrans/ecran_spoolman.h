/* Écran Spoolman (spec 2026-08-15-spoolman-design.md) -- remplace le dernier
 * stub du menu Configuration : liste des bobines connues du serveur Spoolman
 * (via Moonraker, store spoolman_store.h) et désignation de la bobine
 * CHARGÉE, pour que Moonraker décompte automatiquement le filament consommé.
 *
 * Ossature reprise de ecran_fichiers.c (voir son .h pour le raisonnement
 * complet : colonne unique de boutons pleine largeur, page de 5, pagination
 * Prev/Next, contexte par emplacement pour le rappel de clic, grisage
 * systématique quand les données sont périmées). Trois différences :
 *
 *   1. Chaque rangée porte une PASTILLE de couleur (le filament) à gauche du
 *      libellé -- couleur inconnue = gris, jamais une couleur inventée.
 *   2. La bobine active est marquée (LV_SYMBOL_OK + fond distinct) : c'est
 *      l'information que l'utilisateur vient chercher en premier.
 *   3. Deux boutons de pied de page en plus de la pagination : Refresh
 *      (redemande la liste -- une bobine ajoutée depuis Mainsail apparaît
 *      sans redémarrer l'écran) et Clear (plus aucune bobine active, sous
 *      confirmation).
 *
 * Ce panneau ne CRÉE ni ne modifie aucune bobine : l'inventaire (poids,
 * emplacements, archivage) se saisit au clavier sur l'interface web
 * Spoolman, pas au doigt sur une dalle 5". Voir la spec, section « Ce que le
 * panneau fait (et ne fait pas) ».
 *
 * Contexte exposé plutôt qu'opaque, même raison que les autres écrans :
 * host-test relit les libellés via lv_label_get_text() pour prouver ce
 * qu'affiche mettre_a_jour(), sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "spoolman_store.h"

/* Même taille de page que ecran_fichiers/ecran_macros (5 rangées de 52 px,
 * ce qui tient entre l'en-tête et la limite imposée par le bandeau de
 * notification). 12 bobines / 5 = 3 pages au pire. */
#define ECRAN_SPOOLMAN_PAGE_TAILLE 5

struct ecran_spoolman_ctx_s;

typedef struct {
    struct ecran_spoolman_ctx_s *ctx;         /* jamais NULL une fois construire() passé */
    uint8_t                      emplacement; /* 0..ECRAN_SPOOLMAN_PAGE_TAILLE-1, position FIXE */
} ecran_spoolman_emplacement_t;

typedef struct ecran_spoolman_ctx_s {
    lv_obj_t *entete;   /* "Active: ..." / "No active spool" / "Spoolman offline" */
    lv_obj_t *vide;     /* "No spools" -- visible seulement si la liste est vide */

    lv_obj_t *boutons[ECRAN_SPOOLMAN_PAGE_TAILLE];
    lv_obj_t *labels[ECRAN_SPOOLMAN_PAGE_TAILLE];    /* libellé principal, enfant de boutons[i] */
    lv_obj_t *pastilles[ECRAN_SPOOLMAN_PAGE_TAILLE]; /* carré de couleur, enfant de boutons[i] */
    ecran_spoolman_emplacement_t emplacements[ECRAN_SPOOLMAN_PAGE_TAILLE];

    lv_obj_t *bouton_precedent;
    lv_obj_t *bouton_suivant;
    lv_obj_t *page_label;
    lv_obj_t *bouton_rafraichir;
    lv_obj_t *bouton_effacer;

    /* Copie mémorisée de la liste, relue par les rappels de clic et de
     * pagination -- même raison que ctx->fichiers_copie dans
     * ecran_fichiers_ctx_t : le store peut changer entre l'affichage et le
     * tap, l'identifiant envoyé doit être celui de la rangée AFFICHÉE. */
    spoolman_liste_t liste;
    spoolman_etat_t  etat;
    bool             donnees_perimees;
    uint8_t          page; /* 0-indexé */

    /* Bobine tapée, en attente de résolution de la confirmation ouverte : le
     * dialogue reste modal le temps qu'un humain lise, donc l'identifiant
     * doit survivre au retour du rappel de clic (même mécanisme que
     * ctx->nom_attente dans ecran_fichiers_ctx_t). */
    int32_t id_attente;
    char    nom_attente[SPOOLMAN_TEXTE_MAX];

    uint32_t derniere_generation;
} ecran_spoolman_ctx_t;

extern const ecran_desc_t ECRAN_SPOOLMAN;
