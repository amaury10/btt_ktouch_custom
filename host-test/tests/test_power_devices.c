#include <string.h>

#include "moonraker_rpc.h"
#include "petit_test.h"
#include "power_devices.h"

/* --- store : power_devices_definir / power_devices_lire ------------------ */

static void section_store_definir_lire(void)
{
    /* round-trip nominal : ce qu'on ecrit est ce qu'on relit (sauf
     * `generation`, geree par le store lui-meme -- voir plus bas). */
    power_devices_t src;
    memset(&src, 0, sizeof(src));
    strcpy(src.devices[0].nom, "printer");
    src.devices[0].allumee = true;
    src.devices[0].connue = true;
    strcpy(src.devices[1].nom, "led_strip");
    src.devices[1].allumee = false;
    src.devices[1].connue = true;
    src.nb = 2;
    src.tronque = false;

    power_devices_definir(&src);

    power_devices_t lu;
    memset(&lu, 0, sizeof(lu));
    power_devices_lire(&lu);
    VERIFIER(lu.nb == 2);
    VERIFIER_TEXTE(lu.devices[0].nom, "printer");
    VERIFIER(lu.devices[0].allumee == true);
    VERIFIER(lu.devices[0].connue == true);
    VERIFIER_TEXTE(lu.devices[1].nom, "led_strip");
    VERIFIER(lu.devices[1].allumee == false);
    VERIFIER(!lu.tronque);

    /* remplace ENTIEREMENT le contenu precedent (instantane complet, pas une
     * fusion) -- meme contrat que klipper_fichiers_definir(). */
    power_devices_t src2;
    memset(&src2, 0, sizeof(src2));
    strcpy(src2.devices[0].nom, "seule_prise");
    src2.devices[0].allumee = true;
    src2.devices[0].connue = true;
    src2.nb = 1;
    power_devices_definir(&src2);

    memset(&lu, 0, sizeof(lu));
    power_devices_lire(&lu);
    VERIFIER(lu.nb == 1);
    VERIFIER_TEXTE(lu.devices[0].nom, "seule_prise");

    /* troncature : simple roundtrip du drapeau, la LOGIQUE de troncature
     * elle-meme est testee cote parseur (section_lire_power_devices
     * ci-dessous) -- ici on verifie juste que le store ne perd pas
     * l'information au passage. */
    power_devices_t src3;
    memset(&src3, 0, sizeof(src3));
    src3.nb = POWER_DEVICES_MAX;
    src3.tronque = true;
    power_devices_definir(&src3);

    memset(&lu, 0, sizeof(lu));
    power_devices_lire(&lu);
    VERIFIER(lu.nb == POWER_DEVICES_MAX);
    VERIFIER(lu.tronque);

    /* NULL = no-op (definir) */
    power_devices_definir(NULL);
    power_devices_t apres_null;
    memset(&apres_null, 0, sizeof(apres_null));
    power_devices_lire(&apres_null);
    VERIFIER(apres_null.nb == POWER_DEVICES_MAX);   /* inchange par rapport a src3 */

    /* NULL = no-op (lire) -- ne doit pas planter */
    power_devices_lire(NULL);
}

/* --- store : generation ---------------------------------------------------
 * Compteur monotone a travers les appels, pour que l'UI detecte "il y a du
 * neuf" sans comparer le contenu champ par champ (voir power_devices.h). */

static void section_store_generation(void)
{
    power_devices_t src;
    memset(&src, 0, sizeof(src));
    strcpy(src.devices[0].nom, "prise_a");
    src.nb = 1;

    power_devices_definir(&src);
    power_devices_t g1;
    power_devices_lire(&g1);

    /* definir() incremente TOUJOURS, meme si `*src` porte lui-meme une
     * `generation` non nulle -- le store ignore celle de l'appelant et fait
     * progresser la sienne (voir le commentaire de power_devices_definir()). */
    src.generation = 999;
    power_devices_definir(&src);
    power_devices_t g2;
    power_devices_lire(&g2);
    VERIFIER(g2.generation == g1.generation + 1);

    /* maj_un() sur une prise CONNUE incremente aussi */
    power_devices_maj_un("prise_a", true);
    power_devices_t g3;
    power_devices_lire(&g3);
    VERIFIER(g3.generation == g2.generation + 1);
    VERIFIER(g3.devices[0].allumee == true);
    VERIFIER(g3.devices[0].connue == true);

    /* maj_un() sur une prise INCONNUE (nom absent du store) : no-op complet,
     * generation INCHANGEE -- rien n'a reellement change dans le store. */
    power_devices_maj_un("prise_fantome", true);
    power_devices_t g4;
    power_devices_lire(&g4);
    VERIFIER(g4.generation == g3.generation);
    VERIFIER(g4.nb == g3.nb);   /* aucune entree creee */
}

