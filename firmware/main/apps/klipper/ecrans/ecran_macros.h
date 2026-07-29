/* Écran macros (tâche 6, jalon 3a) : la preuve de bout en bout de la
 * tranche 3a — « lister les macros de la machine et en lancer une » (spec
 * §3a point 4). C'est la doléance n°1 remontée sur le K-Touch d'origine
 * (macros injoignables) devenue la première chose visible que ce firmware
 * livre.
 *
 * Liste `etat.macros[0..nb_macros]`, une entrée par macro NON `_préfixée`
 * (convention Klipper pour une macro « cachée » — filtrée ICI, à l'écran :
 * elle reste dans l'état, `rpc_lire_macros()` (moonraker_rpc.h) le dit
 * explicitement, « c'est un choix d'affichage, pas de protocole »). Simple
 * liste paginée (16 par page, 48 macros max ÷ 16 = 3 pages) — PAS le widget
 * de chargement paresseux prévu par la spec pour 3d (YAGNI : ce widget
 * n'existe pas encore, et 48 entrées tiennent sans lui).
 *
 * `ecran_macros_ctx_t` est exposé ici plutôt qu'opaque, exactement pour la
 * même raison que `ecran_accueil_ctx_t` (voir ecran_accueil.h) :
 * host-test/tests/test_ecran_macros.c a besoin de relire les libellés via
 * lv_label_get_text() et l'état de pagination pour prouver ce que
 * mettre_a_jour() affiche, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"

/* Une page à la fois : 4 colonnes × 4 lignes, voir le calcul de mise en page
 * en tête de ecran_macros.c. 48 (KLIPPER_MACROS_MAX) ÷ 16 = 3 pages au pire
 * cas (aucune macro `_préfixée`, aucune troncature). */
#define ECRAN_MACROS_PAGE_COLONNES 4
#define ECRAN_MACROS_PAGE_LIGNES   4
#define ECRAN_MACROS_PAGE_TAILLE   (ECRAN_MACROS_PAGE_COLONNES * ECRAN_MACROS_PAGE_LIGNES)

/* Taille de tampon suffisante pour tout appel de
 * ecran_macros_construire_arguments() ci-dessous, nom le plus long
 * (KLIPPER_MACRO_NOM_MAX) compris. */
#define ECRAN_MACROS_ARGUMENTS_MAX (KLIPPER_MACRO_NOM_MAX + 16)

/* Construit `{"nom":"<nom>"}` (le contrat exact de BACKEND_ACTION_MACRO, voir
 * core/backend.h) dans `sortie` (au moins ECRAN_MACROS_ARGUMENTS_MAX octets).
 * Fonction PURE, sans LVGL ni réseau, exposée ici pour être testable
 * directement (trace du seam, host-test/tests/test_ecran_macros.c) —
 * utilisée en interne par le rappel de clic sur une macro.
 *
 * Rend false SANS TOUCHER `sortie` si `nom` ou `sortie` est NULL, si
 * `taille` vaut 0, ou si le résultat ne tient pas dans `taille` (même
 * discipline de troncature-jamais-silencieuse que rpc_construire_requete(),
 * voir moonraker_rpc.h). N'échappe AUCUN caractère : un nom de macro
 * Klipper (« gcode_macro NOM ») est déjà supposé alphanumérique + underscore
 * par tout le seam en amont (factice_extraire_nom_macro() dans
 * backend_factice.c, rpc_lire_macros() dans moonraker_rpc.c) — aucun
 * échappement n'existe nulle part ailleurs dans cette chaîne, cette fonction
 * ne l'invente pas non plus. */
bool ecran_macros_construire_arguments(const char *nom, char *sortie, size_t taille);

typedef struct {
    struct ecran_macros_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    uint8_t                    emplacement; /* 0..ECRAN_MACROS_PAGE_TAILLE-1, position FIXE dans la grille */
} ecran_macros_emplacement_t;

typedef struct ecran_macros_ctx_s {
    lv_obj_t *avertissement; /* ligne de troncature honnête, masquée sauf macros_tronquees */
    lv_obj_t *vide;          /* "No macros", visible seulement si la liste filtrée est vide */

    lv_obj_t *boutons[ECRAN_MACROS_PAGE_TAILLE];
    lv_obj_t *labels[ECRAN_MACROS_PAGE_TAILLE];  /* enfant direct de boutons[i] */
    ecran_macros_emplacement_t emplacements[ECRAN_MACROS_PAGE_TAILLE]; /* user_data des rappels de clic */

    lv_obj_t *bouton_precedent;
    lv_obj_t *bouton_suivant;
    lv_obj_t *page_label;

    /* Copie FILTRÉE (sans les `_préfixées`) et mémorisée par mettre_a_jour(),
     * relue par les rappels de clic/pagination -- ni `etat` ni `contexte` ne
     * survivent entre deux appels de mettre_a_jour() côté appelant, ce
     * tampon-ci le fait exprès (même raison que ctx->en_pause dans
     * ecran_accueil_ctx_t). */
    char    macros_filtrees[KLIPPER_MACROS_MAX][KLIPPER_MACRO_NOM_MAX];
    uint8_t nb_filtrees;
    bool    macros_tronquees;
    bool    donnees_perimees;
    uint8_t page; /* 0-indexé */
} ecran_macros_ctx_t;

extern const ecran_desc_t ECRAN_MACROS;
