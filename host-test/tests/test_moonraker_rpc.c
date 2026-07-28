#include <string.h>

#include "moonraker_rpc.h"
#include "petit_test.h"

/* --- rpc_construire_requete --------------------------------------------- */

static void section_construire_requete(void)
{
    char tampon[256];

    /* forme exacte avec params */
    VERIFIER(rpc_construire_requete(tampon, sizeof(tampon), 7, "printer.gcode.script",
                                     "{\"script\":\"G28\"}"));
    VERIFIER_TEXTE(tampon,
        "{\"jsonrpc\":\"2.0\",\"method\":\"printer.gcode.script\","
        "\"params\":{\"script\":\"G28\"},\"id\":7}");

    /* params NULL => cle absente entierement (pas de "params":null) */
    VERIFIER(rpc_construire_requete(tampon, sizeof(tampon), 42, "printer.emergency_stop", NULL));
    VERIFIER_TEXTE(tampon, "{\"jsonrpc\":\"2.0\",\"method\":\"printer.emergency_stop\",\"id\":42}");

    /* tampon trop court => false, jamais de troncature rendue comme un succes */
    char petit[10];
    VERIFIER(!rpc_construire_requete(petit, sizeof(petit), 1, "printer.gcode.script", "{}"));

    /* tampon de taille 0 / sortie NULL : ne doit pas planter */
    VERIFIER(!rpc_construire_requete(tampon, 0, 1, "m", NULL));
    VERIFIER(!rpc_construire_requete(NULL, sizeof(tampon), 1, "m", NULL));
    VERIFIER(!rpc_construire_requete(tampon, sizeof(tampon), 1, NULL, NULL));
}

/* --- rpc_construire_abonnement ------------------------------------------ */

static void section_construire_abonnement(void)
{
    char tampon[512];
    VERIFIER(rpc_construire_abonnement(tampon, sizeof(tampon), 3));
    VERIFIER(strstr(tampon, "\"method\":\"printer.objects/subscribe\"") != NULL);
    VERIFIER(strstr(tampon, "\"id\":3") != NULL);
    VERIFIER(strstr(tampon, "\"toolhead\":null") != NULL);
    VERIFIER(strstr(tampon, "\"gcode_move\":null") != NULL);
    VERIFIER(strstr(tampon, "\"extruder\":null") != NULL);
    VERIFIER(strstr(tampon, "\"extruder7\":null") != NULL);
    VERIFIER(strstr(tampon, "\"heater_bed\":null") != NULL);
    VERIFIER(strstr(tampon, "\"fan\":null") != NULL);
    VERIFIER(strstr(tampon, "\"print_stats\":null") != NULL);
    VERIFIER(strstr(tampon, "\"virtual_sdcard\":null") != NULL);
    VERIFIER(strstr(tampon, "\"webhooks\":null") != NULL);

    /* tampon trop court => false */
    char petit[10];
    VERIFIER(!rpc_construire_abonnement(petit, sizeof(petit), 3));
}

/* --- rpc_classifier ------------------------------------------------------ */

static void section_classifier(void)
{
    uint32_t id;

    id = 999;
    VERIFIER(rpc_classifier("{\"result\":{},\"id\":5}", strlen("{\"result\":{},\"id\":5}"), &id)
             == RPC_MSG_REPONSE);
    VERIFIER(id == 5);

    id = 999;
    static const char *ERREUR = "{\"error\":{\"code\":-1,\"message\":\"koko\"},\"id\":12}";
    VERIFIER(rpc_classifier(ERREUR, strlen(ERREUR), &id) == RPC_MSG_REPONSE);
    VERIFIER(id == 12);

    static const char *STATUS =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_status_update\",\"params\":[{},1.0]}";
    VERIFIER(rpc_classifier(STATUS, strlen(STATUS), NULL) == RPC_MSG_STATUS_UPDATE);

    static const char *READY = "{\"jsonrpc\":\"2.0\",\"method\":\"notify_klippy_ready\",\"params\":[]}";
    VERIFIER(rpc_classifier(READY, strlen(READY), NULL) == RPC_MSG_KLIPPY_READY);

    static const char *DECONNECTE =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_klippy_disconnected\",\"params\":[]}";
    VERIFIER(rpc_classifier(DECONNECTE, strlen(DECONNECTE), NULL) == RPC_MSG_KLIPPY_DECONNECTE);

    static const char *INCONNUE =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":[\"ok\"]}";
    VERIFIER(rpc_classifier(INCONNUE, strlen(INCONNUE), NULL) == RPC_MSG_AUTRE);

    /* id absent sur une reponse => INVALIDE (rien a correler) */
    id = 999;
    VERIFIER(rpc_classifier("{\"result\":{}}", strlen("{\"result\":{}}"), &id) == RPC_MSG_INVALIDE);
    VERIFIER(id == 0);   /* mis a 0 dans tous les cas non-REPONSE */

    /* id non numerique sur une reponse => INVALIDE */
    VERIFIER(rpc_classifier("{\"result\":{},\"id\":\"5\"}", strlen("{\"result\":{},\"id\":\"5\"}"), NULL)
             == RPC_MSG_INVALIDE);

    /* JSON illisible => INVALIDE */
    VERIFIER(rpc_classifier("pas du json", 11, NULL) == RPC_MSG_INVALIDE);
    VERIFIER(rpc_classifier("", 0, NULL) == RPC_MSG_INVALIDE);
    VERIFIER(rpc_classifier(NULL, 0, NULL) == RPC_MSG_INVALIDE);

    /* ni reponse ni notification => INVALIDE */
    VERIFIER(rpc_classifier("{\"jsonrpc\":\"2.0\"}", strlen("{\"jsonrpc\":\"2.0\"}"), NULL)
             == RPC_MSG_INVALIDE);
}

