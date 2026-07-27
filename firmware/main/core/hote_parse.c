#include "hote_parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define HOTE_PARSE_PORT_MAX 65535u

bool hote_parse(const char *chaine, backend_hote_t *sortie)
{
    const char *separateur = strrchr(chaine, ':');
    if (separateur == NULL) {
        sortie->adresse[0] = '\0';
        sortie->port = HOTE_PARSE_PORT_DEFAUT;
        return false;
    }

    size_t longueur_adresse = (size_t)(separateur - chaine);
    if (longueur_adresse >= sizeof(sortie->adresse)) {
        sortie->adresse[0] = '\0';
        sortie->port = HOTE_PARSE_PORT_DEFAUT;
        return false;
    }

    memcpy(sortie->adresse, chaine, longueur_adresse);
    sortie->adresse[longueur_adresse] = '\0';

    /* isdigit() sur le premier caractère écarte d'un coup l'absence de
     * chiffre, un signe ('+'/'-') et les espaces de tête que strtoul()
     * tolérerait sinon : les ports que nous écrivons nous-mêmes sont
     * toujours sérialisés avec "%u", donc une chaîne valide ne contient
     * jamais autre chose que des chiffres purs après le ':'. */
    const char *chiffres = separateur + 1;
    char *fin = NULL;
    unsigned long port = 0;
    bool port_valide = false;
    if (isdigit((unsigned char)*chiffres)) {
        port = strtoul(chiffres, &fin, 10);
        port_valide = (fin != NULL && *fin == '\0' && port >= 1 && port <= HOTE_PARSE_PORT_MAX);
    }
    sortie->port = port_valide ? (uint16_t)port : HOTE_PARSE_PORT_DEFAUT;

    return sortie->adresse[0] != '\0';
}
