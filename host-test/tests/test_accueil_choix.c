#include "petit_test.h"
#include "accueil_choix.h"
#include <string.h>

void suite_accueil_choix(void)
{
    printf("suite : accueil_choix\n");
    etat_klipper_t e;
    memset(&e, 0, sizeof(e));
    /* repos : impression pas en cours => accueil-hub */
    e.impression_en_cours = false;
    VERIFIER(accueil_impression_actif(&e) == false);
    /* impression en cours => accueil impression */
    e.impression_en_cours = true;
    VERIFIER(accueil_impression_actif(&e) == true);
    /* pause = impression en cours (juste suspendue) => impression */
    e.impression_en_pause = true;
    VERIFIER(accueil_impression_actif(&e) == true);
}