/* --- rpc_fusionner_status ------------------------------------------------ */

/* Enveloppe un objet de statut dans un notify_status_update complet, avec un
 * horodatage bidon en params[1] -- exactement le piege documente ("params[0],
 * pas params") que cette fonction doit deverrouiller correctement. */
static void enveloppe(char *sortie, size_t taille, const char *statut_json)
{
    snprintf(sortie, taille,
             "{\"jsonrpc\":\"2.0\",\"method\":\"notify_status_update\","
             "\"params\":[%s,1234.5]}", statut_json);
}

static void section_fusionner_status(void)
{
    char msg[1024];

    /* --- mise a jour partielle : un seul champ pousse, tout le reste
     * INTACT. On construit "attendu" comme une copie de "avant" a laquelle
     * on applique A LA MAIN le seul changement attendu, puis on compare TOUT
     * le struct : la moindre ecriture parasite ailleurs echoue le test. --- */
    {
        etat_klipper_t avant;
        memset(&avant, 0, sizeof(avant));
        avant.extrudeurs[0].presente = true;
        avant.extrudeurs[0].actuelle = 200.0f;
        avant.extrudeurs[0].consigne = 200.0f;
        avant.nb_extrudeurs = 1;
        avant.plateau.presente = true;
        avant.plateau.actuelle = 60.0f;
        avant.vitesse_pct = 100;
        avant.flux_pct = 100;
        strcpy(avant.fichier, "piece.gcode");

        etat_klipper_t apres = avant;
        enveloppe(msg, sizeof(msg), "{\"heater_bed\":{\"temperature\":61.5}}");
        VERIFIER(rpc_fusionner_status(&apres, msg, strlen(msg)));

        etat_klipper_t attendu = avant;
        attendu.plateau.actuelle = 61.5f;
        VERIFIER(memcmp(&apres, &attendu, sizeof(apres)) == 0);
    }

    /* --- extruder2 present => presente + nb_extrudeurs recalcules --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        e.extrudeurs[0].presente = true;
        e.nb_extrudeurs = 1;

        enveloppe(msg, sizeof(msg), "{\"extruder2\":{\"temperature\":25.0,\"target\":0.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.extrudeurs[2].presente == true);
        VERIFIER_FLOAT(e.extrudeurs[2].actuelle, 25.0f, 0.01f);
        VERIFIER(e.nb_extrudeurs == 2);
        VERIFIER(e.extrudeurs[1].presente == false);
    }

    /* --- extruder hors bornes (extruder9) : objet ignore, reste du message
     * applique normalement --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"extruder9\":{\"temperature\":300.0},\"heater_bed\":{\"temperature\":50.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.plateau.actuelle, 50.0f, 0.01f);
        VERIFIER(e.nb_extrudeurs == 0);
    }

    /* --- homed_axes : "xyz" => masque 7, "" => masque 0 --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"toolhead\":{\"homed_axes\":\"xyz\"}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.axes_references == 7);

        e.axes_references = 7;
        enveloppe(msg, sizeof(msg), "{\"toolhead\":{\"homed_axes\":\"\"}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.axes_references == 0);

        /* masque partiel + outil actif via nom */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"toolhead\":{\"homed_axes\":\"xz\",\"extruder\":\"extruder2\","
                                     "\"position\":[10.5,20.25,5.0,0.0]}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.axes_references == 5);   /* bit0 + bit2 */
        VERIFIER(e.outil_actif == 2);
        VERIFIER_FLOAT(e.position[0], 10.5f, 0.001f);
        VERIFIER_FLOAT(e.position[1], 20.25f, 0.001f);
        VERIFIER_FLOAT(e.position[2], 5.0f, 0.001f);
    }

    /* --- speed_factor 1.5 => 150 %, extrude_factor 0.9 => 90 %,
     * homing_origin[2] => babystep en µm, absolute_coordinates --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"gcode_move\":{\"speed_factor\":1.5,\"extrude_factor\":0.9,"
            "\"homing_origin\":[0.0,0.0,-0.075,0.0],\"absolute_coordinates\":true}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.vitesse_pct == 150);
        VERIFIER(e.flux_pct == 90);
        VERIFIER(e.babystep_z_um == -75);
        VERIFIER(e.deplacement_absolu == true);
    }

    /* --- valeurs non finies : le champ concerne reste inchange, le reste du
     * MEME objet continue d'etre applique (poison par champ, pas par
     * message). 1e40 devient +infini une fois retreci en float -- meme
     * piege documente que moonraker_parse.c. --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        e.extrudeurs[0].actuelle = 42.0f;   /* valeur temoin : ne doit pas bouger */
        enveloppe(msg, sizeof(msg), "{\"extruder\":{\"temperature\":1e40,\"target\":210.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.extrudeurs[0].actuelle, 42.0f, 0.01f);   /* inchange */
        VERIFIER_FLOAT(e.extrudeurs[0].consigne, 210.0f, 0.01f);  /* applique */
        VERIFIER(e.extrudeurs[0].presente == true);   /* l'objet EST apparu */
    }

    /* --- JSON hostile : params absent, params pas un tableau, params[0] pas
     * un objet, JSON illisible, imbrication 40 niveaux, troncature en plein
     * milieu => false et etat INTACT (temoin 0x5A, memcmp du struct ENTIER) --- */
    {
        etat_klipper_t temoin;
        memset(&temoin, 0x5A, sizeof(temoin));
        etat_klipper_t e;

        e = temoin;
        static const char *SANS_PARAMS =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_status_update\"}";
        VERIFIER(!rpc_fusionner_status(&e, SANS_PARAMS, strlen(SANS_PARAMS)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        static const char *PARAMS_OBJET =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_status_update\",\"params\":{\"a\":1}}";
        VERIFIER(!rpc_fusionner_status(&e, PARAMS_OBJET, strlen(PARAMS_OBJET)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        static const char *PARAMS0_PAS_OBJET =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_status_update\",\"params\":[42,1.0]}";
        VERIFIER(!rpc_fusionner_status(&e, PARAMS0_PAS_OBJET, strlen(PARAMS0_PAS_OBJET)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_status(&e, "pas du json", 11));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_status(&e, "", 0));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_status(&e, NULL, 0));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_status(NULL, "{}", 2));

        /* imbrication 40 niveaux (CJSON_NESTING_LIMIT = 32 dans ce harnais,
         * voir host-test/CMakeLists.txt) */
        char profond[300];
        size_t pos = 0;
        const int niveaux = 40;
        for (int i = 0; i < niveaux; i++) {
            pos += (size_t)snprintf(profond + pos, sizeof(profond) - pos, "{\"a\":");
        }
        pos += (size_t)snprintf(profond + pos, sizeof(profond) - pos, "1");
        for (int i = 0; i < niveaux; i++) {
            pos += (size_t)snprintf(profond + pos, sizeof(profond) - pos, "}");
        }
        VERIFIER(pos < sizeof(profond));
        e = temoin;
        VERIFIER(!rpc_fusionner_status(&e, profond, pos));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        /* troncature en plein milieu */
        enveloppe(msg, sizeof(msg), "{\"heater_bed\":{\"temperature\":61.5");
        /* pas de fermeture -- msg est deja invalide, mais on tronque encore
         * plus court pour etre sur de couper en plein token */
        size_t tronque = strlen(msg) > 5 ? strlen(msg) - 5 : strlen(msg);
        e = temoin;
        VERIFIER(!rpc_fusionner_status(&e, msg, tronque));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);
    }
}

