/* Écran fichiers (sous-projet 6 « browser de fichiers », tâche 3, dernière
 * tâche) : la preuve de bout en bout du sous-projet -- « lister les fichiers
 * gcode connus de Moonraker et en démarrer l'impression ». Grille paginée de
 * NOMS, quasi identique à ecran_macros.c (voir son commentaire de tête pour
 * l'ossature complète : grille 4x4 paginée, contexte par emplacement pour le
 * rappel de clic, grisage systématique C3) -- deux différences structurelles :
 *
 *   1. La source est `etat->fichiers[0..nb_fichiers-1]` (T1/T2 du
 *      sous-projet), pas `etat->macros[]` -- AUCUN filtrage par préfixe ici,
 *      contrairement aux macros `_préfixées` : un chemin de fichier n'a pas
 *      cette convention Klipper, chaque entrée annoncée est affichée telle
 *      quelle (éventuellement avec un '/', sous-dossier -- pas de navigation
 *      de dossiers, le nom complet sert de libellé).
 *   2. Un tap n'agit PAS directement : il ouvre une confirmation
 *      (confirmation.h, "Print file?") -- démarrer une impression ne doit
 *      jamais partir d'un simple effleurement, contrairement au lancement
 *      d'une macro (ecran_macros.c, aucune confirmation). Le nom transmis à
 *      klipper_gcode_imprimer_fichier() est TOUJOURS celui de
 *      `etat->fichiers[i]` au moment du tap, jamais une saisie manuelle --
 *      c'est ce qui rend ce chemin sûr sans filtre de caractères (voir le
 *      commentaire de klipper_gcode_imprimer_fichier() dans klipper_gcode.h).
 *
 * Géométrie 742x436 (conteneur de navigation à droite du rail persistant,
 * même largeur que ecran_accueil.c/ecran_accueil_hub.c/ecran_deplacer.c/
 * ecran_temperatures.c/ecran_extruder.c/ecran_ventilateurs.c) -- PAS les
 * 800px hérités de ecran_macros.c (écrit avant la conversion au rail
 * persistant, jamais reconverti depuis, hors de portée de cette tâche).
 *
 * `ecran_fichiers_ctx_t` est exposé ici plutôt qu'opaque, exactement pour la
 * même raison que `ecran_macros_ctx_t` (voir son .h) : host-test/tests/
 * test_ecran_fichiers.c a besoin de relire les libellés via
 * lv_label_get_text() et l'état de pagination pour prouver ce que
 * mettre_a_jour() affiche, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"

/* Une page à la fois : 4 colonnes × 4 lignes, même géométrie de grille que
 * ecran_macros.c. 32 (KLIPPER_FICHIERS_MAX) ÷ 16 = 2 pages au pire cas. */
#define ECRAN_FICHIERS_PAGE_COLONNES 4
#define ECRAN_FICHIERS_PAGE_LIGNES   4
#define ECRAN_FICHIERS_PAGE_TAILLE   (ECRAN_FICHIERS_PAGE_COLONNES * ECRAN_FICHIERS_PAGE_LIGNES)

typedef struct {
    struct ecran_fichiers_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    uint8_t                      emplacement; /* 0..ECRAN_FICHIERS_PAGE_TAILLE-1, position FIXE dans la grille */
} ecran_fichiers_emplacement_t;

typedef struct ecran_fichiers_ctx_s {
    lv_obj_t *avertissement; /* ligne de troncature honnête, masquée sauf fichiers_tronques */
    lv_obj_t *vide;          /* "No files", visible seulement si la liste est vide */

    lv_obj_t *boutons[ECRAN_FICHIERS_PAGE_TAILLE];
    lv_obj_t *labels[ECRAN_FICHIERS_PAGE_TAILLE];  /* enfant direct de boutons[i] */
    ecran_fichiers_emplacement_t emplacements[ECRAN_FICHIERS_PAGE_TAILLE]; /* user_data des rappels de clic */

    lv_obj_t *bouton_precedent;
    lv_obj_t *bouton_suivant;
    lv_obj_t *page_label;

    /* Copie bornée (défensivement NUL-terminée, voir mettre_a_jour()) et
     * mémorisée, relue par les rappels de clic/pagination -- même raison que
     * ctx->macros_filtrees dans ecran_macros_ctx_t. */
    char    fichiers_copie[KLIPPER_FICHIERS_MAX][KLIPPER_FICHIER_MAX];
    uint8_t nb_fichiers;
    bool    fichiers_tronques;
    bool    donnees_perimees;
    uint8_t page; /* 0-indexé */

    /* Nom du fichier tapé, en attente de la résolution de la confirmation
     * ouverte -- le dialogue reste modal potentiellement plusieurs cycles
     * (le temps qu'un humain lise "Print file?"), donc ce nom doit survivre
     * au-delà du retour de bouton_fichier_cb() ; `ctx` (ce contexte,
     * persistant tant que cet écran est empilé) est passé comme `contexte`
     * à confirmation_ouvrir(), exactement pour porter ce champ jusqu'au
     * rappel. */
    char nom_attente[KLIPPER_FICHIER_MAX];
} ecran_fichiers_ctx_t;

extern const ecran_desc_t ECRAN_FICHIERS;
