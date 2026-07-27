#include <string.h>

#include "etat_klipper.h"
#include "petit_test.h"

void suite_contrat(void)
{
    printf("suite : contrat\n");

    /* La comparaison mémoire du magasin d'état n'est valable que si la
     * structure est comparable octet à octet : pas de pointeur, taille stable. */
    etat_klipper_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    VERIFIER(memcmp(&a, &b, sizeof(a)) == 0);

    a.buse_actuelle = 210.0f;
    VERIFIER(memcmp(&a, &b, sizeof(a)) != 0);

    /* Deux structures remplies identiquement champ par champ doivent être
     * indistinguables : si ce test échoue un jour, c'est qu'un champ non
     * initialisé ou un pointeur s'est glissé dans le modèle. */
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    snprintf(a.fichier, sizeof(a.fichier), "piece.gcode");
    snprintf(b.fichier, sizeof(b.fichier), "piece.gcode");
    a.progression = b.progression = 0.5f;
    VERIFIER(memcmp(&a, &b, sizeof(a)) == 0);
}
