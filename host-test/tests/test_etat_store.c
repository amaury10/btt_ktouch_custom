#include <string.h>

#include "etat_store.h"
#include "petit_test.h"

typedef struct { int a; float b; char c[8]; } exemple_t;

void suite_etat_store(void)
{
    printf("suite : magasin d'etat\n");

    etat_store_t s;
    VERIFIER(etat_store_init(&s, sizeof(exemple_t)));

    /* Au depart, tout est a zero et la generation vaut 0. */
    const exemple_t *lu = etat_store_lire(&s);
    VERIFIER(lu->a == 0 && lu->b == 0.0f && lu->c[0] == '\0');
    VERIFIER(etat_store_generation(&s) == 0);

    /* Ecrire la meme chose ne doit PAS declencher de changement. */
    exemple_t *arriere = etat_store_tampon_arriere(&s);
    memset(arriere, 0, sizeof(exemple_t));
    VERIFIER(!etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 0);

    /* Un vrai changement permute et incremente la generation. */
    arriere = etat_store_tampon_arriere(&s);
    arriere->a = 42;
    VERIFIER(etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 1);
    lu = etat_store_lire(&s);
    VERIFIER(lu->a == 42);

    /* Le tampon arriere est remis a zero avant chaque remplissage : sans ca, le
     * remplissage laisse par l'alignement de la structure ferait echouer la
     * comparaison au hasard. */
    arriere = etat_store_tampon_arriere(&s);
    VERIFIER(arriere->a == 0);

    /* Rejouer la meme valeur qu'en facade ne change rien. */
    arriere->a = 42;
    VERIFIER(!etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 1);

    /* Un changement dans une chaine est detecte. */
    arriere = etat_store_tampon_arriere(&s);
    arriere->a = 42;
    snprintf(arriere->c, sizeof(arriere->c), "bonjour");
    VERIFIER(etat_store_valider(&s));
    VERIFIER(etat_store_generation(&s) == 2);
    lu = etat_store_lire(&s);
    VERIFIER_TEXTE(lu->c, "bonjour");

    etat_store_liberer(&s);

    /* Une taille nulle est refusee plutot que de produire un magasin inutile.
     * On pollue volontairement la memoire avant l'appel : si etat_store_init()
     * ne remettait pas les champs a zero sur ce chemin d'echec, un store sur
     * la pile garderait des pointeurs indetermines et liberer() plus bas
     * appellerait free() sur n'importe quoi. Partir d'une pile deja a zero
     * masquerait ce bug. */
    etat_store_t vide;
    memset(&vide, 0x5A, sizeof(vide));
    VERIFIER(!etat_store_init(&vide, 0));

    /* L'etat d'echec doit etre observable, pas seulement inoffensif. */
    VERIFIER(etat_store_lire(&vide) == NULL);
    VERIFIER(etat_store_generation(&vide) == 0);

    /* Et liberer() doit rester sur sur un store dont l'init a echoue. */
    etat_store_liberer(&vide);
}