/* --- rpc_lire_reponse ----------------------------------------------------- */

static void section_lire_reponse(void)
{
    bool succes;
    char erreur[64];

    /* result ok */
    succes = false;
    strcpy(erreur, "bruit");
    static const char *OK = "{\"result\":{\"foo\":1},\"id\":1}";
    VERIFIER(rpc_lire_reponse(OK, strlen(OK), &succes, erreur, sizeof(erreur)));
    VERIFIER(succes == true);
    VERIFIER_TEXTE(erreur, "");

    /* error avec message Klipper */
    succes = true;
    static const char *ERR =
        "{\"error\":{\"code\":-32601,\"message\":\"Unknown macro\"},\"id\":2}";
    VERIFIER(rpc_lire_reponse(ERR, strlen(ERR), &succes, erreur, sizeof(erreur)));
    VERIFIER(succes == false);
    VERIFIER_TEXTE(erreur, "Unknown macro");

    /* error sans message */
    succes = true;
    static const char *ERR_SANS_MSG = "{\"error\":{\"code\":-1},\"id\":3}";
    VERIFIER(rpc_lire_reponse(ERR_SANS_MSG, strlen(ERR_SANS_MSG), &succes, erreur, sizeof(erreur)));
    VERIFIER(succes == false);
    VERIFIER_TEXTE(erreur, "");

    /* erreur_texte/taille_erreur optionnels */
    succes = false;
    static const char *ERR2 = "{\"error\":{\"message\":\"koko\"},\"id\":4}";
    VERIFIER(rpc_lire_reponse(ERR2, strlen(ERR2), &succes, NULL, 0));
    VERIFIER(succes == false);

    /* message plus long que le tampon, coupe SANS fendre un caractere UTF-8
     * multi-octets : "\xC2\xB0" (le signe degre, 2 octets) juste a la coupe */
    {
        char petit[6];   /* 5 octets utiles + '\0' */
        /* "\xB0" suivi directement d'un chiffre/lettre hexadecimal ("C")
         * serait absorbe dans la MEME sequence d'echappement (\xB0C se lit
         * comme un seul \x de 3 chiffres, hors plage d'un char) -- la
         * concatenation de litteraux adjacents coupe la sequence proprement. */
        static const char *ERR_UTF8 =
            "{\"error\":{\"message\":\"AB\xC2\xB0" "C\"},\"id\":5}";
        succes = false;
        VERIFIER(rpc_lire_reponse(ERR_UTF8, strlen(ERR_UTF8), &succes, petit, sizeof(petit)));
        VERIFIER(succes == false);
        /* le texte source est "AB\xC2\xB0C" (4 caracteres, 5 octets) ; le
         * tampon ne peut garder que 5 octets utiles -- tout tient en fait.
         * On reduit encore le tampon pour forcer une vraie coupe. */
        char plus_petit[4];   /* 3 octets utiles : "AB" + 1 octet du degre */
        succes = false;
        VERIFIER(rpc_lire_reponse(ERR_UTF8, strlen(ERR_UTF8), &succes, plus_petit, sizeof(plus_petit)));
        size_t len = strlen(plus_petit);
        VERIFIER(len <= 3);
        /* le dernier octet garde ne doit jamais etre un octet de tete d'une
         * sequence UTF-8 multi-octets sans sa suite (0xC2 seul) */
        VERIFIER_TEXTE(plus_petit, "AB");
    }

    /* entrees invalides => false */
    VERIFIER(!rpc_lire_reponse("pas du json", 11, &succes, erreur, sizeof(erreur)));
    VERIFIER(!rpc_lire_reponse("{\"jsonrpc\":\"2.0\"}", 18, &succes, erreur, sizeof(erreur)));
    VERIFIER(!rpc_lire_reponse("", 0, &succes, erreur, sizeof(erreur)));
    VERIFIER(!rpc_lire_reponse(NULL, 0, &succes, erreur, sizeof(erreur)));
    VERIFIER(!rpc_lire_reponse(OK, strlen(OK), NULL, erreur, sizeof(erreur)));
}

