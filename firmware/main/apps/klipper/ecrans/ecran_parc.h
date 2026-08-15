/* Écran Parc (spec 2026-08-15-gestion-parc-design.md) : une tuile par
 * imprimante configurée (parc_imprimantes.h), l'ACTIVE nourrie par l'état
 * temps réel (etat_klipper_t reçu via mettre_a_jour), les autres par la
 * sonde séquentielle (parc_sonde.h -- armée à construire(), désarmée à
 * detruire() : aucun trafic de fond écran fermé). Tap sur une tuile non
 * active -> confirmation -> bascule : l'entrée devient active (NVS) + son
 * hôte est recopié dans le réglage hôte historique + redémarrage de la
 * dalle -- le redémarrage EST le chemin d'application d'un changement
 * d'hôte existant (voir ecran_configuration.c, « power-cycle pour
 * l'instant »). Dernière tuile « + Add printer » si le parc n'est pas
 * plein : clavier nom puis clavier adresse (validée par
 * ecran_configuration_valider(), jamais un second analyseur).
 *
 * ecran_parc_ctx_t exposé, même raison que les autres écrans (tests host
 * par lecture de libellés). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "parc_imprimantes.h"

typedef struct ecran_parc_ctx_s ecran_parc_ctx_t;

/* Contexte de clic d'une tuile -- même patron que ecran_usb_emplacement_t. */
typedef struct {
    ecran_parc_ctx_t *ctx;
    uint8_t           indice;
} ecran_parc_emplacement_t;

struct ecran_parc_ctx_s {
    lv_obj_t *tuiles[PARC_MAX];
    lv_obj_t *labels[PARC_MAX]; /* enfant direct de tuiles[i] */
    lv_obj_t *bouton_ajouter;   /* NULL si jamais construit ; masqué si parc plein */
    lv_obj_t *vide;             /* "No printers configured" */

    /* Copies locales (mêmes raisons que partout : les rappels de clic
     * relisent ce que l'écran affiche, jamais le store directement). */
    parc_config_t config;
    parc_etat_t   etats[PARC_MAX];

    /* Résumé temps réel de l'ACTIVE, extrait du dernier mettre_a_jour(). */
    char    etat_actif[16];
    float   buse_actif;
    float   lit_actif;
    bool    actif_disponible; /* faux = données périmées/absentes -> tuile "?" */

    uint32_t derniere_generation;

    /* Bascule en attente de confirmation. */
    uint8_t indice_attente;

    /* Ajout en cours : nom saisi, en attente de l'adresse. */
    char nom_attente[PARC_NOM_MAX];

    ecran_parc_emplacement_t emplacements[PARC_MAX];
};

extern const ecran_desc_t ECRAN_PARC;
