/* Implémentation de json_util.h -- voir ce header pour le contrat complet et
 * le POURQUOI (échappement JSON de la saisie libre de la console gcode). */
#include "json_util.h"

#include <stdio.h>

size_t json_echapper_chaine(char *dest, size_t dest_n, const char *src)
{
    /* `pos` : position d'écriture RÉELLE dans `dest`, jamais >= dest_n (garde
     * toujours 1 octet de réserve pour le '\0' final tant que `a_de_la_place`
     * est vrai). `ecrit` : compteur logique de tout ce qui AURAIT été écrit,
     * indépendant de la place réellement disponible -- c'est lui qui est
     * rendu à l'appelant, même convention que snprintf(). */
    size_t pos = 0;
    size_t ecrit = 0;
    const int a_de_la_place = (dest != NULL && dest_n > 0);
    /* Dès qu'UN groupe ne tient plus, plus RIEN n'est écrit ensuite -- `dest`
     * doit rester un PRÉFIXE valide de la sortie complète, même convention
     * que snprintf(). Sans cette garde, un groupe qui manque de place
     * pourrait être sauté silencieusement alors qu'un groupe plus PETIT plus
     * loin dans la chaîne (ex. un octet recopié tel quel, 1 seul octet)
     * continuerait à tenir dans la place restante -- le contenu écrit ne
     * serait alors plus le début de la sortie attendue, mais un mélange
     * avec un trou invisible au milieu (ex. un `\uXXXX` de contrôle disparu
     * sans laisser de trace, suivi des caractères normaux d'APRÈS lui). */
    int tronque = 0;

    if (src != NULL) {
        for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; p++) {
            unsigned char c = *p;
            /* `groupe`/`len` : le ou les octets de SORTIE produits par CET
             * octet source (1 pour un octet recopié tel quel, 2 pour `\"`/
             * `\\`, 6 pour `\uXXXX`), toujours écrits comme un BLOC ATOMIQUE
             * -- tout ou rien selon la place restante. Sans cette règle, un
             * tampon qui manque de place au milieu de `\"` laisserait un
             * antislash SEUL en fin de tampon, qui échapperait le guillemet
             * fermant ajouté par l'appelant et casserait tout le JSON en
             * aval au lieu de simplement tronquer proprement. */
            char groupe[7]; /* 6 caracteres utiles ("\uXXXX") + '\0' de snprintf() */
            size_t len;

            if (c == '"' || c == '\\') {
                groupe[0] = '\\';
                groupe[1] = (char)c;
                len = 2;
            } else if (c < 0x20) {
                int n = snprintf(groupe, sizeof(groupe), "\\u%04x", (unsigned)c);
                /* snprintf() rend toujours 6 ici (format fixe, %04x sur un
                 * octet) ; comparé à sizeof(groupe) par prudence, même
                 * discipline que le reste du dépôt (voir moonraker_rpc.c). */
                len = (n > 0 && (size_t)n < sizeof(groupe)) ? (size_t)n : 6;
            } else {
                groupe[0] = (char)c;
                len = 1;
            }

            if (!tronque && a_de_la_place && pos + len <= dest_n - 1) {
                for (size_t i = 0; i < len; i++) {
                    dest[pos++] = groupe[i];
                }
            } else {
                tronque = 1;
            }
            ecrit += len;
        }
    }

    if (a_de_la_place) {
        dest[pos] = '\0';
    }

    return ecrit;
}
