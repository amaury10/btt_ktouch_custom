#include <string.h>

#include "backend.h"
#include "hote_parse.h"
#include "petit_test.h"

void suite_hote_parse(void)
{
    printf("suite : analyseur d'hote (adresse:port)\n");

    /* Cas nominal : adresse et port normalement saisis. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("192.168.1.42:7125", &h) == true);
        VERIFIER_TEXTE(h.adresse, "192.168.1.42");
        VERIFIER(h.port == 7125);
    }

    /* Adresse IPv6 littérale : contient elle-même plusieurs ':' — le
     * découpage doit se faire sur le DERNIER, pas le premier. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("fe80::1:8080", &h) == true);
        VERIFIER_TEXTE(h.adresse, "fe80::1");
        VERIFIER(h.port == 8080);
    }

    /* Pas de ':' du tout : chaîne entièrement inexploitable, adresse et
     * port retombent tous les deux sur leurs valeurs par défaut. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("sansport", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* ':' en toute fin de chaîne : adresse valide, mais rien après le ':' à
     * parser comme port — l'adresse est conservée, seul le port retombe sur
     * sa valeur par défaut. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("moulinex.local:", &h) == true);
        VERIFIER_TEXTE(h.adresse, "moulinex.local");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* ':' en tout début de chaîne : adresse vide (donc hôte jugé non
     * exploitable, cohérent avec reglages_configures()), même si le port qui
     * suit est numériquement valide. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse(":1234", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == 1234);
    }

    /* Port 0 : hors de 1..65535, rejeté même si purement numérique. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("hote.local:0", &h) == true);
        VERIFIER_TEXTE(h.adresse, "hote.local");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Port 65536 : un de plus que le maximum représentable par un port
     * réseau (uint16_t inclus), rejeté. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("hote.local:65536", &h) == true);
        VERIFIER_TEXTE(h.adresse, "hote.local");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Port très largement hors limites (dépasse même unsigned long sur une
     * plateforme 32 bits) : doit être rejeté sans jamais faire confiance à
     * la valeur repliée par strtoul(). */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("hote.local:99999999999", &h) == true);
        VERIFIER_TEXTE(h.adresse, "hote.local");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Port non numérique. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("hote.local:abc", &h) == true);
        VERIFIER_TEXTE(h.adresse, "hote.local");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Adresse plus longue que ce que backend_hote_t peut stocker : la chaîne
     * entière est jugée inexploitable, pas seulement tronquée. */
    {
        char longue[BACKEND_HOTE_LONGUEUR_MAX + 32];
        memset(longue, 'x', sizeof(longue) - 1);
        longue[sizeof(longue) - 1] = '\0';
        char chaine[sizeof(longue) + 8];
        snprintf(chaine, sizeof(chaine), "%s:1234", longue);

        backend_hote_t h;
        VERIFIER(hote_parse(chaine, &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Chaîne vide : ni ':' ni adresse ni port, retombe entièrement sur les
     * valeurs par défaut. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Signe explicite devant le port : rejeté, jamais laissé à strtoul() qui
     * l'accepterait silencieusement (y compris un '-', par retournement
     * arithmétique vers une valeur positive géante). */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("hote.local:+80", &h) == true);
        VERIFIER_TEXTE(h.adresse, "hote.local");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }
}