/* --- store : power_devices_maj_un ----------------------------------------- */

static void section_store_maj_un(void)
{
    power_devices_t src;
    memset(&src, 0, sizeof(src));
    strcpy(src.devices[0].nom, "printer");
    src.devices[0].allumee = false;
    src.devices[0].connue = false;
    strcpy(src.devices[1].nom, "led_strip");
    src.devices[1].allumee = true;
    src.devices[1].connue = true;
    src.nb = 2;
    power_devices_definir(&src);

    /* bascule ciblée d'une seule prise, l'autre INTACTE */
    power_devices_maj_un("printer", true);
    power_devices_t lu;
    power_devices_lire(&lu);
    VERIFIER(lu.devices[0].allumee == true);
    VERIFIER(lu.devices[0].connue == true);
    VERIFIER(lu.devices[1].allumee == true);   /* inchange (etait deja true) */
    VERIFIER_TEXTE(lu.devices[1].nom, "led_strip");
    VERIFIER(lu.nb == 2);   /* jamais de creation/suppression d'entree */

    power_devices_maj_un("led_strip", false);
    power_devices_lire(&lu);
    VERIFIER(lu.devices[1].allumee == false);
    VERIFIER(lu.devices[0].allumee == true);   /* toujours intact */

    /* nom NULL : no-op, ne doit pas planter */
    power_devices_maj_un(NULL, true);
    power_devices_t apres_nul;
    power_devices_lire(&apres_nul);
    VERIFIER(apres_nul.devices[0].allumee == true);
    VERIFIER(apres_nul.devices[1].allumee == false);
}

/* --- rpc_lire_power_devices ------------------------------------------------
 * Meme structure que section_lire_fichiers() (test_moonraker_rpc.c) :
 * reponse a machine.device_power.devices, result.devices = tableau
 * d'OBJETS {"device":"nom","status":"on|off"}. */

