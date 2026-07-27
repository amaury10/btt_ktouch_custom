/* Les quelques valeurs matérielles dont l'habillage a besoin, derrière une
 * façade que le simulateur peut fournir autrement.
 *
 * Volontairement minuscule : n'ajouter une fonction ici que le jour où un
 * écran en a besoin. Une façade de plateforme qui grossit sans usage devient
 * une couche d'abstraction à maintenir des deux côtés pour rien. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t heures;    /* 0-23 */
    uint8_t minutes;   /* 0-59 */
    bool    valide;    /* false tant qu'aucune heure n'a été obtenue */
} plateforme_heure_t;

typedef struct {
    uint8_t pourcentage;  /* 0-100 */
    bool    en_charge;
    bool    valide;       /* false si la mesure n'est pas disponible */
} plateforme_batterie_t;

typedef struct {
    bool    associe;
    int8_t  rssi;        /* dBm, négatif ; 0 si !associe */
    uint8_t barres;      /* 0-4, dérivé du rssi par plateforme_wifi_barres() */
} plateforme_wifi_t;

void plateforme_heure(plateforme_heure_t *sortie);
void plateforme_batterie(plateforme_batterie_t *sortie);
void plateforme_wifi(plateforme_wifi_t *sortie);

/* Conversion pure, partagée par les deux implémentations pour que le
 * simulateur et l'appareil affichent le même nombre de barres au même
 * signal. Seuils : >= -55 → 4, >= -65 → 3, >= -75 → 2, >= -85 → 1, sinon 0. */
uint8_t plateforme_wifi_barres(int8_t rssi);
