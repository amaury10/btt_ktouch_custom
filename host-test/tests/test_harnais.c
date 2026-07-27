#include "petit_test.h"

void suite_harnais(void)
{
    printf("suite : harnais\n");
    VERIFIER(1 + 1 == 2);
    VERIFIER_FLOAT(0.1f + 0.2f, 0.3f, 0.0001f);
    VERIFIER_TEXTE("k-touch", "k-touch");
}