static void section_lire_power_devices(void)
{
    /* liste vide => nb == 0, true */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        p.nb = 7;   /* valeur temoin, doit etre remise a 0 */
        static const char *VIDE = "{\"result\":{\"devices\":[]},\"id\":1}";
        VERIFIER(rpc_lire_power_devices(&p, VIDE, strlen(VIDE)));
        VERIFIER(p.nb == 0);
        VERIFIER(!p.tronque);
    }

    /* une seule prise, allumee */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        static const char *UNE =
            "{\"result\":{\"devices\":["
            "{\"device\":\"printer\",\"status\":\"on\",\"type\":\"gpio\"}"
            "]},\"id\":1}";
        VERIFIER(rpc_lire_power_devices(&p, UNE, strlen(UNE)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "printer");
        VERIFIER(p.devices[0].allumee == true);
        VERIFIER(p.devices[0].connue == true);
    }

    /* N prises, on/off melanges */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        static const char *MELANGE =
            "{\"result\":{\"devices\":["
            "{\"device\":\"printer\",\"status\":\"on\"},"
            "{\"device\":\"led_strip\",\"status\":\"off\"},"
            "{\"device\":\"enclosure_fan\",\"status\":\"on\"}"
            "]},\"id\":1}";
        VERIFIER(rpc_lire_power_devices(&p, MELANGE, strlen(MELANGE)));
        VERIFIER(p.nb == 3);
        VERIFIER_TEXTE(p.devices[0].nom, "printer");
        VERIFIER(p.devices[0].allumee == true);
        VERIFIER_TEXTE(p.devices[1].nom, "led_strip");
        VERIFIER(p.devices[1].allumee == false);
        VERIFIER(p.devices[1].connue == true);   /* connue meme eteinte */
        VERIFIER_TEXTE(p.devices[2].nom, "enclosure_fan");
        VERIFIER(p.devices[2].allumee == true);
        VERIFIER(!p.tronque);
    }

    /* forme "devices" directement a la racine (sans enveloppe result) --
     * voir le commentaire de rpc_lire_power_devices dans moonraker_rpc.h */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        static const char *SANS_RESULT =
            "{\"devices\":[{\"device\":\"printer\",\"status\":\"on\"}]}";
        VERIFIER(rpc_lire_power_devices(&p, SANS_RESULT, strlen(SANS_RESULT)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "printer");
        VERIFIER(p.devices[0].allumee == true);
    }

    /* > POWER_DEVICES_MAX entrees => nb == MAX, tronque == true */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));

        char liste[2048];
        size_t pos = (size_t)snprintf(liste, sizeof(liste), "{\"result\":{\"devices\":[");
        for (int i = 0; i < POWER_DEVICES_MAX + 5; i++) {
            pos += (size_t)snprintf(liste + pos, sizeof(liste) - pos,
                                     "%s{\"device\":\"d%d\",\"status\":\"on\"}", i == 0 ? "" : ",", i);
        }
        pos += (size_t)snprintf(liste + pos, sizeof(liste) - pos, "]},\"id\":1}");
        VERIFIER(pos < sizeof(liste));

        VERIFIER(rpc_lire_power_devices(&p, liste, pos));
        VERIFIER(p.nb == POWER_DEVICES_MAX);
        VERIFIER(p.tronque);
    }

    /* .device de POWER_NOM_MAX caracteres ou plus : IGNORE (pas tronque),
     * ne compte pas dans nb, le reste de la liste continue -- meme piege que
     * rpc_lire_fichiers sur les chemins trop longs */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        char nom_long[64];
        memset(nom_long, 'A', 40);   /* >= POWER_NOM_MAX (32) */
        nom_long[40] = '\0';

        char liste[256];
        snprintf(liste, sizeof(liste),
                 "{\"result\":{\"devices\":["
                 "{\"device\":\"%s\",\"status\":\"on\"},"
                 "{\"device\":\"courte\",\"status\":\"on\"}"
                 "]}}", nom_long);
        VERIFIER(rpc_lire_power_devices(&p, liste, strlen(liste)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "courte");
        VERIFIER(!p.tronque);
    }

    /* element du tableau qui n'est pas un objet, ou objet sans .device
     * chaine : ignore, le reste continue */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        static const char *POISON =
            "{\"result\":{\"devices\":["
            "42,"
            "\"pas un objet\","
            "{\"status\":\"on\"},"
            "{\"device\":123,\"status\":\"on\"},"
            "{\"device\":\"valide\",\"status\":\"on\"}"
            "]}}";
        VERIFIER(rpc_lire_power_devices(&p, POISON, strlen(POISON)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "valide");
    }

    /* remplace ENTIEREMENT la liste precedente (instantane complet) */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        p.nb = 3;
        strcpy(p.devices[0].nom, "vieille_prise");

        static const char *NEUVE = "{\"result\":{\"devices\":[{\"device\":\"neuve\",\"status\":\"on\"}]}}";
        VERIFIER(rpc_lire_power_devices(&p, NEUVE, strlen(NEUVE)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "neuve");
    }

    /* entrees invalides => false, dest INTACT (temoin 0x5A, memcmp du
     * struct ENTIER) */
    {
        power_devices_t temoin;
        memset(&temoin, 0x5A, sizeof(temoin));
        power_devices_t p = temoin;
        VERIFIER(!rpc_lire_power_devices(&p, "pas du json", 11));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        VERIFIER(!rpc_lire_power_devices(&p, "", 0));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        VERIFIER(!rpc_lire_power_devices(&p, NULL, 0));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        static const char *NI_RESULT_NI_DEVICES = "{\"id\":1}";
        VERIFIER(!rpc_lire_power_devices(&p, NI_RESULT_NI_DEVICES, strlen(NI_RESULT_NI_DEVICES)));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        static const char *DEVICES_PAS_TABLEAU = "{\"result\":{\"devices\":{\"a\":1}}}";
        VERIFIER(!rpc_lire_power_devices(&p, DEVICES_PAS_TABLEAU, strlen(DEVICES_PAS_TABLEAU)));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        VERIFIER(!rpc_lire_power_devices(NULL, "{}", 2));
    }
}

