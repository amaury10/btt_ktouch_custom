#include <stdio.h>
#include <string.h>

#include "json_util.h"
#include "petit_test.h"

/* --- guillemets / backslash ------------------------------------------------ */

static void section_guillemets_backslash(void)
{
    char dest[64];

    size_t n = json_echapper_chaine(dest, sizeof(dest), "dit \"bonjour\"");
    VERIFIER_TEXTE(dest, "dit \\\"bonjour\\\"");
    VERIFIER(n == strlen(dest));

    n = json_echapper_chaine(dest, sizeof(dest), "C:\\chemin\\vers\\fichier");
    VERIFIER_TEXTE(dest, "C:\\\\chemin\\\\vers\\\\fichier");
    VERIFIER(n == strlen(dest));

    /* texte sans rien a echapper : recopie telle quelle */
    n = json_echapper_chaine(dest, sizeof(dest), "G1 X10 Y20 F3000");
    VERIFIER_TEXTE(dest, "G1 X10 Y20 F3000");
    VERIFIER(n == strlen("G1 X10 Y20 F3000"));
}

/* --- caracteres de controle -> \uXXXX -------------------------------------- */

static void section_controle(void)
{
    char dest[64];

    /* tabulation (0x09), saut de ligne (0x0A), retour chariot (0x0D) */
    size_t n = json_echapper_chaine(dest, sizeof(dest), "a\tb\nc\rd");
    VERIFIER_TEXTE(dest, "a\\u0009b\\u000ac\\u000dd");
    VERIFIER(n == strlen(dest));

    /* octet de controle le plus bas (0x00 milieu de chaine n'est pas
     * atteignable via une C-string classique -- 0x01 et 0x1f, les bornes
     * pratiques du domaine < 0x20) */
    char src[3] = { '\x01', '\x1f', '\0' };
    n = json_echapper_chaine(dest, sizeof(dest), src);
    VERIFIER_TEXTE(dest, "\\u0001\\u001f");
    VERIFIER(n == strlen(dest));

    /* 0x20 (espace) n'est PAS un caractere de controle : recopie tel quel */
    n = json_echapper_chaine(dest, sizeof(dest), "a b");
    VERIFIER_TEXTE(dest, "a b");
    VERIFIER(n == 3);
}

/* --- troncature -------------------------------------------------------------
 * Contrat snprintf() : le retour est TOUJOURS le nombre d'octets qui
 * auraient ete ecrits (hors '\0') si dest avait ete assez grand, meme quand
 * dest est trop petit -- l'appelant compare le retour a dest_n pour
 * detecter la troncature (voir json_util.h). Chaque groupe de sortie
 * (2 octets pour \" / \\, 6 pour \uXXXX) est ecrit ATOMIQUEMENT : jamais de
 * sequence d'echappement coupee en deux. */

static void section_troncature(void)
{
    /* texte simple sans echappement : troncature classique, comme snprintf */
    {
        char petit[4];   /* 3 octets utiles + '\0' */
        size_t n = json_echapper_chaine(petit, sizeof(petit), "abcdef");
        VERIFIER(n == 6);           /* longueur reelle, pas celle du tampon */
        VERIFIER(n >= sizeof(petit));   /* signale la troncature */
        VERIFIER_TEXTE(petit, "abc");
    }

    /* le tampon manque de place PENDANT un \" : le groupe entier (2 octets)
     * est omis plutot que d'ecrire un antislash seul en fin de tampon (ce
     * qui echapperait le guillemet fermant ajoute par l'appelant et
     * casserait tout le JSON en aval). */
    {
        char petit[3];   /* 2 octets utiles + '\0' : "ab" tient, pas le \" qui suit */
        size_t n = json_echapper_chaine(petit, sizeof(petit), "ab\"cd");
        VERIFIER(n == 2 + 2 + 2);   /* "ab" (2) + \" (2, comptes meme si non ecrits) + "cd" (2) */
        VERIFIER_TEXTE(petit, "ab");   /* PAS "ab\" (antislash orphelin) */
        VERIFIER(strlen(petit) == 2);
    }

    /* meme regle pour un groupe \uXXXX (6 octets) : omis en bloc si la place
     * manque, jamais coupe au milieu. */
    {
        char petit[5];   /* 4 octets utiles : "a" + 3 des 6 octets de \u0009 */
        size_t n = json_echapper_chaine(petit, sizeof(petit), "a\tbc");
        VERIFIER(n == 1 + 6 + 2);   /* 'a' + \u0009 (6) + "bc" (2) */
        VERIFIER_TEXTE(petit, "a");   /* le \u0009 entier est omis, pas coupe */
    }

    /* dest NULL ou dest_n == 0 : rien n'est ecrit (pas de crash), mais la
     * longueur necessaire est quand meme calculee -- meme usage que
     * snprintf(NULL, 0, ...). */
    {
        size_t n = json_echapper_chaine(NULL, 0, "abc\"def");
        VERIFIER(n == strlen("abc") + 2 + strlen("def"));

        char dest[64];
        n = json_echapper_chaine(dest, 0, "abc");
        VERIFIER(n == 3);
    }
}

/* --- src NULL : traite comme une chaine vide ------------------------------- */

static void section_src_null(void)
{
    char dest[8];
    strcpy(dest, "bruit");
    size_t n = json_echapper_chaine(dest, sizeof(dest), NULL);
    VERIFIER(n == 0);
    VERIFIER_TEXTE(dest, "");
}

void suite_json_util(void)
{
    printf("suite : json_util (echappement JSON de la saisie console)\n");
    section_guillemets_backslash();
    section_controle();
    section_troncature();
    section_src_null();
}
