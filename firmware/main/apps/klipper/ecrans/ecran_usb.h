/* Écran USB (feature "Impression depuis USB", tâche B) : pont clé USB ->
 * Moonraker -- liste les .gcode trouvés sur la clé montée (usb_fichiers.h,
 * rempli par le scan de usb_scan.c au montage, démarré paresseusement par cet
 * écran lui-même -- voir usb_scan.h et ecran_usb_construire()) et, sur
 * confirmation, les envoie en streaming vers Moonraker (usb_upload_http.h)
 * avec `print=true`
 * (Moonraker démarre l'impression lui-même, pas de SDCARD_PRINT_FILE séparé
 * -- voir docs/superpowers/specs/2026-08-04-usb-impression-design.md).
 *
 * Ossature reprise de ecran_fichiers.c (colonne unique de boutons pleine
 * largeur, paginée, contexte par emplacement pour le rappel de clic, tap ->
 * confirmation_ouvrir() avant toute action -- jamais un envoi qui démarre une
 * impression sur un simple effleurement) : mêmes dimensions verticales
 * (page de 5 boutons de 52 px), même géométrie 742x436 (conteneur de
 * navigation à droite du rail persistant). DEUX différences structurelles :
 *
 *   1. La source est `usb_fichiers_lire()` (store dédié, jamais
 *      etat_klipper_t -- cet écran n'a d'ailleurs besoin d'AUCUN champ de
 *      l'état Klipper, `mettre_a_jour()` ignore `etat`/`donnees_perimees`).
 *   2. Une rangée de statut/progression, EN HAUT de l'écran (au-dessus de la
 *      grille, budget vertical serré -- voir les _Static_assert du .c) :
 *      pendant un upload (usb_upload_http_en_cours()), une barre de
 *      progression (widgets/progression.h) remplace le texte de statut ; hors
 *      upload, ce texte porte le résultat du dernier envoi (SUCCES/ECHEC) ou
 *      reste vide. Les deux widgets partagent la MÊME zone, un seul visible à
 *      la fois -- jamais assez de hauteur disponible pour les deux en même
 *      temps (voir ecran_usb.c).
 *
 * `ecran_usb_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_fichiers_ctx_t : un futur host-test pourrait relire les libellés via
 * lv_label_get_text() sans jamais regarder un pixel (aucun test de ce genre
 * n'est écrit par cette tâche -- ce fichier ne compile de toute façon que
 * pour la cible ESP, esp_http_client n'existe pas côté host-test). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "progression.h"
#include "usb_fichiers.h" /* USB_FICHIERS_MAX / USB_FICHIER_CHEMIN_MAX */

/* Une page à la fois, une colonne de boutons pleine largeur -- même taille de
 * page que ecran_fichiers.c (5 boutons de 52 px, voir le calcul complet en
 * tête de ecran_usb.c). USB_FICHIERS_MAX (32) / 5 = 7 pages au pire cas. */
#define ECRAN_USB_PAGE_TAILLE 5

typedef struct {
    struct ecran_usb_ctx_s *ctx;
    uint8_t                 emplacement; /* 0..ECRAN_USB_PAGE_TAILLE-1, position FIXE dans la grille */
} ecran_usb_emplacement_t;

typedef struct ecran_usb_ctx_s {
    lv_obj_t *statut_texte; /* résultat du dernier envoi (ou vide), masqué pendant un upload */
    progression_t progression; /* barre d'avancement, visible SEULEMENT pendant un upload */

    lv_obj_t *vide; /* "Insert a USB key" / "No .gcode files on this USB key" */

    lv_obj_t *boutons[ECRAN_USB_PAGE_TAILLE];
    lv_obj_t *labels[ECRAN_USB_PAGE_TAILLE]; /* enfant direct de boutons[i] */
    ecran_usb_emplacement_t emplacements[ECRAN_USB_PAGE_TAILLE];

    lv_obj_t *bouton_precedent;
    lv_obj_t *bouton_suivant;
    lv_obj_t *page_label;

    /* Copie bornée (défensivement NUL-terminée) du store, mémorisée pour les
     * rappels de clic/pagination -- même raison que ctx->fichiers_copie dans
     * ecran_fichiers_ctx_t. `chemins_copie[i]` porte le chemin COMPLET
     * ("/usb/..."), affiché tel quel comme libellé (pas de nom raccourci --
     * le chemin sous /usb EST le nom le plus lisible pour cette source). */
    char     chemins_copie[USB_FICHIERS_MAX][USB_FICHIER_CHEMIN_MAX];
    size_t   tailles_copie[USB_FICHIERS_MAX];
    uint8_t  nb_fichiers;
    bool     tronques;
    bool     monte;
    uint8_t  page; /* 0-indexé */

    /* Génération du store USB vue au dernier mettre_a_jour() -- évite de
     * recopier/redessiner la liste entière à chaque pompage (potentiellement
     * plusieurs fois par seconde) quand rien n'a changé côté clé USB ; la
     * rangée de statut/progression, elle, est TOUJOURS réévaluée (voir
     * ecran_usb.c : l'état d'upload change en continu pendant un envoi, sans
     * jamais toucher la génération du store fichiers). */
    uint32_t derniere_generation;

    /* Chemin/taille du fichier tapé, en attente de la résolution de la
     * confirmation ouverte -- même raison que ctx->nom_attente dans
     * ecran_fichiers_ctx_t (le dialogue reste modal potentiellement plusieurs
     * cycles). */
    char   chemin_attente[USB_FICHIER_CHEMIN_MAX];
    size_t taille_attente;
} ecran_usb_ctx_t;

extern const ecran_desc_t ECRAN_USB;
