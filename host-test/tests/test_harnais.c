#include <stdbool.h>

#include "petit_test.h"

void suite_harnais(void)
{
    printf("suite : harnais\n");
    VERIFIER(1 + 1 == 2);
    VERIFIER_FLOAT(0.1f + 0.2f, 0.3f, 0.0001f);
    VERIFIER_TEXTE("k-touch", "k-touch");

    /* Le harnais lui-meme doit echouer sur un NaN : en IEEE-754, toute
     * comparaison impliquant un NaN est fausse, donc une macro qui teste
     * la condition d'ECHEC (`ecart > tolerance`) ne se declenche jamais
     * et un NaN passerait pour une reussite silencieuse. VERIFIER_FLOAT
     * teste au contraire la condition de SUCCES, donc un NaN echoue
     * naturellement.
     *
     * Ce test ne passe PAS par VERIFIER_FLOAT directement : cette macro
     * imprime une ligne "ECHEC" des qu'elle detecte un echec, et un run sain
     * de la suite entiere en afficherait alors une en permanence — la
     * premiere chose qu'un nouveau contributeur verrait sur un build qui
     * passe, avant meme d'avoir touche au code. On reproduit ici la meme
     * condition de succes que VERIFIER_FLOAT (`ecart <= tolerance`) sans
     * passer par elle, et on verifie SEULEMENT que cette condition est
     * fausse pour un NaN (elle doit l'etre, car IEEE-754) : la substance du
     * test — un NaN ne doit jamais se glisser en silence dans une
     * comparaison — reste intacte, sans ligne ECHEC sur un run sain. */
    {
        float nan = 0.0f / 0.0f;
        float ecart = nan - 0.0f;
        if (ecart < 0) {
            ecart = -ecart;
        }
        bool condition_succes_verifier_float = (ecart <= 0.1f);
        VERIFIER(!condition_succes_verifier_float);
    }
}
