#include <stdio.h>

int tests_echoues = 0;
int tests_lances = 0;

void suite_harnais(void);

int main(void)
{
    suite_harnais();

    printf("\n%d verification(s), %d echec(s)\n", tests_lances, tests_echoues);
    return tests_echoues == 0 ? 0 : 1;
}
