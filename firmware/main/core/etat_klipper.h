/* État d'une machine Klipper tel que l'interface le consomme.
 *
 * Structure POD, taille fixe, sans pointeur : c'est ce qui permet au socle de
 * détecter un changement par simple comparaison mémoire (voir etat_store.h) et
 * d'allouer une fois pour toutes. Ajouter un champ est sans danger ; ajouter un
 * pointeur casserait la détection de changement. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KLIPPER_ETAT_TEXTE_MAX  24
#define KLIPPER_FICHIER_MAX     64

typedef struct {
    char  etat[KLIPPER_ETAT_TEXTE_MAX];   /* "ready", "printing", "paused", "error" */

    float buse_actuelle;
    float buse_consigne;
    float plateau_actuel;
    float plateau_consigne;

    char     fichier[KLIPPER_FICHIER_MAX];
    float    progression;                  /* 0.0 à 1.0 */
    uint32_t temps_restant_s;              /* 0 si inconnu */

    bool impression_en_cours;
    bool impression_en_pause;
} etat_klipper_t;
