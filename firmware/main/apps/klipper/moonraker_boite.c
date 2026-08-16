/* Implémentation : voir moonraker_boite.h pour le contrat complet (un slot,
 * pas une file ; pas de verrou ; portable). */
#include "moonraker_boite.h"

#include <string.h>

void boite_deposer(moonraker_boite_t *b, const etat_klipper_t *etat)
{
    memcpy(&b->etat, etat, sizeof(b->etat));
    b->neuf = true;
}

bool boite_drainer(moonraker_boite_t *b, etat_klipper_t *sortie)
{
    if (!b->neuf) {
        return false;
    }
    memcpy(sortie, &b->etat, sizeof(*sortie));
    b->neuf = false;
    return true;
}

bool boite_a_du_neuf(const moonraker_boite_t *b)
{
    return b->neuf;
}
