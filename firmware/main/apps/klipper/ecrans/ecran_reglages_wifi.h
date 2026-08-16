/* Écran de réglages WiFi (sous-projet 7, tâche 4) : balaye les réseaux à
 * portée, en liste les SSID (barres RSSI + cadenas si chiffré), et permet de
 * se (re)connecter à chaud — mot de passe saisi au clavier tactile pour un
 * réseau chiffré, connexion directe pour un réseau ouvert. Câblé depuis
 * l'écran de configuration (bouton « WiFi »).
 *
 * Le point délicat de cet écran est le BALAYAGE : wifi_scanner() (voir
 * wifi.h) est BLOQUANT (plusieurs secondes) et NE DOIT JAMAIS être appelé
 * depuis un rappel LVGL. L'écran le lance donc hors du fil LVGL (une tâche
 * FreeRTOS dédiée sur l'appareil ; un appel synchrone en host-test/simulateur
 * où wifi_scanner est un mock instantané — voir ecran_reglages_wifi.c) et
 * dépose le résultat dans des buffers MODULE-STATIC qui survivent à la
 * destruction de l'écran. mettre_a_jour() interroge (poll) un drapeau et
 * peuple la liste quand le balayage est terminé.
 *
 * `ecran_reglages_wifi_ctx_t` est exposé ici plutôt qu'opaque, exactement
 * pour la même raison que ecran_macros_ctx_t (voir ecran_macros.h) :
 * host-test/tests/test_ecran_reglages_wifi.c relit les libellés et l'état de
 * pagination via lv_label_get_text() pour prouver ce que l'écran affiche,
 * sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "wifi.h"

/* Nombre maximal de réseaux mémorisés d'un balayage. wifi_scanner() dédoublonne
 * par SSID ; 16 couvre très largement ce qu'un balayage rend en pratique. */
#define WIFI_RESEAUX_MAX 16

/* Une page de liste à la fois : quatre lignes pleine largeur (voir le calcul
 * de mise en page en tête de ecran_reglages_wifi.c). 16 / 4 = 4 pages au pire. */
#define WIFI_LIGNES_PAR_PAGE 4

/* Contexte d'un emplacement de ligne, passé en user_data du rappel de tap :
 * même modèle que ecran_macros_emplacement_t. */
typedef struct {
    struct ecran_reglages_wifi_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    uint8_t                           emplacement; /* 0..WIFI_LIGNES_PAR_PAGE-1, position FIXE */
} ecran_reglages_wifi_emplacement_t;

typedef struct ecran_reglages_wifi_ctx_s {
    lv_obj_t *etat_label;        /* en-tête : SSID courant / état de reconfiguration */
    lv_obj_t *bouton_rescanner;  /* relance un balayage */
    lv_obj_t *recherche_label;   /* « Recherche… », visible pendant un balayage */
    lv_obj_t *vide_label;        /* « Aucun réseau — Rescanner », liste vide */

    lv_obj_t *boutons[WIFI_LIGNES_PAR_PAGE];    /* une ligne = un bouton pleine largeur */
    lv_obj_t *ssid_labels[WIFI_LIGNES_PAR_PAGE];/* enfant du bouton : le SSID */
    lv_obj_t *cadenas[WIFI_LIGNES_PAR_PAGE];    /* petit cadenas, masqué si réseau ouvert */
    lv_obj_t *barres[WIFI_LIGNES_PAR_PAGE][4];  /* 4 barres de RSSI par ligne */
    ecran_reglages_wifi_emplacement_t emplacements[WIFI_LIGNES_PAR_PAGE];

    lv_obj_t *bouton_precedent;
    lv_obj_t *bouton_suivant;
    lv_obj_t *page_label;

    /* Copie du dernier balayage rendu, relue par les rappels de tap/pagination
     * (les buffers module-static peuvent être écrasés par un rebalayage
     * concurrent — cette copie-ci est stable pour toute la durée de vie de
     * l'écran). */
    wifi_reseau_t reseaux[WIFI_RESEAUX_MAX];
    size_t        nb;
    uint8_t       page; /* 0-indexé */

    /* Vrai tant que cette instance attend le résultat du balayage courant
     * (posé à la construction et au « Rescanner », remis à faux dès que la
     * liste est peuplée) — distingue « en cours » d'« déjà affiché ». */
    bool attente_resultat;

    /* SSID capturé au moment du tap sur une ligne chiffrée : passé au clavier
     * puis à wifi_reconfigurer() une fois le mot de passe validé. Capturé pour
     * ne jamais dépendre d'un indice de ligne qui aurait pu changer entre le
     * tap et la validation. */
    char ssid_en_attente[33];
} ecran_reglages_wifi_ctx_t;

extern const ecran_desc_t ECRAN_REGLAGES_WIFI;
