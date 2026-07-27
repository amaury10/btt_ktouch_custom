#include <stdio.h>

int tests_echoues = 0;
int tests_lances = 0;

void suite_harnais(void);
void suite_contrat(void);
void suite_moonraker_parse(void);
void suite_etat_store(void);
void suite_liaison(void);
void suite_backend_factice(void);

int main(void)
{
    suite_harnais();
    suite_contrat();
    suite_moonraker_parse();
    suite_etat_store();
    suite_liaison();
    suite_backend_factice();

    printf("\n%d verification(s), %d echec(s)\n", tests_lances, tests_echoues);
    return tests_echoues == 0 ? 0 : 1;
}
