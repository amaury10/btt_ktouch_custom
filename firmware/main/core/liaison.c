#include "liaison.h"

void liaison_init(liaison_t *l, uint8_t seuil_degrade, uint8_t seuil_hors_ligne)
{
    l->etat = LIAISON_CONNEXION;
    l->echecs_consecutifs = 0;
    l->seuil_degrade = seuil_degrade;
    l->seuil_hors_ligne = seuil_hors_ligne;
}

void liaison_succes(liaison_t *l)
{
    /* Un succès efface l'historique et ramène en ligne sans étape
     * intermédiaire : quelqu'un qui vient de rebrancher sa machine veut voir
     * l'état revenir tout de suite. */
    l->echecs_consecutifs = 0;
    l->etat = LIAISON_EN_LIGNE;
}

void liaison_echec(liaison_t *l)
{
    if (l->echecs_consecutifs < UINT32_MAX) {
        l->echecs_consecutifs++;
    }
    if (l->echecs_consecutifs >= l->seuil_hors_ligne) {
        l->etat = LIAISON_HORS_LIGNE;
    } else if (l->echecs_consecutifs >= l->seuil_degrade) {
        l->etat = LIAISON_DEGRADEE;
    }
    /* Sous le premier seuil, l'état ne bouge pas : un échec isolé sur un réseau
     * local est banal et ne doit rien signaler. */
}

liaison_etat_t liaison_etat(const liaison_t *l) { return l->etat; }
uint32_t liaison_echecs_consecutifs(const liaison_t *l) { return l->echecs_consecutifs; }

const char *liaison_nom(liaison_etat_t etat)
{
    switch (etat) {
        case LIAISON_CONNEXION:  return "connexion";
        case LIAISON_EN_LIGNE:   return "en ligne";
        case LIAISON_DEGRADEE:   return "degradee";
        case LIAISON_HORS_LIGNE: return "hors ligne";
    }
    return "inconnu";
}
