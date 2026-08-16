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
        float _obtenu = (obtenu);                                            \
        float _attendu = (attendu);                                          \
        float _ecart = _obtenu - _attendu;                                    \
        if (_ecart < 0) _ecart = -_ecart;                                    \
        /* Formuler la condition de SUCCES, pas celle d'echec : toute        \
         * comparaison impliquant un NaN est fausse, donc un NaN echoue      \
         * naturellement au lieu de passer inapercu. */                      \
        if (!(_ecart <= (tolerance))) {                                      \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s = %f, attendu %f\n",                   \
                   __FILE__, __LINE__, #obtenu, (double)_obtenu,             \
                   (double)_attendu);                                        \
        }                                                                     \
    } while (0)

#define VERIFIER_TEXTE(obtenu, attendu)                                       \
    do {                                                                      \
        tests_lances++;                                                       \
        const char *_obtenu = (obtenu);                                      \
        const char *_attendu = (attendu);                                    \
        if (strcmp(_obtenu, _attendu) != 0) {                                \
            tests_echoues++;                                                  \
            printf("  ECHEC %s:%d : %s = \"%s\", attendu \"%s\"\n",           \
                   __FILE__, __LINE__, #obtenu, _obtenu, _attendu);          \
        }                                                                     \
    } while (0)
