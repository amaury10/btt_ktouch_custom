/* Rejeu de transcripts JSON-RPC Moonraker REELS (tâche 4, jalon 3a).
 *
 * test_moonraker_rpc.c encode notre LECTURE de la documentation Moonraker.
 * Ce fichier rejoue des conversations RÉELLEMENT enregistrées contre
 * virtual-klipper-printer (un vrai Klipper à MCU simulé + un vrai
 * Moonraker, voir docs/dev/klipper-simule.md) dans EXACTEMENT les mêmes
 * fonctions pures de moonraker_rpc.c -- c'est ce qui empêche le jalon de
 * découvrir le protocole réel seulement une fois sur l'appareil.
 *
 * Les fixtures (host-test/fixtures/moonraker/, extension .jsonl) sont produites par
 * tools/moonraker-record/enregistrer.py : un objet JSON par ligne,
 * {"dir":"in"|"out","t_ms":N,"raw":"<texte EXACT reçu/envoyé>",
 * "message":<le même, déjà décodé>}. Seuls les messages ENTRANTS
 * ("dir":"in", ce que CE PROCESSUS a reçu de Moonraker) sont rejoués ici :
 * rpc_classifier/rpc_fusionner_* ne s'appliquent qu'à ce qu'on REÇOIT,
 * jamais à ce qu'on envoie. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "etat_klipper.h"
#include "moonraker_rpc.h"
#include "petit_test.h"

#ifndef KTOUCH_FIXTURES_MOONRAKER_DIR
#error "KTOUCH_FIXTURES_MOONRAKER_DIR doit etre defini par host-test/CMakeLists.txt"
#endif

/* Id JSON-RPC de la requete d'abonnement et de l'action (SET_HEATER_TEMPERATURE
 * / printer.gcode.script) telles qu'émises par le SEUL enregistreur qui
 * produit ces fixtures (tools/moonraker-record/enregistrer.py, constantes
 * ID_SUBSCRIBE/ID_ACTION) -- SYNCHRONISÉ À LA MAIN avec ce script : si l'un
 * des deux change, l'autre doit changer aussi, sinon ce rejeu ne saurait
 * plus reconnaître quelle réponse corrélée correspond à quelle requête. */
#define FIXTURE_ID_ABONNEMENT 2u
#define FIXTURE_ID_ACTION     3u

typedef struct {
    etat_klipper_t etat;
    int  nb_lignes;
    int  nb_entrants;
    int  nb_status_update;
    int  nb_reponses;
    bool instantane_applique;

    /* Dernière réponse corrélée à l'id "action" (printer.gcode.script) et
     * dernière notification notify_gcode_response vues, capturées telles
     * quelles pour que chaque section de test puisse leur appliquer
     * rpc_lire_reponse() / une inspection de texte après coup -- voir
     * section_macro_inexistante() ci-dessous pour pourquoi les deux sont
     * nécessaires (la trouvaille centrale de cette tâche). */
    char derniere_reponse_action[1024];
    bool a_reponse_action;
    char dernier_gcode_response[512];
    bool a_gcode_response;
} rejeu_t;

/* Lit `chemin` ligne à ligne et rejoue chaque message ENTRANT dans les
 * fonctions pures de moonraker_rpc.c. C'est un fichier plutôt qu'une
 * socket, mais du point de vue de moonraker_rpc.c la distinction n'existe
 * pas : ces fonctions ne touchent jamais au réseau, seul l'appelant (ici un
 * fgets(), demain la tâche 5 avec une vraie WebSocket) change. */
