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

    /* Adresse IPv6 littérale SANS crochets : ambiguë -- le découpage sur le
     * DERNIER ':' donnerait "fe80::1", mais rien ne distingue alors ce cas de
     * "a:b:c" (adresse "a:b", tout aussi arbitraire). REJETÉE depuis la revue
     * de la tâche 8 (round 1) : avant ce correctif, ce cas était accepté et
     * produisait ensuite une URL Moonraker elle-même ambiguë
     * ("http://fe80::1:7125/...", voir backend_moonraker.c) -- non conforme à
     * RFC 3986 §3.2.2, qui exige la forme "[adresse]" pour tout hôte IPv6
     * littéral dans une URI. La forme entre crochets ci-dessous est
     * désormais la SEULE façon exploitable de saisir une adresse IPv6. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("fe80::1:8080", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Même règle, cas dégénéré à trois ':' : la chaîne entière est jugée
     * inexploitable, pas seulement la première paire — même diagnostic que
     * ci-dessus (candidat d'adresse "::" ou "a:b", qui contient lui-même un
     * ':'). */
    {
        backend_hote_t h;
        VERIFIER(hote_parse(":::", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }
    {
        backend_hote_t h;
        VERIFIER(hote_parse("a:b:c", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Adresse IPv6 entre crochets (RFC 3986 §3.2.2) : la SEULE forme
     * exploitable pour une adresse qui contient elle-même des ':' --
     * crochets retirés pour le stockage (voir backend.h : adresse "sans
     * schéma", pas "sans crochets" à la lettre, mais c'est la convention
     * retenue ici pour que backend_moonraker.c n'ait qu'à réencadrer au
     * moment de construire l'URL, jamais à décider où). */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("[fe80::1]:8080", &h) == true);
        VERIFIER_TEXTE(h.adresse, "fe80::1");
        VERIFIER(h.port == 8080);
    }

    /* Crochets sans port : port par défaut, même règle que "moulinex.local:"
     * plus bas pour la forme sans crochets. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("[fe80::1]", &h) == true);
        VERIFIER_TEXTE(h.adresse, "fe80::1");
        VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);
    }

    /* Crochet ouvrant jamais refermé, ou contenu vide entre crochets : chaîne
     * jugée inexploitable dans les deux cas, jamais une adresse tronquée ou
     * vide silencieusement acceptée. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("[fe80::1", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
    }
    {
        backend_hote_t h;
        VERIFIER(hote_parse("[]:80", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
    }

    /* Préfixe de schéma ("http://", ou n'importe quel "://") : rejeté
     * d'emblée, avant même toute tentative de découpage sur ':' -- sans quoi
     * une valeur collée depuis un navigateur ("http://192.168.1.50:7125")
     * serait acceptée avec pour adresse "http", puis reconstruite en
     * "http://http://..." par backend_moonraker.c (revue tâche 8, round 1). */
    {
        backend_hote_t h;
        VERIFIER(hote_parse("http://host:7125", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
    }
    {
        backend_hote_t h;
        VERIFIER(hote_parse("http://192.168.1.50:7125", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
    }

    /* Espace de tête ou de fin : REJETÉ, jamais tronqué silencieusement --
     * même politique que le brief de la tâche 8 l'exige pour la troncature :
     * une saisie qui contient un défaut visible doit être visiblement
     * refusée, pas discrètement corrigée à la place de l'utilisateur. */
    {
        backend_hote_t h;
        VERIFIER(hote_parse(" 192.168.1.50:7125", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
    }
    {
        backend_hote_t h;
        VERIFIER(hote_parse("192.168.1.50:7125 ", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
    }
    {
        backend_hote_t h;
        VERIFIER(hote_parse(" 192.168.1.50 ", &h) == false);
        VERIFIER_TEXTE(h.adresse, "");
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
