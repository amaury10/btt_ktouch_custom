#include "petit_test.h"
#include "klipper_paliers.h"

void suite_klipper_paliers(void)
{
    printf("suite : klipper_paliers\n");
    /* bornes exactes du choix */
    VERIFIER(palier_outils(0) == PALIER_MONO);
    VERIFIER(palier_outils(1) == PALIER_MONO);
    VERIFIER(palier_outils(2) == PALIER_MOYEN);
    VERIFIER(palier_outils(4) == PALIER_MOYEN);
    VERIFIER(palier_outils(5) == PALIER_COMPACT);
    VERIFIER(palier_outils(8) == PALIER_COMPACT);
    /* géométrie et police cohérentes avec la spec §6 */
    VERIFIER(palier_colonnes(PALIER_MONO) == 1);
    VERIFIER(palier_colonnes(PALIER_MOYEN) == 2);
    VERIFIER(palier_colonnes(PALIER_COMPACT) == 2);
    VERIFIER(palier_taille_police(PALIER_MONO) == 48);
    VERIFIER(palier_taille_police(PALIER_MOYEN) == 28);
    VERIFIER(palier_taille_police(PALIER_COMPACT) == 20);
}