/* --- rpc_lire_macros ------------------------------------------------------- */

static void section_lire_macros(void)
{
    /* liste nominale, _CACHEE presente (le filtre est ailleurs, cote UI) */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        static const char *LISTE =
            "{\"result\":{\"objects\":["
            "\"gcode_macro START_PRINT\",\"gcode_macro _CACHEE\",\"toolhead\","
            "\"extruder\",\"gcode_macro PURGE_LINE\""
            "]},\"id\":1}";
        VERIFIER(rpc_lire_macros(&e, LISTE, strlen(LISTE)));
        VERIFIER(e.nb_macros == 3);
        VERIFIER(!e.macros_tronquees);

        bool a_start = false, a_cachee = false, a_purge = false;
        for (int i = 0; i < e.nb_macros; i++) {
            if (strcmp(e.macros[i], "START_PRINT") == 0) a_start = true;
            if (strcmp(e.macros[i], "_CACHEE") == 0) a_cachee = true;
            if (strcmp(e.macros[i], "PURGE_LINE") == 0) a_purge = true;
        }
        VERIFIER(a_start);
        VERIFIER(a_cachee);
        VERIFIER(a_purge);
    }

    /* 50 macros => 48 + tronquees */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));

        char liste[4096];
        size_t pos = (size_t)snprintf(liste, sizeof(liste), "{\"result\":{\"objects\":[");
        for (int i = 0; i < 50; i++) {
            pos += (size_t)snprintf(liste + pos, sizeof(liste) - pos,
                                     "%s\"gcode_macro M%d\"", i == 0 ? "" : ",", i);
        }
        pos += (size_t)snprintf(liste + pos, sizeof(liste) - pos, "]},\"id\":1}");
        VERIFIER(pos < sizeof(liste));

        VERIFIER(rpc_lire_macros(&e, liste, pos));
        VERIFIER(e.nb_macros == KLIPPER_MACROS_MAX);
        VERIFIER(e.macros_tronquees);
    }

    /* nom >= KLIPPER_MACRO_NOM_MAX : ignore (pas tronque), le reste de la
     * liste est traite normalement */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        /* 32 'A' (>= KLIPPER_MACRO_NOM_MAX=32) : trop long pour tenir avec le
         * '\0' dans macros[i][32] */
        static const char *LISTE =
            "{\"result\":{\"objects\":["
            "\"gcode_macro AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
            "\"gcode_macro COURTE\""
            "]},\"id\":1}";
        VERIFIER(rpc_lire_macros(&e, LISTE, strlen(LISTE)));
        VERIFIER(e.nb_macros == 1);
        VERIFIER_TEXTE(e.macros[0], "COURTE");
        VERIFIER(!e.macros_tronquees);   /* omission de longueur != troncature de compte */

        for (int i = 0; i < e.nb_macros; i++) {
            VERIFIER(strncmp(e.macros[i], "AAAA", 4) != 0);
        }
    }

    /* configfile : meme convention de nom, objet dont les CLES sont les
     * sections */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        static const char *CONFIGFILE =
            "{\"result\":{\"status\":{\"configfile\":{\"config\":{"
            "\"gcode_macro START_PRINT\":{\"gcode\":\"...\"},"
            "\"extruder\":{\"max_temp\":\"280\"}"
            "}}}},\"id\":1}";
        VERIFIER(rpc_lire_macros(&e, CONFIGFILE, strlen(CONFIGFILE)));
        VERIFIER(e.nb_macros == 1);
        VERIFIER_TEXTE(e.macros[0], "START_PRINT");
    }

    /* remplace ENTIEREMENT la liste precedente (instantane complet, pas une
     * fusion) */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        e.nb_macros = 5;
        strcpy(e.macros[0], "VIEILLE_MACRO");

        static const char *LISTE = "{\"result\":{\"objects\":[\"gcode_macro NEUVE\"]},\"id\":1}";
        VERIFIER(rpc_lire_macros(&e, LISTE, strlen(LISTE)));
        VERIFIER(e.nb_macros == 1);
        VERIFIER_TEXTE(e.macros[0], "NEUVE");
    }

    /* entrees invalides => false, etat INTACT */
    {
        etat_klipper_t temoin;
        memset(&temoin, 0x5A, sizeof(temoin));
        etat_klipper_t e = temoin;

        VERIFIER(!rpc_lire_macros(&e, "pas du json", 11));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        static const char *NI_LUN_NI_LAUTRE = "{\"result\":{\"objects_typo\":[]},\"id\":1}";
        VERIFIER(!rpc_lire_macros(&e, NI_LUN_NI_LAUTRE, strlen(NI_LUN_NI_LAUTRE)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_lire_macros(&e, "", 0));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_lire_macros(&e, NULL, 0));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        VERIFIER(!rpc_lire_macros(NULL, "{}", 2));
    }
}

void suite_moonraker_rpc(void)
{
    printf("suite : protocole moonraker (JSON-RPC / WebSocket)\n");
    section_construire_requete();
    section_construire_abonnement();
    section_classifier();
    section_fusionner_status();
    section_lire_reponse();
    section_lire_macros();
}
