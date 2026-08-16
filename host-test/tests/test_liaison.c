#include "liaison.h"
#include "petit_test.h"

void suite_liaison(void)
{
    printf("suite : liaison\n");

    liaison_t l;
    liaison_init(&l, 2, 5);   /* degradee a 2 echecs, hors ligne a 5 */

    /* Au demarrage, on est en cours de connexion : ni en ligne ni hors ligne. */
    VERIFIER(liaison_etat(&l) == LIAISON_CONNEXION);

    /* Un premier succes fait passer en ligne. */
    liaison_succes(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);
    VERIFIER(liaison_echecs_consecutifs(&l) == 0);

    /* Un echec isole ne doit pas alarmer : le reseau local perd des paquets. */
    liaison_echec(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);

    /* Au seuil, on passe en degradee. */
    liaison_echec(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_DEGRADEE);
    VERIFIER(liaison_echecs_consecutifs(&l) == 2);

    /* Un succes efface tout, immediatement. */
    liaison_succes(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);
    VERIFIER(liaison_echecs_consecutifs(&l) == 0);

    /* Au second seuil, hors ligne. */
    for (int i = 0; i < 5; i++) {
        liaison_echec(&l);
    }
    VERIFIER(liaison_etat(&l) == LIAISON_HORS_LIGNE);

    /* Et l'on en sort des le premier succes, sans etape intermediaire :
     * l'utilisateur qui rebranche veut voir l'etat revenir tout de suite. */
    liaison_succes(&l);
    VERIFIER(liaison_etat(&l) == LIAISON_EN_LIGNE);

    /* Les noms servent a la barre d'etat et au journal : ils doivent exister
     * pour toutes les valeurs. */
    VERIFIER_TEXTE(liaison_nom(LIAISON_CONNEXION), "connexion");
    VERIFIER_TEXTE(liaison_nom(LIAISON_EN_LIGNE), "en ligne");
    VERIFIER_TEXTE(liaison_nom(LIAISON_DEGRADEE), "degradee");
    VERIFIER_TEXTE(liaison_nom(LIAISON_HORS_LIGNE), "hors ligne");

    /* Depuis l'etat initial, des echecs menent hors ligne sans jamais etre
     * passe par en ligne : une machine eteinte au demarrage doit le dire. */
    liaison_t neuve;
    liaison_init(&neuve, 2, 5);
    for (int i = 0; i < 5; i++) {
        liaison_echec(&neuve);
    }
    VERIFIER(liaison_etat(&neuve) == LIAISON_HORS_LIGNE);
}
