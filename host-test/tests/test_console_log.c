#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "console_log.h"
#include "petit_test.h"

/* --- append normal + lecture ---------------------------------------------- */

static void section_ajouter_normal(void)
{
    console_log_effacer();

    console_log_t avant;
    console_log_lire(&avant);
    VERIFIER(avant.nb == 0);
    VERIFIER(avant.debut == 0);
    uint32_t gen0 = avant.generation;

    console_log_ajouter("// echo: G28");
    console_log_ajouter(">> G28");

    console_log_t lu;
    console_log_lire(&lu);
    VERIFIER(lu.nb == 2);
    VERIFIER(lu.debut == 0);
    VERIFIER_TEXTE(lu.lignes[0], "// echo: G28");
    VERIFIER_TEXTE(lu.lignes[1], ">> G28");
    VERIFIER(lu.generation == gen0 + 2);   /* generation++ a CHAQUE ajout */

    /* ligne NULL : no-op complet, generation INCHANGEE */
    console_log_ajouter(NULL);
    console_log_t apres_nul;
    console_log_lire(&apres_nul);
    VERIFIER(apres_nul.nb == 2);
    VERIFIER(apres_nul.generation == lu.generation);
}

/* --- eviction FIFO au-dela de CONSOLE_LIGNES_MAX --------------------------
 * Pousse CONSOLE_LIGNES_MAX + 5 lignes numerotees ; les 5 plus anciennes
 * doivent avoir disparu, les CONSOLE_LIGNES_MAX dernieres doivent rester,
 * dans l'ORDRE (la plus ancienne restante en premier). */

static void section_eviction_fifo(void)
{
    console_log_effacer();

    char tampon[CONSOLE_LIGNE_MAX];
    const int total = CONSOLE_LIGNES_MAX + 5;
    for (int i = 0; i < total; i++) {
        snprintf(tampon, sizeof(tampon), "ligne%d", i);
        console_log_ajouter(tampon);
    }

    console_log_t lu;
    console_log_lire(&lu);
    VERIFIER(lu.nb == CONSOLE_LIGNES_MAX);   /* jamais plus que MAX, pas de tronques ici */

    /* La plus ancienne restante est "ligne5" (les 5 premieres, ligne0..4,
     * ont ete evincees), la plus recente est "ligne{total-1}" -- lues via
     * l'index logique (debut + k) % CONSOLE_LIGNES_MAX. */
    for (int k = 0; k < CONSOLE_LIGNES_MAX; k++) {
        int index_ring = (lu.debut + k) % CONSOLE_LIGNES_MAX;
        snprintf(tampon, sizeof(tampon), "ligne%d", 5 + k);
        VERIFIER_TEXTE(lu.lignes[index_ring], tampon);
    }
}

/* --- troncature d'une ligne trop longue ------------------------------------
 * Une ligne >= CONSOLE_LIGNE_MAX doit etre TRONQUEE a CONSOLE_LIGNE_MAX - 1
 * caracteres utiles (jamais rejetee, contrairement aux noms de macro/fichier
 * de moonraker_rpc.c qui eux sont IGNORES si trop longs -- un scrollback
 * n'a pas la meme contrainte d'identite qu'un nom qui doit designer une
 * ressource precise). */

static void section_troncature_ligne(void)
{
    console_log_effacer();

    char longue[CONSOLE_LIGNE_MAX + 50];
    memset(longue, 'A', sizeof(longue) - 1);
    longue[sizeof(longue) - 1] = '\0';

    console_log_ajouter(longue);

    console_log_t lu;
    console_log_lire(&lu);
    VERIFIER(lu.nb == 1);
    VERIFIER(strlen(lu.lignes[0]) == CONSOLE_LIGNE_MAX - 1);
    for (size_t i = 0; i < strlen(lu.lignes[0]); i++) {
        VERIFIER(lu.lignes[0][i] == 'A');
    }
}

/* --- effacer ---------------------------------------------------------------
 * Vide le scrollback (nb=0, debut=0) et incremente generation. */

static void section_effacer(void)
{
    console_log_effacer();
    console_log_ajouter("une ligne");
    console_log_ajouter("une autre");

    console_log_t avant;
    console_log_lire(&avant);
    VERIFIER(avant.nb == 2);

    console_log_effacer();

    console_log_t apres;
    console_log_lire(&apres);
    VERIFIER(apres.nb == 0);
    VERIFIER(apres.debut == 0);
    VERIFIER(apres.generation == avant.generation + 1);

    /* effacer() sur un store deja vide : incremente quand meme generation
     * (ecriture reelle du store, meme si le contenu logique ne change pas --
     * meme discipline que power_devices_definir()). */
    console_log_effacer();
    console_log_t re_efface;
    console_log_lire(&re_efface);
    VERIFIER(re_efface.generation == apres.generation + 1);
}

/* --- console_log_lire(NULL) : no-op, ne doit pas planter ------------------ */

static void section_lire_null(void)
{
    console_log_lire(NULL);   /* ne doit pas planter */
    VERIFIER(true);
}

void suite_console_log(void)
{
    printf("suite : console_log (store scrollback console gcode)\n");
    section_ajouter_normal();
    section_eviction_fifo();
    section_troncature_ligne();
    section_effacer();
    section_lire_null();
}