static void rejeu_fixture(const char *chemin, rejeu_t *r)
{
    memset(r, 0, sizeof(*r));

    FILE *f = fopen(chemin, "r");
    VERIFIER(f != NULL);
    if (f == NULL) {
        printf("  (fixture introuvable : %s)\n", chemin);
        return;
    }

    /* 3062 octets est la ligne la plus longue mesurée dans les fixtures
     * actuelles (l'instantané initial de connexion-idle.jsonl) -- marge
     * large pour une future fixture un peu plus verbeuse sans avoir à
     * retoucher ce chiffre à chaque enregistrement. */
    static char ligne[16384];
    while (fgets(ligne, sizeof(ligne), f) != NULL) {
        r->nb_lignes++;
        size_t len = strlen(ligne);
        while (len > 0 && (ligne[len - 1] == '\n' || ligne[len - 1] == '\r')) {
            ligne[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }

        cJSON *enveloppe = cJSON_Parse(ligne);
        VERIFIER(enveloppe != NULL);
        if (enveloppe == NULL) {
            continue;
        }

        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(enveloppe, "dir");
        const cJSON *raw = cJSON_GetObjectItemCaseSensitive(enveloppe, "raw");
        bool enveloppe_ok = cJSON_IsString(dir) && cJSON_IsString(raw) && raw->valuestring != NULL;
        VERIFIER(enveloppe_ok);

        if (enveloppe_ok && strcmp(dir->valuestring, "in") == 0) {
            r->nb_entrants++;
            const char *msg = raw->valuestring;
            size_t msg_len = strlen(msg);

            uint32_t id = 0;
            rpc_message_type_t type = rpc_classifier(msg, msg_len, &id);
            /* Coeur du critère de la tâche 4 : un message RÉELLEMENT émis
             * par Moonraker ne doit JAMAIS être classé invalide -- sinon
             * c'est soit un bug du classifieur face à une forme réelle
             * qu'on n'avait pas anticipée (à corriger), soit un
             * enregistreur qui a corrompu la capture (à diagnostiquer). */
            VERIFIER(type != RPC_MSG_INVALIDE);

            if (type == RPC_MSG_STATUS_UPDATE) {
                r->nb_status_update++;
                VERIFIER(rpc_fusionner_status(&r->etat, msg, msg_len));
            } else if (type == RPC_MSG_REPONSE) {
                r->nb_reponses++;
                if (id == FIXTURE_ID_ABONNEMENT) {
                    VERIFIER(rpc_fusionner_instantane(&r->etat, msg, msg_len));
                    r->instantane_applique = true;
                } else if (id == FIXTURE_ID_ACTION) {
                    snprintf(r->derniere_reponse_action, sizeof(r->derniere_reponse_action),
                             "%s", msg);
                    r->a_reponse_action = true;
                }
            } else if (type == RPC_MSG_AUTRE && strstr(msg, "notify_gcode_response") != NULL) {
                snprintf(r->dernier_gcode_response, sizeof(r->dernier_gcode_response), "%s", msg);
                r->a_gcode_response = true;
            }
        }

        cJSON_Delete(enveloppe);
    }

    fclose(f);
}

static void chemin_fixture(char *sortie, size_t taille, const char *nom)
{
    snprintf(sortie, taille, "%s/%s", KTOUCH_FIXTURES_MOONRAKER_DIR, nom);
}

/* --- connexion-idle.jsonl --------------------------------------------- */

static void section_connexion_idle(void)
{
    char chemin[512];
    chemin_fixture(chemin, sizeof(chemin), "connexion-idle.jsonl");

    rejeu_t r;
    rejeu_fixture(chemin, &r);

    VERIFIER(r.nb_lignes > 0);
    VERIFIER(r.nb_entrants > 0);
    VERIFIER(r.instantane_applique);
    /* Machine vkp fraîchement démarrée : aucun axe référencé (voir
     * "homed_axes":"" dans la fixture elle-même). */
    VERIFIER(r.etat.axes_references == 0);
    /* ~30 s d'écoute passive : Moonraker pousse des notify_status_update
     * (toolhead.estimated_print_time avance) et des notify_proc_stat_update
     * (RPC_MSG_AUTRE, ignoré) même sans la moindre action de notre part --
     * un vrai flux idle n'est jamais silencieux, contrairement à ce qu'un
     * test synthétique aurait pu supposer. */
    VERIFIER(r.nb_status_update > 0);
}

/* --- chauffe-buse.jsonl -------------------------------------------------
 *
 * Trouvaille de cette tâche (voir docs/dev/klipper-simule.md, section
 * "limite connue") : le MCU simulé de virtual-klipper-printer renvoie une
 * lecture ADC de thermistance CONSTANTE -- vérifié dans les logs bruts du
 * conteneur, `analog_in_state` porte EXACTEMENT les mêmes octets avant et
 * après la commande, sur plusieurs enregistrements et plusieurs redémarrages
 * du conteneur. Il n'y a donc AUCUN modèle thermique dans ce simulateur ;
 * `temperature` ne peut structurellement pas "monter". Ce fixture ne peut
 * donc PAS prouver "temperature > ambiante" (c'était l'attente initiale de
 * cette tâche) -- ce qu'il prouve réellement, et que ce test vérifie, c'est
 * que la CONSIGNE (`target`) est bien poussée par un vrai Moonraker suite à
 * une vraie commande `SET_HEATER_TEMPERATURE`, exactement comme
 * rpc_fusionner_status() l'attend. */
static void section_chauffe_buse(void)
{
    char chemin[512];
    chemin_fixture(chemin, sizeof(chemin), "chauffe-buse.jsonl");

    rejeu_t r;
    rejeu_fixture(chemin, &r);

    VERIFIER(r.nb_entrants > 0);
    VERIFIER(r.instantane_applique);
    VERIFIER(r.etat.extrudeurs[0].presente);
    VERIFIER_FLOAT(r.etat.extrudeurs[0].consigne, 60.0f, 0.01f);
    /* La temperature capturee (~103 C, un artefact du simulateur -- voir
     * commentaire ci-dessus) doit rester une valeur FINIE correctement
     * fusionnee, jamais poisonnee : c'est tout ce que ce champ peut
     * legitimement prouver ici. */
    VERIFIER(isfinite(r.etat.extrudeurs[0].actuelle));
}

/* --- macro-ok.jsonl ------------------------------------------------------
 * LOAD_FILAMENT : une macro RÉELLEMENT définie par virtual-klipper-printer
 * (example-configs/addons/basic_macros.cfg), choisie parce qu'elle ne fait
 * qu'un M117 (aucun mouvement, aucun effet de bord). */
static void section_macro_existante(void)
{
    char chemin[512];
    chemin_fixture(chemin, sizeof(chemin), "macro-ok.jsonl");

    rejeu_t r;
    rejeu_fixture(chemin, &r);

    VERIFIER(r.nb_entrants > 0);
    VERIFIER(r.instantane_applique);
    VERIFIER(r.a_reponse_action);

    bool succes = false;
    char erreur[128];
    VERIFIER(rpc_lire_reponse(r.derniere_reponse_action, strlen(r.derniere_reponse_action),
                               &succes, erreur, sizeof(erreur)));
    VERIFIER(succes == true);
    VERIFIER_TEXTE(erreur, "");
}

/* --- macro-inexistante.jsonl ----------------------------------------------
 *
 * TROUVAILLE CENTRALE de cette tâche (à reporter dans task-4-report.md) :
 * l'attente initiale ("macro-inexistante doit rendre succes=false avec un
 * texte d'erreur via rpc_lire_reponse") est FAUSSE face à un vrai Klipper.
 * Un vrai Klipper traite une commande étendue totalement inconnue
 * ("MACRO_QUI_NEXISTE_PAS") comme une commande INFO, PAS comme une erreur
 * fatale : la réponse JSON-RPC corrélée (id de l'action) est
 * `{"result":"ok"}`, EXACTEMENT comme pour une macro qui réussit -- voir
 * ci-dessous, `succes` est bien `true`. Le texte d'erreur réel
 * ("// Unknown command:\"MACRO_QUI_NEXISTE_PAS\"") n'arrive QUE de façon
 * asynchrone, via une notification `notify_gcode_response` séparée, que
 * rpc_classifier() range aujourd'hui dans RPC_MSG_AUTRE (reconnue mais
 * ignorée -- moonraker_rpc.c ne sait pas encore lire le texte d'une
 * réponse gcode). Conséquence pour la suite du jalon (écran macros, tâche
 * 6) : `rpc_lire_reponse()` sur la réponse corrélée à
 * `printer.gcode.script` NE PEUT PAS servir à détecter l'échec d'une macro
 * inconnue -- il faudrait une fonction dédiée pour `notify_gcode_response`,
 * qui n'existe pas encore. Ce test documente et fige ce comportement RÉEL
 * plutôt que d'imposer un résultat qu'aucun vrai Moonraker ne produit. */
static void section_macro_inexistante(void)
{
    char chemin[512];
    chemin_fixture(chemin, sizeof(chemin), "macro-inexistante.jsonl");

    rejeu_t r;
    rejeu_fixture(chemin, &r);

    VERIFIER(r.nb_entrants > 0);
    VERIFIER(r.instantane_applique);
    VERIFIER(r.a_reponse_action);

    bool succes = false;
    char erreur[128];
    VERIFIER(rpc_lire_reponse(r.derniere_reponse_action, strlen(r.derniere_reponse_action),
                               &succes, erreur, sizeof(erreur)));
    /* PAS une regression : c'est le vrai Moonraker qui repond "ok" ici,
     * voir le commentaire de tete de cette section. */
    VERIFIER(succes == true);

    /* L'erreur REELLE existe bel et bien -- juste pas la ou l'attente
     * initiale la cherchait. */
    VERIFIER(r.a_gcode_response);
    VERIFIER(strstr(r.dernier_gcode_response, "Unknown command") != NULL);
    VERIFIER(strstr(r.dernier_gcode_response, "MACRO_QUI_NEXISTE_PAS") != NULL);
}

void suite_fixtures_moonraker(void)
{
    printf("suite : rejeu des fixtures moonraker (virtual-klipper-printer reel)\n");
    section_connexion_idle();
    section_chauffe_buse();
    section_macro_existante();
    section_macro_inexistante();
}
