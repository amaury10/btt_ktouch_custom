#include "klipper_paliers.h"

palier_outils_t palier_outils(uint8_t nb_extrudeurs)
{
    if (nb_extrudeurs <= 1) {
        return PALIER_MONO;
    }
    if (nb_extrudeurs <= 4) {
        return PALIER_MOYEN;
    }
    return PALIER_COMPACT;
}

uint8_t palier_colonnes(palier_outils_t palier)
{
    switch (palier) {
        case PALIER_MONO:
            return 1;
        case PALIER_MOYEN:
            return 2;
        case PALIER_COMPACT:
            return 2;
        default:
            return 0;
    }
}

uint8_t palier_taille_police(palier_outils_t palier)
{
    switch (palier) {
        case PALIER_MONO:
            return 48;
        case PALIER_MOYEN:
            return 28;
        case PALIER_COMPACT:
            return 20;
        default:
            return 0;
    }
}
