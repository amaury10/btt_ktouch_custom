/* Micro-cadre de test : suffisant pour des fonctions pures, et sans dépendance
 * externe — le harnais doit rester trivial à faire fonctionner chez un
 * contributeur qui découvre le dépôt. */
#pragma once

#include <stdio.h>
#include <string.h>

extern int tests_echoues;
extern int tests_lances;

#define VERIFIER(condition)                                                   \
    do {                                                                      \
        tests_lances++;                                                       \
        if (!(condition)) {                                                   \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                     \
    } while (0)

#define VERIFIER_FLOAT(obtenu, attendu, tolerance)                            \
    do {                                                                      \
        tests_lances++;                                                       \
        float _d = (obtenu) - (attendu);                                      \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tolerance)) {                                               \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s = %f, attendu %f\n",                   \
                   __FILE__, __LINE__, #obtenu, (double)(obtenu),             \
                   (double)(attendu));                                        \
        }                                                                     \
    } while (0)

#define VERIFIER_TEXTE(obtenu, attendu)                                       \
    do {                                                                      \
        tests_lances++;                                                       \
        if (strcmp((obtenu), (attendu)) != 0) {                               \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s = \"%s\", attendu \"%s\"\n",           \
                   __FILE__, __LINE__, #obtenu, (obtenu), (attendu));         \
        }                                                                     \
    } while (0)
