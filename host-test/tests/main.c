#include <stdio.h>

int tests_echoues = 0;
int tests_lances = 0;

void suite_harnais(void);
void suite_contrat(void);

int main(void)
{
    suite_harnais();
    suite_contrat();

    printf("\n%d verification(s), %d echec(s)\n", tests_lances, tests_echoues);
    return tests_echoues == 0 ? 0 : 1;
}
