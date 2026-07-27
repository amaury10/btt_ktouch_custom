/* Santé de la liaison avec l'hôte, exprimée en quatre états.
 *
 * L'habillage est seul à afficher cet état : un écran ne montre jamais de boîte
 * d'erreur réseau, il grise ses données périmées. Cette règle évite que chaque
 * panneau invente sa propre façon de dire « je n'ai pas de nouvelles ». */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LIAISON_CONNEXION = 0,  /* jamais joint l'hôte depuis le démarrage */
    LIAISON_EN_LIGNE,
    LIAISON_DEGRADEE,       /* des échecs, pas encore de quoi renoncer */
    LIAISON_HORS_LIGNE,
} liaison_etat_t;

typedef struct {
    liaison_etat_t etat;
    uint32_t       echecs_consecutifs;
    uint8_t        seuil_degrade;
    uint8_t        seuil_hors_ligne;
} liaison_t;

void           liaison_init(liaison_t *l, uint8_t seuil_degrade, uint8_t seuil_hors_ligne);
void           liaison_succes(liaison_t *l);
void           liaison_echec(liaison_t *l);
liaison_etat_t liaison_etat(const liaison_t *l);
uint32_t       liaison_echecs_consecutifs(const liaison_t *l);
const char    *liaison_nom(liaison_etat_t etat);
