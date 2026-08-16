/* Gestion de parc (spec 2026-08-15-gestion-parc-design.md) : store de la
 * configuration du parc (6 imprimantes max, une active) et des états sondés,
 * plus le parseur PUR de la réponse de sonde Moonraker. Le store est
 * process-wide (une instance) : chaque section repart d'une config vide via
 * parc_config_definir() -- aucune contrainte d'ordre avec les autres suites,
 * mais les sections de CE fichier s'enchaînent sur le même état. */
#include <string.h>

#include "parc_imprimantes.h"
#include "parc_parse.h"
#include "petit_test.h"

/* --- configuration : bornes, actif, generation --------------------------- */

static void section_parc_config(void)
{
    parc_config_t config;
    memset(&config, 0, sizeof(config));

    /* Etat de depart propre. */
    VERIFIER(parc_config_definir(&config) == ESP_OK);
    parc_config_lire(&config);
    VERIFIER(config.nb == 0);
    VERIFIER(config.actif == 0);

    /* Deux entrees, la seconde active. */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "CR-10 S5");
    strcpy(config.entrees[0].hote.adresse, "192.168.10.101");
    config.entrees[0].hote.port = 7125;
    strcpy(config.entrees[1].nom, "Snapmaker U1");
    strcpy(config.entrees[1].hote.adresse, "192.168.10.102");
    config.entrees[1].hote.port = 7125;
    config.nb = 2;
    config.actif = 1;
    uint32_t generation_avant = parc_generation();
    VERIFIER(parc_config_definir(&config) == ESP_OK);
    VERIFIER(parc_generation() == generation_avant + 1);

    parc_config_t lu;
    parc_config_lire(&lu);
    VERIFIER(lu.nb == 2);
    VERIFIER(lu.actif == 1);
    VERIFIER_TEXTE(lu.entrees[0].nom, "CR-10 S5");
    VERIFIER_TEXTE(lu.entrees[1].hote.adresse, "192.168.10.102");
    VERIFIER(lu.entrees[1].hote.port == 7125);

    /* Bornes defensives : nb > PARC_MAX rabattu, actif >= nb rabattu a 0. */
    config.nb = PARC_MAX + 3;
    config.actif = PARC_MAX + 1;
    VERIFIER(parc_config_definir(&config) == ESP_OK);
    parc_config_lire(&lu);
    VERIFIER(lu.nb == PARC_MAX);
    VERIFIER(lu.actif < lu.nb);

    /* NULL refuse. */
    VERIFIER(parc_config_definir(NULL) == ESP_ERR_INVALID_ARG);
}

/* --- etats sondes -------------------------------------------------------- */

static void section_parc_etats(void)
{
    parc_etat_t etats[PARC_MAX];

    /* Repartir d'une config a deux entrees (section precedente : bornes). */
    parc_config_t config;
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    strcpy(config.entrees[0].hote.adresse, "10.0.0.1");
    config.entrees[0].hote.port = 7125;
    config.nb = 1;
    VERIFIER(parc_config_definir(&config) == ESP_OK);

    /* Jamais sondee par defaut. */
    parc_etats_lire(etats);
    VERIFIER(!etats[0].sonde);

    parc_etat_t sonde;
    memset(&sonde, 0, sizeof(sonde));
    sonde.sonde = true;
    sonde.atteignable = true;
    strcpy(sonde.etat, "printing");
    sonde.buse = 210.5f;
    sonde.lit = 60.0f;
    sonde.progression_pct = 42;
    uint32_t generation_avant = parc_generation();
    parc_etat_publier(0, &sonde);
    VERIFIER(parc_generation() == generation_avant + 1);
    parc_etats_lire(etats);
    VERIFIER(etats[0].sonde);
    VERIFIER(etats[0].atteignable);
    VERIFIER_TEXTE(etats[0].etat, "printing");
    VERIFIER(etats[0].progression_pct == 42);

    /* Indice hors bornes : no-op, pas de generation. */
    generation_avant = parc_generation();
    parc_etat_publier(PARC_MAX, &sonde);
    VERIFIER(parc_generation() == generation_avant);
    parc_etat_publier(0, NULL);
    VERIFIER(parc_generation() == generation_avant);
}

/* --- parseur de la reponse de sonde -------------------------------------- */

/* Reponse realiste (raccourcie) de
 * /printer/objects/query?print_stats&extruder&heater_bed&display_status. */
static const char REPONSE_NOMINALE[] =
    "{\"result\":{\"eventtime\":123.4,\"status\":{"
    "\"print_stats\":{\"state\":\"printing\",\"filename\":\"piece.gcode\"},"
    "\"extruder\":{\"temperature\":210.53,\"target\":215.0},"
    "\"heater_bed\":{\"temperature\":59.98,\"target\":60.0},"
    "\"display_status\":{\"progress\":0.42,\"message\":null}}}}";

