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
     * naturellement. On le verifie ici en observant le compteur
     * d'echecs directement, puisque le harnais ne s'arrete pas au
     * premier echec ; on le decremente ensuite pour que la suite se
     * termine toujours a zero echec. */
    {
        int echecs_avant = tests_echoues;
        VERIFIER_FLOAT(0.0f / 0.0f, 0.0f, 0.1f);
        VERIFIER(tests_echoues == echecs_avant + 1);
        tests_echoues = echecs_avant;
    }
}
