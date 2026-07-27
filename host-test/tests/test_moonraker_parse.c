#include <string.h>

#include "moonraker_parse.h"
#include "petit_test.h"

static const char *REPONSE_IMPRESSION =
    "{\"result\":{\"status\":{"
    "\"extruder\":{\"temperature\":210.4,\"target\":210.0},"
    "\"heater_bed\":{\"temperature\":60.1,\"target\":60.0},"
    "\"print_stats\":{\"filename\":\"piece.gcode\",\"state\":\"printing\","
    "\"print_duration\":600.0},"
    "\"virtual_sdcard\":{\"progress\":0.5,\"is_active\":true},"
    "\"webhooks\":{\"state\":\"ready\"}"
    "}}}";

static const char *REPONSE_REPOS =
    "{\"result\":{\"status\":{"
    "\"extruder\":{\"temperature\":24.5,\"target\":0.0},"
    "\"heater_bed\":{\"temperature\":23.1,\"target\":0.0},"
    "\"print_stats\":{\"filename\":\"\",\"state\":\"standby\",\"print_duration\":0.0},"
    "\"virtual_sdcard\":{\"progress\":0.0,\"is_active\":false},"
    "\"webhooks\":{\"state\":\"ready\"}"
    "}}}";

void suite_moonraker_parse(void)
{
    printf("suite : analyseur moonraker\n");
    etat_klipper_t e;

    /* --- cas nominal, impression en cours --- */
    memset(&e, 0xAA, sizeof(e));   /* rempli de bruit : l'analyseur doit tout écrire */
    VERIFIER(moonraker_parse_status(REPONSE_IMPRESSION, strlen(REPONSE_IMPRESSION), &e));
    VERIFIER_FLOAT(e.buse_actuelle, 210.4f, 0.01f);
    VERIFIER_FLOAT(e.buse_consigne, 210.0f, 0.01f);
    VERIFIER_FLOAT(e.plateau_actuel, 60.1f, 0.01f);
    VERIFIER_FLOAT(e.plateau_consigne, 60.0f, 0.01f);
    VERIFIER_TEXTE(e.fichier, "piece.gcode");
    VERIFIER_TEXTE(e.etat, "printing");
    VERIFIER_FLOAT(e.progression, 0.5f, 0.001f);
    VERIFIER(e.impression_en_cours);
    VERIFIER(!e.impression_en_pause);
    /* 600 s ecoulees a 50 % => 600 s restantes */
    VERIFIER(e.temps_restant_s == 600);

    /* --- machine au repos --- */
    memset(&e, 0xAA, sizeof(e));
    VERIFIER(moonraker_parse_status(REPONSE_REPOS, strlen(REPONSE_REPOS), &e));
    VERIFIER_TEXTE(e.etat, "standby");
    VERIFIER_TEXTE(e.fichier, "");
    VERIFIER(!e.impression_en_cours);
    VERIFIER(e.temps_restant_s == 0);
    VERIFIER_FLOAT(e.buse_consigne, 0.0f, 0.01f);

    /* --- progression trop faible : l'estimation doit etre neutralisee --- */
    static const char *DEBUT =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"printing\",\"print_duration\":5.0},"
        "\"virtual_sdcard\":{\"progress\":0.001,\"is_active\":true}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(DEBUT, strlen(DEBUT), &e));
    VERIFIER(e.temps_restant_s == 0);   /* plutot qu'une valeur absurde */

    /* --- champs absents : ne pas planter, laisser les valeurs a zero --- */
    static const char *PARTIEL = "{\"result\":{\"status\":{\"webhooks\":{\"state\":\"ready\"}}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(PARTIEL, strlen(PARTIEL), &e));
    VERIFIER_FLOAT(e.buse_actuelle, 0.0f, 0.01f);
    VERIFIER_TEXTE(e.fichier, "");

    /* --- pause --- */
    static const char *PAUSE =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"paused\",\"print_duration\":100.0},"
        "\"virtual_sdcard\":{\"progress\":0.25,\"is_active\":false}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(PAUSE, strlen(PAUSE), &e));
    VERIFIER(e.impression_en_pause);
    VERIFIER(!e.impression_en_cours);

    /* --- entrees invalides : rendre false sans toucher la sortie --- */
    etat_klipper_t temoin;
    memset(&temoin, 0x5A, sizeof(temoin));
    e = temoin;
    VERIFIER(!moonraker_parse_status("pas du json", 11, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    e = temoin;
    VERIFIER(!moonraker_parse_status("{\"error\":\"koko\"}", 16, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    e = temoin;
    VERIFIER(!moonraker_parse_status("", 0, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    e = temoin;
    VERIFIER(!moonraker_parse_status(NULL, 0, &e));
    VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

    /* --- nom de fichier plus long que le tampon : tronquer sans deborder --- */
    static const char *LONG_NOM =
        "{\"result\":{\"status\":{\"print_stats\":{\"filename\":"
        "\"0123456789012345678901234567890123456789012345678901234567890123456789.gcode\","
        "\"state\":\"printing\",\"print_duration\":10.0}}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(LONG_NOM, strlen(LONG_NOM), &e));
    VERIFIER(strlen(e.fichier) == KLIPPER_FICHIER_MAX - 1);

    /* --- print_duration demesure (1e40) : le flottant devient +infini une
     * fois retreci en float, la conversion doit etre bornee plutot que de
     * produire un comportement indefini --- */
    static const char *DUREE_INFINIE =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"printing\",\"print_duration\":1e40},"
        "\"virtual_sdcard\":{\"progress\":0.5,\"is_active\":true}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(DUREE_INFINIE, strlen(DUREE_INFINIE), &e));
    VERIFIER(e.temps_restant_s == KLIPPER_TEMPS_RESTANT_MAX_S);

    /* --- duree finie mais assez grande pour deborder un uint32_t une fois
     * divisee par une progression proche du seuil bas --- */
    static const char *DUREE_DEBORDANTE =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"printing\",\"print_duration\":400000000.0},"
        "\"virtual_sdcard\":{\"progress\":0.02,\"is_active\":true}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(DUREE_DEBORDANTE, strlen(DUREE_DEBORDANTE), &e));
    VERIFIER(e.temps_restant_s == KLIPPER_TEMPS_RESTANT_MAX_S);

    /* --- bornes exactes du garde-fou de progression : 1 % pile et 100 % pile
     * doivent toutes deux neutraliser l'estimation --- */
    static const char *PROGRESSION_UN_POURCENT_PILE =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"printing\",\"print_duration\":1000.0},"
        "\"virtual_sdcard\":{\"progress\":0.01,\"is_active\":true}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(PROGRESSION_UN_POURCENT_PILE, strlen(PROGRESSION_UN_POURCENT_PILE), &e));
    VERIFIER(e.temps_restant_s == 0);

    static const char *PROGRESSION_CENT_POURCENT_PILE =
        "{\"result\":{\"status\":{"
        "\"print_stats\":{\"state\":\"printing\",\"print_duration\":1000.0},"
        "\"virtual_sdcard\":{\"progress\":1.0,\"is_active\":true}"
        "}}}";
    memset(&e, 0, sizeof(e));
    VERIFIER(moonraker_parse_status(PROGRESSION_CENT_POURCENT_PILE, strlen(PROGRESSION_CENT_POURCENT_PILE), &e));
    VERIFIER(e.temps_restant_s == 0);
}
