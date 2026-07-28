#include "hote_parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define HOTE_PARSE_PORT_MAX 65535u

/* Remet `sortie` à son état "chaîne inexploitable" -- un seul site pour les
 * différentes raisons de rejet (voir hote_parse.h), pour ne jamais laisser
 * une adresse partiellement construite sur un chemin de refus. */
static void hote_parse_inexploitable(backend_hote_t *sortie)
{
    sortie->adresse[0] = '\0';
    sortie->port = HOTE_PARSE_PORT_DEFAUT;
}

/* Analyse le port en base 10 à partir de `chiffres` (ce qui suit le ':' de
 * séparation, ou une chaîne vide s'il n'y avait rien après). N'affecte
 * JAMAIS l'adresse : un port absent ou invalide retombe seul sur
 * HOTE_PARSE_PORT_DEFAUT (voir hote_parse.h), la chaîne entière n'est pas
 * rejetée pour autant. Factorisé ici : la forme entre crochets et la forme
 * classique ci-dessous partagent EXACTEMENT cette même règle, jamais une
 * copie qui pourrait diverger. */
static uint16_t hote_parse_port(const char *chiffres)
{
    /* isdigit() sur le premier caractère écarte d'un coup l'absence de
     * chiffre, un signe ('+'/'-') et les espaces de tête que strtoul()
     * tolérerait sinon : les ports que nous écrivons nous-mêmes sont
     * toujours sérialisés avec "%u", donc une chaîne valide ne contient
     * jamais autre chose que des chiffres purs après le ':'. */
    if (!isdigit((unsigned char)*chiffres)) {
        return HOTE_PARSE_PORT_DEFAUT;
    }
    char *fin = NULL;
    unsigned long port = strtoul(chiffres, &fin, 10);
    bool valide = (fin != NULL && *fin == '\0' && port >= 1 && port <= HOTE_PARSE_PORT_MAX);
    return valide ? (uint16_t)port : HOTE_PARSE_PORT_DEFAUT;
}

/* Forme "[adresse]" ou "[adresse]:port" (RFC 3986 §3.2.2). `chaine[0]` vaut
 * déjà '[' chez l'appelant. Rend faux (chaîne inexploitable) si le crochet
 * n'est jamais refermé, si le contenu entre crochets est vide ou trop long,
 * ou si ce qui suit ']' n'est ni vide ni ":<chiffres...>". */
static bool hote_parse_entre_crochets(const char *chaine, backend_hote_t *sortie)
{
    const char *fermante = strchr(chaine, ']');
    if (fermante == NULL) {
        hote_parse_inexploitable(sortie);
        return false;
    }

    size_t longueur_adresse = (size_t)(fermante - chaine - 1);
    if (longueur_adresse == 0 || longueur_adresse >= sizeof(sortie->adresse)) {
        hote_parse_inexploitable(sortie);
        return false;
    }

    const char *apres = fermante + 1;
    if (*apres != '\0' && *apres != ':') {
        /* Garbage après le crochet fermant ("[fe80::1]x") : la chaîne
         * entière est jugée inexploitable plutôt que d'ignorer ce reliquat
         * en silence. */
        hote_parse_inexploitable(sortie);
        return false;
    }

    memcpy(sortie->adresse, chaine + 1, longueur_adresse);
    sortie->adresse[longueur_adresse] = '\0';
    sortie->port = hote_parse_port(*apres == ':' ? apres + 1 : "");
    return true; /* longueur_adresse > 0 garanti ci-dessus : adresse jamais vide ici */
}

/* Forme "adresse:port" classique, découpée sur le DERNIER ':' de la chaîne.
 * Rend faux si aucun ':' n'est trouvé, si l'adresse ne tient pas dans
 * sortie->adresse, ou si l'adresse ainsi obtenue contient elle-même un ':'
 * (ambiguïté IPv6 non encadrée, voir hote_parse.h -- c'est cette dernière
 * règle qui distingue ce correctif de l'implémentation d'origine). */
static bool hote_parse_sans_crochets(const char *chaine, backend_hote_t *sortie)
{
    const char *separateur = strrchr(chaine, ':');
    if (separateur == NULL) {
        hote_parse_inexploitable(sortie);
        return false;
    }

    size_t longueur_adresse = (size_t)(separateur - chaine);
    if (longueur_adresse >= sizeof(sortie->adresse)) {
        hote_parse_inexploitable(sortie);
        return false;
    }
    if (memchr(chaine, ':', longueur_adresse) != NULL) {
        /* La partie adresse contient elle-même un ':' : "fe80::1:8080"
         * (adresse candidate "fe80::1") ou "a:b:c" (candidate "a:b") sont
         * indiscernables l'un de l'autre sans plus d'information -- rejetés
         * tous les deux, la forme entre crochets est la seule issue pour une
         * adresse qui a réellement besoin d'un ':'. */
        hote_parse_inexploitable(sortie);
        return false;
    }

    memcpy(sortie->adresse, chaine, longueur_adresse);
    sortie->adresse[longueur_adresse] = '\0';
    sortie->port = hote_parse_port(separateur + 1);

    return sortie->adresse[0] != '\0';
}

bool hote_parse(const char *chaine, backend_hote_t *sortie)
{
    size_t longueur = strlen(chaine);

    /* Espace de tête ou de fin : rejeté, jamais tronqué (voir hote_parse.h).
     * `longueur > 0` d'abord : sur une chaîne vide, chaine[0] == '\0', qui
     * n'est jamais un espace -- la garde de longueur reste plus sûre à lire
     * que de s'y fier implicitement. */
    if (longueur > 0 && (isspace((unsigned char)chaine[0]) || isspace((unsigned char)chaine[longueur - 1]))) {
        hote_parse_inexploitable(sortie);
        return false;
    }

    /* Préfixe de schéma ("http://", "ws://", n'importe lequel) : rejeté
     * d'emblée, avant toute tentative de découpage -- sans quoi l'adresse
     * obtenue serait "http" et backend_moonraker.c produirait ensuite
     * "http://http://...". */
    if (strstr(chaine, "://") != NULL) {
        hote_parse_inexploitable(sortie);
        return false;
    }

    if (chaine[0] == '[') {
        return hote_parse_entre_crochets(chaine, sortie);
    }
    return hote_parse_sans_crochets(chaine, sortie);
}