/* --- rpc_lire_power_changed ------------------------------------------------
 * notify_power_changed : params[0] est lui-meme un TABLEAU de prises
 * changees -- piege distinct de rpc_fusionner_status (params[0] objet). */

static void section_lire_power_changed(void)
{
    /* une seule prise changee (cas reel Moonraker le plus courant) */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        static const char *UNE =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_power_changed\","
            "\"params\":[[{\"device\":\"printer\",\"status\":\"on\"}]]}";
        VERIFIER(rpc_lire_power_changed(&p, UNE, strlen(UNE)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "printer");
        VERIFIER(p.devices[0].allumee == true);
        VERIFIER(p.devices[0].connue == true);
    }

    /* N prises changees dans la meme notification */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        static const char *PLUSIEURS =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_power_changed\","
            "\"params\":[["
            "{\"device\":\"printer\",\"status\":\"off\"},"
            "{\"device\":\"led_strip\",\"status\":\"on\"}"
            "]]}";
        VERIFIER(rpc_lire_power_changed(&p, PLUSIEURS, strlen(PLUSIEURS)));
        VERIFIER(p.nb == 2);
        VERIFIER_TEXTE(p.devices[0].nom, "printer");
        VERIFIER(p.devices[0].allumee == false);
        VERIFIER(p.devices[0].connue == true);
        VERIFIER_TEXTE(p.devices[1].nom, "led_strip");
        VERIFIER(p.devices[1].allumee == true);
    }

    /* remplace ENTIEREMENT dest (instantane des SEULES prises changees, pas
     * une fusion avec ce qui s'y trouvait avant) */
    {
        power_devices_t p;
        memset(&p, 0, sizeof(p));
        p.nb = 3;
        strcpy(p.devices[0].nom, "reste_du_bruit");

        static const char *UNE =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_power_changed\","
            "\"params\":[[{\"device\":\"printer\",\"status\":\"on\"}]]}";
        VERIFIER(rpc_lire_power_changed(&p, UNE, strlen(UNE)));
        VERIFIER(p.nb == 1);
        VERIFIER_TEXTE(p.devices[0].nom, "printer");
    }

    /* entrees invalides => false, dest INTACT */
    {
        power_devices_t temoin;
        memset(&temoin, 0x5A, sizeof(temoin));
        power_devices_t p = temoin;

        static const char *SANS_PARAMS =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_power_changed\"}";
        VERIFIER(!rpc_lire_power_changed(&p, SANS_PARAMS, strlen(SANS_PARAMS)));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        static const char *PARAMS_VIDE =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_power_changed\",\"params\":[]}";
        VERIFIER(!rpc_lire_power_changed(&p, PARAMS_VIDE, strlen(PARAMS_VIDE)));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        static const char *PARAMS0_PAS_TABLEAU =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_power_changed\","
            "\"params\":[{\"device\":\"printer\",\"status\":\"on\"}]}";
        VERIFIER(!rpc_lire_power_changed(&p, PARAMS0_PAS_TABLEAU, strlen(PARAMS0_PAS_TABLEAU)));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        VERIFIER(!rpc_lire_power_changed(&p, "pas du json", 11));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        VERIFIER(!rpc_lire_power_changed(&p, "", 0));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        p = temoin;
        VERIFIER(!rpc_lire_power_changed(&p, NULL, 0));
        VERIFIER(memcmp(&p, &temoin, sizeof(p)) == 0);

        VERIFIER(!rpc_lire_power_changed(NULL, "{}", 2));
    }
}

void suite_power_devices(void)
{
    printf("suite : prises Moonraker (store + parseurs)\n");
    section_store_definir_lire();
    section_store_generation();
    section_store_maj_un();
    section_lire_power_devices();
    section_lire_power_changed();
}