static void section_parc_parse(void)
{
    parc_etat_t sortie;

    VERIFIER(parc_parse_reponse(REPONSE_NOMINALE, strlen(REPONSE_NOMINALE), &sortie));
    VERIFIER(sortie.sonde);
    VERIFIER(sortie.atteignable);
    VERIFIER_TEXTE(sortie.etat, "printing");
    VERIFIER(sortie.buse > 210.0f && sortie.buse < 211.0f);
    VERIFIER(sortie.lit > 59.0f && sortie.lit < 61.0f);
    VERIFIER(sortie.progression_pct == 42);

    /* Champs absents toleres : etat vide, zeros. */
    static const char minimal[] = "{\"result\":{\"status\":{}}}";
    VERIFIER(parc_parse_reponse(minimal, strlen(minimal), &sortie));
    VERIFIER(sortie.etat[0] == '\0');
    VERIFIER(sortie.progression_pct == 0);

    /* progress > 1 borne a 100. */
    static const char deborde[] =
        "{\"result\":{\"status\":{\"display_status\":{\"progress\":7.5}}}}";
    VERIFIER(parc_parse_reponse(deborde, strlen(deborde), &sortie));
    VERIFIER(sortie.progression_pct == 100);

    /* Invalide / NULL : false. */
    VERIFIER(!parc_parse_reponse("pas du json", 11, &sortie));
    VERIFIER(!parc_parse_reponse(NULL, 0, &sortie));
    VERIFIER(!parc_parse_reponse(REPONSE_NOMINALE, strlen(REPONSE_NOMINALE), NULL));
}

/* --- retrait d'une entree ------------------------------------------------ *
 * parc_config_retirer() est PURE (elle travaille sur la copie fournie, sans
 * toucher au store ni a la NVS) : c'est ce qui la rend testable ici. L'ecran
 * et le point d'entree HTTP l'appliquent puis persistent avec
 * parc_config_definir(), exactement comme ils le font deja pour l'ajout. */
static void section_parc_retirer(void)
{
    parc_config_t config;

    /* Trois entrees, la DERNIERE active. Retirer une entree AVANT l'active
     * doit decaler les suivantes ET decrementer `actif`, sinon le marqueur
     * designerait silencieusement une autre imprimante que celle en service. */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    strcpy(config.entrees[1].nom, "B");
    strcpy(config.entrees[2].nom, "C");
    config.nb = 3;
    config.actif = 2;
    VERIFIER(parc_config_retirer(&config, 0) == ESP_OK);
    VERIFIER(config.nb == 2);
    VERIFIER_TEXTE(config.entrees[0].nom, "B");
    VERIFIER_TEXTE(config.entrees[1].nom, "C");
    VERIFIER(config.actif == 1); /* "C" reste l'active */

    /* Retirer une entree APRES l'active laisse `actif` intact. */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    strcpy(config.entrees[1].nom, "B");
    strcpy(config.entrees[2].nom, "C");
    config.nb = 3;
    config.actif = 0;
    VERIFIER(parc_config_retirer(&config, 2) == ESP_OK);
    VERIFIER(config.nb == 2);
    VERIFIER(config.actif == 0);
    VERIFIER_TEXTE(config.entrees[1].nom, "B");

    /* Retirer l'ACTIVE alors qu'il reste d'autres imprimantes : REFUS.
     * Supprimer l'active imposerait de reecrire l'hote de boot et de
     * redemarrer la dalle -- un effet de bord que l'utilisateur ne demande
     * pas en supprimant une tuile (decision du 2026-08-16). La config ne doit
     * pas etre modifiee du tout. */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    strcpy(config.entrees[1].nom, "B");
    config.nb = 2;
    config.actif = 1;
    VERIFIER(parc_config_retirer(&config, 1) == ESP_ERR_INVALID_STATE);
    VERIFIER(config.nb == 2);
    VERIFIER(config.actif == 1);
    VERIFIER_TEXTE(config.entrees[1].nom, "B");

    /* SEULE exception : l'active est la DERNIERE imprimante du parc. La
     * retirer vide le parc (nb = 0) -- rien a rebasculer, et l'hote de boot
     * n'est pas du ressort de cette fonction pure. */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    config.nb = 1;
    config.actif = 0;
    VERIFIER(parc_config_retirer(&config, 0) == ESP_OK);
    VERIFIER(config.nb == 0);
    VERIFIER(config.actif == 0);

    /* Bornes : indice hors des entrees valides, et pointeur nul. */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    config.nb = 1;
    config.actif = 0;
    VERIFIER(parc_config_retirer(&config, 1) == ESP_ERR_INVALID_ARG);
    VERIFIER(parc_config_retirer(&config, PARC_MAX) == ESP_ERR_INVALID_ARG);
    VERIFIER(config.nb == 1);
    VERIFIER(parc_config_retirer(NULL, 0) == ESP_ERR_INVALID_ARG);

    /* Parc vide : aucun indice n'est valide. */
    memset(&config, 0, sizeof(config));
    VERIFIER(parc_config_retirer(&config, 0) == ESP_ERR_INVALID_ARG);

    /* L'entree liberee est REMISE A ZERO, jamais laissee en place : sans ca
     * un nom d'imprimante supprimee survivrait dans entrees[nb] et
     * reapparaitrait au prochain ajout (qui ecrit dans cette case avant de
     * l'avoir nettoyee). */
    memset(&config, 0, sizeof(config));
    strcpy(config.entrees[0].nom, "A");
    strcpy(config.entrees[1].nom, "B");
    strcpy(config.entrees[1].hote.adresse, "192.168.1.42");
    config.entrees[1].hote.port = 7125;
    config.nb = 2;
    config.actif = 0;
    VERIFIER(parc_config_retirer(&config, 1) == ESP_OK);
    VERIFIER(config.nb == 1);
    VERIFIER_TEXTE(config.entrees[1].nom, "");
    VERIFIER_TEXTE(config.entrees[1].hote.adresse, "");
    VERIFIER(config.entrees[1].hote.port == 0);
}

void suite_parc(void)
{
    printf("suite : parc d'imprimantes (config + etats + parseur de sonde)\n");
    section_parc_config();
    section_parc_etats();
    section_parc_parse();
    section_parc_retirer();
}
