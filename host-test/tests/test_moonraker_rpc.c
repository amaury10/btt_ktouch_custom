#include <string.h>

#include "backend.h"
#include "klipper_fichiers.h"
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
    /* Fix round 1 (revue, C1 CRITIQUE) : la methode JSON-RPC Moonraker
     * s'ecrit avec des POINTS, jamais un '/' -- Moonraker derive ses
     * methodes par ".".join(...) et ne connait aucun alias HTTP-like.
     * Ce test PINAIT le bug avant le fix (il cherchait ".../subscribe") ;
     * il pin desormais la forme correcte. */
    VERIFIER(strstr(tampon, "\"method\":\"printer.objects.subscribe\"") != NULL);
    VERIFIER(strstr(tampon, "\"method\":\"printer.objects/subscribe\"") == NULL);
    VERIFIER(strstr(tampon, "\"id\":3") != NULL);
    VERIFIER(strstr(tampon, "\"toolhead\":null") != NULL);
    VERIFIER(strstr(tampon, "\"gcode_move\":null") != NULL);
    VERIFIER(strstr(tampon, "\"extruder\":null") != NULL);
    VERIFIER(strstr(tampon, "\"extruder7\":null") != NULL);
    /* Modules Bed Mesh + Input Shaper (spec 2026-08-15) : leurs objets
     * doivent etre abonnes, sinon leurs ecrans restent vides pour toujours
     * (meme piege que firmware_retraction, voir le commentaire plus haut). */
    VERIFIER(strstr(tampon,
                    "\"bed_mesh\":[\"profile_name\",\"mesh_min\",\"mesh_max\",\"probed_matrix\"]")
             != NULL);
    VERIFIER(strstr(tampon, "\"input_shaper\":null") != NULL);
    VERIFIER(strstr(tampon, "\"heater_bed\":null") != NULL);
    VERIFIER(strstr(tampon, "\"fan\":null") != NULL);
    VERIFIER(strstr(tampon, "\"print_stats\":null") != NULL);
    VERIFIER(strstr(tampon, "\"virtual_sdcard\":null") != NULL);
    VERIFIER(strstr(tampon, "\"webhooks\":null") != NULL);
    /* Tache 6 (panneau Retraction) : firmware_retraction, objet DISTINCT de
     * toolhead, doit etre explicitement ajoute a l'abonnement (contrairement
     * aux limite_*, portes par toolhead, deja abonne avant la tache 5). */
    VERIFIER(strstr(tampon, "\"firmware_retraction\":null") != NULL);

    /* tampon trop court => false */
    char petit[10];
    VERIFIER(!rpc_construire_abonnement(petit, sizeof(petit), 3));

    /* C8 : RPC_ABONNEMENT_TAILLE_MIN doit reellement suffire, id le plus
     * long possible (10 chiffres, UINT32_MAX) compris -- garde-fou reel
     * contre la taille de build publiee, pas une supposition. */
    char tampon_min[RPC_ABONNEMENT_TAILLE_MIN];
    VERIFIER(rpc_construire_abonnement(tampon_min, sizeof(tampon_min), UINT32_MAX));
    VERIFIER(strstr(tampon_min, "\"id\":4294967295") != NULL);
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

    /* C4 : notify_klippy_shutdown (MCU shutdown, thermal runaway...) est le
     * cas #1 en pratique -- meme classement que la deconnexion, puisque
     * dans les deux cas Klippy n'est plus utilisable et les
     * notify_status_update cessent d'arriver. */
    static const char *SHUTDOWN =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_klippy_shutdown\",\"params\":[]}";
    VERIFIER(rpc_classifier(SHUTDOWN, strlen(SHUTDOWN), NULL) == RPC_MSG_KLIPPY_DECONNECTE);

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

    /* C2 : id numerique mais hostile (probes de revue) -- CLAMPE a
     * [0, UINT32_MAX] plutot qu'UB sur la conversion double -> uint32_t.
     * Le message reste classe REPONSE : l'id existe et est un nombre, la
     * conversion se contente d'etre sure. */
    id = 999;
    static const char *ID_NEGATIF = "{\"result\":{},\"id\":-1}";
    VERIFIER(rpc_classifier(ID_NEGATIF, strlen(ID_NEGATIF), &id) == RPC_MSG_REPONSE);
    VERIFIER(id == 0);

    id = 999;
    static const char *ID_ENORME = "{\"result\":{},\"id\":1e300}";
    VERIFIER(rpc_classifier(ID_ENORME, strlen(ID_ENORME), &id) == RPC_MSG_REPONSE);
    VERIFIER(id == UINT32_MAX);

    id = 999;
    static const char *ID_DEUX_PUISSANCE_32 = "{\"result\":{},\"id\":4294967296}";
    VERIFIER(rpc_classifier(ID_DEUX_PUISSANCE_32, strlen(ID_DEUX_PUISSANCE_32), &id) == RPC_MSG_REPONSE);
    VERIFIER(id == UINT32_MAX);

    /* id non fini (1e400 devient +infini meme en double, cJSON accepte le
     * token JSON) : pas plus exploitable qu'un id absent => INVALIDE */
    id = 999;
    static const char *ID_INFINI = "{\"result\":{},\"id\":1e400}";
    VERIFIER(rpc_classifier(ID_INFINI, strlen(ID_INFINI), &id) == RPC_MSG_INVALIDE);
    VERIFIER(id == 0);

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

    /* --- input_shaper (module Input Shaper, spec 2026-08-15) : type + freq
     * par axe, fusion par champ (absent = inchange, meme politique que les
     * limite_*). --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"input_shaper\":{\"shaper_type_x\":\"mzv\",\"shaper_freq_x\":41.6,"
            "\"shaper_type_y\":\"ei\",\"shaper_freq_y\":52.2}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_TEXTE(e.shaper_type_x, "mzv");
        VERIFIER_TEXTE(e.shaper_type_y, "ei");
        VERIFIER_FLOAT(e.shaper_freq_x, 41.6f, 0.01f);
        VERIFIER_FLOAT(e.shaper_freq_y, 52.2f, 0.01f);

        /* Notify partiel : seul freq_x bouge, le reste reste. */
        enveloppe(msg, sizeof(msg), "{\"input_shaper\":{\"shaper_freq_x\":45.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.shaper_freq_x, 45.0f, 0.01f);
        VERIFIER_TEXTE(e.shaper_type_x, "mzv");
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

    /* --- tache 5 (panneau Limits) : max_velocity/max_accel/
     * square_corner_velocity/max_accel_to_decel, MEME objet toolhead, lus
     * exactement comme position/homed_axes ci-dessus (poison par champ) --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"toolhead\":{\"max_velocity\":300.0,\"max_accel\":5000.0,"
            "\"square_corner_velocity\":5.0,\"max_accel_to_decel\":2500.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.limite_velocity, 300.0f, 0.01f);
        VERIFIER_FLOAT(e.limite_accel, 5000.0f, 0.01f);
        VERIFIER_FLOAT(e.limite_square_corner, 5.0f, 0.01f);
        VERIFIER_FLOAT(e.limite_accel_to_decel, 2500.0f, 0.01f);

        /* Klipper recent : max_accel_to_decel deprecie, peut etre absent --
         * reste a 0 (pas ecrase par du bruit), le reste du meme objet
         * s'applique normalement (poison par CHAMP, pas par message). */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"toolhead\":{\"max_velocity\":300.0,\"max_accel\":5000.0,"
            "\"square_corner_velocity\":5.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.limite_velocity, 300.0f, 0.01f);
        VERIFIER(e.limite_accel_to_decel == 0.0f);

        /* champ non fini (1e40, +infini en float) : reste inchange, le reste
         * du meme objet continue d'etre applique -- meme piege que
         * extruder.temperature plus haut. */
        memset(&e, 0, sizeof(e));
        e.limite_accel = 42.0f;   /* valeur temoin : ne doit pas bouger */
        enveloppe(msg, sizeof(msg),
            "{\"toolhead\":{\"max_accel\":1e40,\"max_velocity\":300.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.limite_accel, 42.0f, 0.01f);
        VERIFIER_FLOAT(e.limite_velocity, 300.0f, 0.01f);
    }

    /* --- tache 6 (panneau Retraction) : firmware_retraction, objet DISTINCT
     * de toolhead (contrairement a limite_*, doit etre ajoute a
     * l'abonnement -- voir section_construire_abonnement() plus bas), meme
     * discipline nombre_fini()/poison-par-champ --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"firmware_retraction\":{\"retract_length\":1.5,\"retract_speed\":40.0,"
            "\"unretract_extra_length\":0.2,\"unretract_speed\":35.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.retr_length, 1.5f, 0.001f);
        VERIFIER_FLOAT(e.retr_speed, 40.0f, 0.01f);
        VERIFIER_FLOAT(e.retr_unretract_extra, 0.2f, 0.001f);
        VERIFIER_FLOAT(e.retr_unretract_speed, 35.0f, 0.01f);

        /* Machine sans [firmware_retraction] : Klipper renvoie `{}` pour cet
         * objet -- les quatre champs restent a 0, sans erreur de parsing. */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"firmware_retraction\":{}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.retr_length == 0.0f);
        VERIFIER(e.retr_speed == 0.0f);
        VERIFIER(e.retr_unretract_extra == 0.0f);
        VERIFIER(e.retr_unretract_speed == 0.0f);

        /* Champ manquant isole : reste a 0 (valeur temoin), le reste du meme
         * objet continue d'etre applique -- poison par CHAMP, pas par
         * message (meme piege que max_accel_to_decel ci-dessus). */
        memset(&e, 0, sizeof(e));
        e.retr_speed = 42.0f;   /* valeur temoin : ne doit pas bouger */
        enveloppe(msg, sizeof(msg),
            "{\"firmware_retraction\":{\"retract_length\":1.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.retr_length, 1.0f, 0.001f);
        VERIFIER_FLOAT(e.retr_speed, 42.0f, 0.01f);

        /* champ non fini (1e40, +infini en float) : reste inchange, le reste
         * du meme objet continue d'etre applique -- meme piege que
         * extruder.temperature/toolhead.max_accel plus haut. */
        memset(&e, 0, sizeof(e));
        e.retr_unretract_speed = 42.0f;   /* valeur temoin : ne doit pas bouger */
        enveloppe(msg, sizeof(msg),
            "{\"firmware_retraction\":{\"unretract_speed\":1e40,\"retract_length\":1.5}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER_FLOAT(e.retr_unretract_speed, 42.0f, 0.01f);
        VERIFIER_FLOAT(e.retr_length, 1.5f, 0.001f);
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

    /* --- C6 : arrondi pin -- 2 mutations avaient survecu sans ces cas --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"speed_factor\":1.03,\"extrude_factor\":1.07}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.vitesse_pct == 103);
        VERIFIER(e.flux_pct == 107);

        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"homing_origin\":[0,0,-0.0755,0]}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.babystep_z_um == -76);

        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"homing_origin\":[0,0,0.0755,0]}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.babystep_z_um == 76);
    }

    /* --- Fix round 2 (re-revue) : 1.03/1.07 ci-dessus ne DISCRIMINENT PAS
     * entre lire speed_factor/extrude_factor en float (retreci AVANT la
     * mise a l'echelle x100, le meme defaut que C6 avait diagnostique pour
     * babystep sans l'appliquer ici) et les lire en double -- les deux
     * chemins tombent par hasard sur le meme entier pour ces deux valeurs,
     * donc la suite restait verte au-dessus d'un vrai bug (M220 S104.5
     * affichait 104 au lieu de 105). Valeurs verifiees en Python AVANT
     * d'ecrire ce test (meme discipline que pour C6) :
     *   float32(1.045)*100+0.5 = 104.999995... -> 104   double : 105.0 -> 105
     *   float32(1.055)*100+0.5 = 105.999994... -> 105   double : 106.0 -> 106
     *   float32(0.905)*100+0.5 = 90.999997...  -> 90    double : 91.0  -> 91 --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"speed_factor\":1.045,\"extrude_factor\":1.055}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.vitesse_pct == 105);
        VERIFIER(e.flux_pct == 106);

        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"speed_factor\":0.905}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.vitesse_pct == 91);
    }

    /* --- C2 : conversions flottant -> entier bornees -- probes de revue qui
     * faisaient echouer UBSan (float-cast-overflow) avant le fix. Toutes ces
     * entrees sont FINIES (pas le cas "valeurs non finies" ci-dessous) :
     * elles doivent etre CLAMPEES a la borne du type cible, pas rejetees. --- */
    {
        etat_klipper_t e;

        /* speed_factor 1e30 : bien au-dela d'un uint16_t une fois x100 */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"speed_factor\":1e30}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.vitesse_pct == 65535);

        /* M220 S100000 : commande Klipper REELLE (pas une entree hostile),
         * speed_factor devient 1000.0 => 100000 % > UINT16_MAX */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"speed_factor\":1000.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.vitesse_pct == 65535);

        /* babystep : homing_origin[2] de 1e30 mm et 1e7 mm, tous deux finis
         * mais hors de la plage d'un int32_t une fois convertis en µm */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"homing_origin\":[0,0,1e30,0]}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.babystep_z_um == INT32_MAX);

        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"homing_origin\":[0,0,1e7,0]}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.babystep_z_um == INT32_MAX);

        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"gcode_move\":{\"homing_origin\":[0,0,-1e7,0]}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.babystep_z_um == INT32_MIN);
    }

    /* --- C5/probe 11 : un objet SANS AUCUN champ reconnu (Klipper repond
     * `{}` pour un objet interroge mais absent de la machine) ne doit PAS
     * flipper `presente` -- sinon une machine mono-extrudeur verrait
     * nb_extrudeurs gonfler des le premier instantane (extruder..extruder7
     * sont tous demandes par l'abonnement, qu'ils existent ou non). --- */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        e.extrudeurs[0].presente = true;
        e.nb_extrudeurs = 1;
        enveloppe(msg, sizeof(msg), "{\"extruder3\":{}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.extrudeurs[3].presente == false);
        VERIFIER(e.nb_extrudeurs == 1);

        /* meme garde pour le ventilateur */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg), "{\"fan\":{}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.ventilateurs[0].present == false);
    }

    /* --- C3 : temps_restant_s doit etre recalcule par la fusion WebSocket,
     * pas seulement par le sondage HTTP -- meme formule que
     * moonraker_parse.c (moonraker_estimer_temps_restant_s), memes cas
     * limites (nominal, progression trop faible, non fini, deja plafonne
     * cote moonraker_parse -- ici juste le nominal + les gardes cote fusion,
     * la formule elle-meme est deja exhaustivement testee par
     * test_moonraker_parse.c puisque c'est desormais la MEME fonction). --- */
    {
        etat_klipper_t e;

        /* nominal : print_duration et progression dans le MEME message,
         * 600 s ecoulees a 50 % => 600 s restantes (meme calcul que
         * test_moonraker_parse.c) */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"print_stats\":{\"print_duration\":600.0},\"virtual_sdcard\":{\"progress\":0.5}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.temps_restant_s == 600);

        /* print_duration arrive seul ; progression DEJA connue d'un message
         * anterieur (fusion partielle : la meilleure valeur connue de
         * `progression` est reutilisee, pas seulement celle du message
         * courant) */
        memset(&e, 0, sizeof(e));
        e.progression = 0.5f;
        enveloppe(msg, sizeof(msg), "{\"print_stats\":{\"print_duration\":600.0}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.temps_restant_s == 600);

        /* progression trop faible : estimation neutralisee (0), comme
         * moonraker_parse.c */
        memset(&e, 0, sizeof(e));
        enveloppe(msg, sizeof(msg),
            "{\"print_stats\":{\"print_duration\":5.0},\"virtual_sdcard\":{\"progress\":0.001}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.temps_restant_s == 0);

        /* print_duration non fini (1e40) : champ poison, temps_restant_s
         * reste a sa valeur COURANTE (pas remis a 0 ni recalcule) */
        memset(&e, 0, sizeof(e));
        e.temps_restant_s = 42;
        enveloppe(msg, sizeof(msg),
            "{\"print_stats\":{\"print_duration\":1e40},\"virtual_sdcard\":{\"progress\":0.5}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.temps_restant_s == 42);

        /* print_duration absent de CE message : temps_restant_s inchange,
         * meme si virtual_sdcard/progress est present (fusion partielle) */
        memset(&e, 0, sizeof(e));
        e.temps_restant_s = 123;
        enveloppe(msg, sizeof(msg), "{\"virtual_sdcard\":{\"progress\":0.5}}");
        VERIFIER(rpc_fusionner_status(&e, msg, strlen(msg)));
        VERIFIER(e.temps_restant_s == 123);
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

/* --- rpc_fusionner_instantane ---------------------------------------------
 * C5 : l'instantane initial arrive dans la REPONSE de printer.objects.
 * subscribe (result.status), pas dans params[0] -- rpc_fusionner_instantane
 * reutilise le meme moteur de fusion que rpc_fusionner_status (voir
 * fusionner_objet_statut dans moonraker_rpc.c), donc les memes regles
 * (partiel, poison par champ, presente-si-champ-reconnu, temps_restant_s)
 * s'appliquent -- ce test verifie surtout l'ENVELOPPE result.status et la
 * garde `{}` -> presente=false qui motive cette fonction. --- */

static void section_fusionner_instantane(void)
{
    /* nominal : plusieurs objets d'un coup, comme un vrai premier instantane */
    {
        etat_klipper_t e;
        memset(&e, 0, sizeof(e));
        static const char *REPONSE =
            "{\"result\":{\"status\":{"
            "\"toolhead\":{\"homed_axes\":\"xyz\",\"extruder\":\"extruder1\"},"
            "\"extruder\":{\"temperature\":25.0,\"target\":0.0},"
            "\"extruder1\":{\"temperature\":26.0,\"target\":210.0},"
            "\"extruder3\":{},"
            "\"heater_bed\":{\"temperature\":24.0,\"target\":60.0},"
            "\"print_stats\":{\"state\":\"printing\",\"filename\":\"x.gcode\",\"print_duration\":600.0},"
            "\"virtual_sdcard\":{\"progress\":0.5}"
            "}},\"id\":1}";
        VERIFIER(rpc_fusionner_instantane(&e, REPONSE, strlen(REPONSE)));
        VERIFIER(e.axes_references == 7);
        VERIFIER(e.outil_actif == 1);
        VERIFIER(e.extrudeurs[0].presente == true);
        VERIFIER(e.extrudeurs[1].presente == true);
        /* extruder3 : {} sans champ reconnu => n'existe pas sur cette machine */
        VERIFIER(e.extrudeurs[3].presente == false);
        VERIFIER(e.nb_extrudeurs == 2);
        VERIFIER(e.plateau.presente == true);
        VERIFIER_TEXTE(e.fichier, "x.gcode");
        VERIFIER(e.impression_en_cours == true);
        VERIFIER(e.temps_restant_s == 600);
    }

    /* fusion PARTIELLE, pas un ecrasement total : un champ non couvert par
     * l'instantane reste intact (meme contrat que rpc_fusionner_status) */
    {
        etat_klipper_t avant;
        memset(&avant, 0, sizeof(avant));
        avant.babystep_z_um = 500;
        avant.vitesse_pct = 120;

        etat_klipper_t e = avant;
        static const char *REPONSE = "{\"result\":{\"status\":{\"heater_bed\":{\"temperature\":61.0}}},\"id\":1}";
        VERIFIER(rpc_fusionner_instantane(&e, REPONSE, strlen(REPONSE)));
        VERIFIER(e.babystep_z_um == 500);
        VERIFIER(e.vitesse_pct == 120);
        VERIFIER_FLOAT(e.plateau.actuelle, 61.0f, 0.01f);
    }

    /* enveloppe hostile : result absent, result.status absent, result.status
     * pas un objet, JSON illisible => false, etat INTACT */
    {
        etat_klipper_t temoin;
        memset(&temoin, 0x5A, sizeof(temoin));
        etat_klipper_t e;

        e = temoin;
        static const char *SANS_RESULT = "{\"id\":1}";
        VERIFIER(!rpc_fusionner_instantane(&e, SANS_RESULT, strlen(SANS_RESULT)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        static const char *SANS_STATUS = "{\"result\":{\"eventtime\":1.0},\"id\":1}";
        VERIFIER(!rpc_fusionner_instantane(&e, SANS_STATUS, strlen(SANS_STATUS)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        static const char *STATUS_PAS_OBJET = "{\"result\":{\"status\":42},\"id\":1}";
        VERIFIER(!rpc_fusionner_instantane(&e, STATUS_PAS_OBJET, strlen(STATUS_PAS_OBJET)));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_instantane(&e, "pas du json", 11));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_instantane(&e, "", 0));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        e = temoin;
        VERIFIER(!rpc_fusionner_instantane(&e, NULL, 0));
        VERIFIER(memcmp(&e, &temoin, sizeof(e)) == 0);

        VERIFIER(!rpc_fusionner_instantane(NULL, "{}", 2));
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

    /* C7 : "error":null a cote d'un "result" valide ne doit JAMAIS se lire
     * comme un echec -- seul un "error" qui est un OBJET JSON compte. */
    succes = false;
    strcpy(erreur, "bruit");
    static const char *RESULT_ET_ERREUR_NULLE = "{\"result\":{\"foo\":1},\"error\":null,\"id\":6}";
    VERIFIER(rpc_lire_reponse(RESULT_ET_ERREUR_NULLE, strlen(RESULT_ET_ERREUR_NULLE),
                               &succes, erreur, sizeof(erreur)));
    VERIFIER(succes == true);
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

/* --- rpc_lire_fichiers ------------------------------------------------------
 * Meme structure que section_lire_macros() ci-dessus : rpc_lire_fichiers lit
 * `result` = tableau d'OBJETS (pas `result.objects` = tableau de chaines) et
 * prend `.path` de chacun -- voir task-1-brief.md et le commentaire de tete
 * dans moonraker_rpc.h. */

static void section_lire_fichiers(void)
{
    /* rpc_lire_fichiers ecrit desormais dans un klipper_fichiers_t (le store,
     * sorti de etat_klipper_t -- voir klipper_fichiers.h), pas dans l'etat. */

    /* liste nominale, 2 fichiers dont un dans un sous-dossier */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        static const char *LISTE =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":["
            "{\"path\":\"a.gcode\",\"modified\":1785605464.65,\"size\":10,\"permissions\":\"rw\"},"
            "{\"path\":\"sub/b.gcode\",\"modified\":1785605464.65,\"size\":20,\"permissions\":\"rw\"}"
            "]}";
        VERIFIER(rpc_lire_fichiers(&f, LISTE, strlen(LISTE)));
        VERIFIER(f.nb == 2);
        VERIFIER_TEXTE(f.noms[0], "a.gcode");
        VERIFIER_TEXTE(f.noms[1], "sub/b.gcode");
        VERIFIER(!f.tronques);
    }

    /* liste vide => nb == 0, true */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        f.nb = 7;   /* valeur temoin, doit etre remise a 0 */
        static const char *VIDE = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[]}";
        VERIFIER(rpc_lire_fichiers(&f, VIDE, strlen(VIDE)));
        VERIFIER(f.nb == 0);
        VERIFIER(!f.tronques);
    }

    /* result absent => false, dest INTACT (sentinelle nb=99) */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        f.nb = 99;
        static const char *SANS_RESULT = "{\"jsonrpc\":\"2.0\",\"id\":1}";
        VERIFIER(!rpc_lire_fichiers(&f, SANS_RESULT, strlen(SANS_RESULT)));
        VERIFIER(f.nb == 99);
    }

    /* result pas un tableau => false, dest INTACT */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        f.nb = 99;
        static const char *PAS_TABLEAU = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"a\":1}}";
        VERIFIER(!rpc_lire_fichiers(&f, PAS_TABLEAU, strlen(PAS_TABLEAU)));
        VERIFIER(f.nb == 99);
    }

    /* JSON illisible => false, dest INTACT (temoin 0x5A, memcmp du struct ENTIER) */
    {
        klipper_fichiers_t temoin;
        memset(&temoin, 0x5A, sizeof(temoin));
        klipper_fichiers_t f = temoin;
        VERIFIER(!rpc_lire_fichiers(&f, "pas du json", 11));
        VERIFIER(memcmp(&f, &temoin, sizeof(f)) == 0);

        f = temoin;
        VERIFIER(!rpc_lire_fichiers(&f, "", 0));
        VERIFIER(memcmp(&f, &temoin, sizeof(f)) == 0);

        f = temoin;
        VERIFIER(!rpc_lire_fichiers(&f, NULL, 0));
        VERIFIER(memcmp(&f, &temoin, sizeof(f)) == 0);

        VERIFIER(!rpc_lire_fichiers(NULL, "{}", 2));
    }

    /* .path de KLIPPER_FICHIER_MAX caracteres ou plus : IGNORE (pas tronque),
     * ne compte pas dans nb, le reste de la liste continue */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        /* 70 'A' + ".gcode" : bien >= KLIPPER_FICHIER_MAX (64) */
        char chemin_long[80];
        memset(chemin_long, 'A', 70);
        strcpy(chemin_long + 70, ".gcode");

        char liste[256];
        snprintf(liste, sizeof(liste),
                 "{\"result\":[{\"path\":\"%s\"},{\"path\":\"courte.gcode\"}]}", chemin_long);
        VERIFIER(rpc_lire_fichiers(&f, liste, strlen(liste)));
        VERIFIER(f.nb == 1);
        VERIFIER_TEXTE(f.noms[0], "courte.gcode");
        VERIFIER(!f.tronques);
    }

    /* element du tableau qui n'est pas un objet, ou objet sans .path chaine :
     * ignore, le reste continue */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        static const char *POISON =
            "{\"result\":["
            "42,"
            "\"pas un objet\","
            "{\"size\":10},"
            "{\"path\":123},"
            "{\"path\":\"valide.gcode\"}"
            "]}";
        VERIFIER(rpc_lire_fichiers(&f, POISON, strlen(POISON)));
        VERIFIER(f.nb == 1);
        VERIFIER_TEXTE(f.noms[0], "valide.gcode");
    }

    /* > KLIPPER_FICHIERS_MAX entrees => nb == MAX, tronques == true */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));

        char liste[4096];
        size_t pos = (size_t)snprintf(liste, sizeof(liste), "{\"result\":[");
        for (int i = 0; i < 40; i++) {
            pos += (size_t)snprintf(liste + pos, sizeof(liste) - pos,
                                     "%s{\"path\":\"f%d.gcode\"}", i == 0 ? "" : ",", i);
        }
        pos += (size_t)snprintf(liste + pos, sizeof(liste) - pos, "]}");
        VERIFIER(pos < sizeof(liste));

        VERIFIER(rpc_lire_fichiers(&f, liste, pos));
        VERIFIER(f.nb == KLIPPER_FICHIERS_MAX);
        VERIFIER(f.tronques);
    }

    /* remplace ENTIEREMENT la liste precedente (instantane complet, pas une
     * fusion) */
    {
        klipper_fichiers_t f;
        memset(&f, 0, sizeof(f));
        f.nb = 3;
        strcpy(f.noms[0], "vieux.gcode");

        static const char *LISTE = "{\"result\":[{\"path\":\"neuf.gcode\"}]}";
        VERIFIER(rpc_lire_fichiers(&f, LISTE, strlen(LISTE)));
        VERIFIER(f.nb == 1);
        VERIFIER_TEXTE(f.noms[0], "neuf.gcode");
    }
}

/* --- rpc_lire_miniature (feature "Miniatures gcode", tache A) -------------
 * `json` porte le message COMPLET (avec enveloppe "result"), meme convention
 * que rpc_lire_fichiers() ci-dessus -- voir moonraker_rpc.h. */

static void section_lire_miniature(void)
{
    /* nominal : une seule miniature, largeur dans la limite */
    {
        char chemin[64];
        int w = -1, h = -1;
        static const char *UNE =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"thumbnails\":["
            "{\"width\":300,\"height\":300,\"size\":1234,\"relative_path\":\".thumbs/piece-300x300.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, UNE, strlen(UNE), 400));
        VERIFIER_TEXTE(chemin, ".thumbs/piece-300x300.png");
        VERIFIER(w == 300);
        VERIFIER(h == 300);
    }

    /* N miniatures : choisit la plus GRANDE <= largeur_max, pas la premiere
     * ni la derniere du tableau */
    {
        char chemin[64];
        int w = -1, h = -1;
        static const char *TROIS =
            "{\"result\":{\"thumbnails\":["
            "{\"width\":100,\"height\":100,\"relative_path\":\"p100.png\"},"
            "{\"width\":400,\"height\":400,\"relative_path\":\"p400.png\"},"
            "{\"width\":200,\"height\":200,\"relative_path\":\"p200.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, TROIS, strlen(TROIS), 250));
        VERIFIER_TEXTE(chemin, "p200.png");
        VERIFIER(w == 200);
        VERIFIER(h == 200);
    }

    /* repli sur la plus PETITE quand aucune n'est <= largeur_max */
    {
        char chemin[64];
        int w = -1, h = -1;
        static const char *TROIS =
            "{\"result\":{\"thumbnails\":["
            "{\"width\":300,\"height\":300,\"relative_path\":\"p300.png\"},"
            "{\"width\":500,\"height\":500,\"relative_path\":\"p500.png\"},"
            "{\"width\":400,\"height\":400,\"relative_path\":\"p400.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, TROIS, strlen(TROIS), 100));
        VERIFIER_TEXTE(chemin, "p300.png");
        VERIFIER(w == 300);
    }

    /* tailles egales <= max : le PREMIER rencontre l'emporte */
    {
        char chemin[64];
        int w = -1, h = -1;
        static const char *EGALES =
            "{\"result\":{\"thumbnails\":["
            "{\"width\":200,\"height\":200,\"relative_path\":\"a.png\"},"
            "{\"width\":200,\"height\":200,\"relative_path\":\"b.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, EGALES, strlen(EGALES), 300));
        VERIFIER_TEXTE(chemin, "a.png");
    }

    /* tailles egales, toutes au-dela de largeur_max (repli) : le PREMIER
     * rencontre l'emporte aussi pour le repli */
    {
        char chemin[64];
        int w = -1, h = -1;
        static const char *EGALES =
            "{\"result\":{\"thumbnails\":["
            "{\"width\":200,\"height\":200,\"relative_path\":\"a.png\"},"
            "{\"width\":200,\"height\":200,\"relative_path\":\"b.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, EGALES, strlen(EGALES), 50));
        VERIFIER_TEXTE(chemin, "a.png");
    }

    /* element malforme au milieu du tableau : ignore, le reste continue
     * d'etre examine (poison par element, meme esprit que
     * traiter_candidat_fichier()) */
    {
        char chemin[64];
        int w = -1, h = -1;
        static const char *POISON =
            "{\"result\":{\"thumbnails\":["
            "42,"
            "\"pas un objet\","
            "{\"height\":100,\"relative_path\":\"sans-largeur.png\"},"
            "{\"width\":0,\"height\":100,\"relative_path\":\"largeur-nulle.png\"},"
            "{\"width\":-50,\"height\":100,\"relative_path\":\"largeur-negative.png\"},"
            "{\"width\":100,\"height\":100,\"relative_path\":\"\"},"
            "{\"width\":150,\"height\":150,\"relative_path\":\"bonne.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, POISON, strlen(POISON), 400));
        VERIFIER_TEXTE(chemin, "bonne.png");
        VERIFIER(w == 150);
        VERIFIER(h == 150);
    }

    /* chemin_dest trop petit : tronque proprement, rend quand meme true
     * (contrairement a rpc_lire_fichiers ou un nom trop long est ignore --
     * ici un simple segment d'URL, voir moonraker_rpc.h) */
    {
        char petit[6];
        int w = -1, h = -1;
        static const char *LONG =
            "{\"result\":{\"thumbnails\":["
            "{\"width\":300,\"height\":300,\"relative_path\":\".thumbs/nom-bien-plus-long-que-le-tampon.png\"}"
            "]}}";
        VERIFIER(rpc_lire_miniature(petit, sizeof(petit), &w, &h, LONG, strlen(LONG), 400));
        VERIFIER(strlen(petit) <= 5);
        VERIFIER_TEXTE(petit, ".thum");
        VERIFIER(w == 300);
    }

    /* 0 miniature : tableau vide => false, sorties INTACTES */
    {
        char chemin[64];
        strcpy(chemin, "sentinelle");
        int w = -1, h = -2;
        static const char *VIDE = "{\"result\":{\"thumbnails\":[]}}";
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, VIDE, strlen(VIDE), 400));
        VERIFIER_TEXTE(chemin, "sentinelle");
        VERIFIER(w == -1);
        VERIFIER(h == -2);
    }

    /* thumbnails absent => false */
    {
        char chemin[64];
        strcpy(chemin, "sentinelle");
        int w = -1, h = -2;
        static const char *SANS_THUMBS = "{\"result\":{\"size\":10}}";
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, SANS_THUMBS, strlen(SANS_THUMBS), 400));
        VERIFIER_TEXTE(chemin, "sentinelle");
        VERIFIER(w == -1);
        VERIFIER(h == -2);
    }

    /* thumbnails pas un tableau => false */
    {
        char chemin[64];
        strcpy(chemin, "sentinelle");
        int w = -1, h = -2;
        static const char *PAS_TABLEAU = "{\"result\":{\"thumbnails\":{\"a\":1}}}";
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, PAS_TABLEAU, strlen(PAS_TABLEAU), 400));
        VERIFIER_TEXTE(chemin, "sentinelle");
    }

    /* result absent => false */
    {
        char chemin[64];
        strcpy(chemin, "sentinelle");
        int w = -1, h = -2;
        static const char *SANS_RESULT = "{\"id\":1}";
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, SANS_RESULT, strlen(SANS_RESULT), 400));
        VERIFIER_TEXTE(chemin, "sentinelle");
    }

    /* tous les elements malformes => false, sorties intactes */
    {
        char chemin[64];
        strcpy(chemin, "sentinelle");
        int w = -1, h = -2;
        static const char *TOUT_POISON =
            "{\"result\":{\"thumbnails\":[42,\"x\",{\"width\":0,\"relative_path\":\"a.png\"}]}}";
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, TOUT_POISON, strlen(TOUT_POISON), 400));
        VERIFIER_TEXTE(chemin, "sentinelle");
        VERIFIER(w == -1);
        VERIFIER(h == -2);
    }

    /* JSON illisible => false */
    {
        char chemin[64];
        strcpy(chemin, "sentinelle");
        int w = -1, h = -2;
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, "pas du json", 11, 400));
        VERIFIER_TEXTE(chemin, "sentinelle");
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, "", 0, 400));
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, &h, NULL, 0, 400));
    }

    /* entrees NULL / tailles nulles => false, jamais de dereferencement */
    {
        char chemin[64];
        int w, h;
        static const char *UNE =
            "{\"result\":{\"thumbnails\":[{\"width\":100,\"height\":100,\"relative_path\":\"a.png\"}]}}";
        VERIFIER(!rpc_lire_miniature(NULL, sizeof(chemin), &w, &h, UNE, strlen(UNE), 400));
        VERIFIER(!rpc_lire_miniature(chemin, 0, &w, &h, UNE, strlen(UNE), 400));
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), NULL, &h, UNE, strlen(UNE), 400));
        VERIFIER(!rpc_lire_miniature(chemin, sizeof(chemin), &w, NULL, UNE, strlen(UNE), 400));
    }
}

/* --- miniature_construire_chemin (feature "Miniatures gcode", tache A) ---- */

static void section_construire_chemin_miniature(void)
{
    /* fichier a la racine "gcodes" (pas de '/') : resultat = relative_path
     * tel quel, aucun prefixe */
    {
        char dest[64];
        size_t r = miniature_construire_chemin(dest, sizeof(dest), "piece.gcode", ".thumbs/piece.png");
        VERIFIER(r == strlen(".thumbs/piece.png"));
        VERIFIER_TEXTE(dest, ".thumbs/piece.png");
    }

    /* sous-dossier simple */
    {
        char dest[64];
        size_t r = miniature_construire_chemin(dest, sizeof(dest), "dir/foo.gcode", ".thumbs/foo.png");
        VERIFIER(r == strlen("dir/.thumbs/foo.png"));
        VERIFIER_TEXTE(dest, "dir/.thumbs/foo.png");
    }

    /* sous-dossier imbrique : seul ce qui precede le DERNIER '/' est gardé */
    {
        char dest[64];
        size_t r = miniature_construire_chemin(dest, sizeof(dest), "a/b/foo.gcode", ".thumbs/foo.png");
        VERIFIER(r == strlen("a/b/.thumbs/foo.png"));
        VERIFIER_TEXTE(dest, "a/b/.thumbs/foo.png");
    }

    /* relative_path avec '/' en tete : pas de cas particulier, simple
     * concatenation (voir moonraker_rpc.h -- Moonraker cote serveur
     * interprete l'URL, cette fonction ne fait que joindre deux segments) */
    {
        char dest[64];
        size_t r = miniature_construire_chemin(dest, sizeof(dest), "dir/foo.gcode", "/abs/thumb.png");
        VERIFIER_TEXTE(dest, "dir//abs/thumb.png");
        VERIFIER(r == strlen("dir//abs/thumb.png"));
    }

    /* gcode_chemin/relative_path NULL : traites comme des chaines vides,
     * jamais de dereferencement NULL */
    {
        char dest[64];
        size_t r = miniature_construire_chemin(dest, sizeof(dest), NULL, "x.png");
        VERIFIER_TEXTE(dest, "x.png");
        VERIFIER(r == strlen("x.png"));

        r = miniature_construire_chemin(dest, sizeof(dest), "dir/foo.gcode", NULL);
        VERIFIER_TEXTE(dest, "dir/");
        VERIFIER(r == strlen("dir/"));
    }

    /* troncature : contrat a la snprintf() -- le retour est la longueur
     * COMPLETE qui aurait ete ecrite, meme si `dest` est trop petit pour la
     * recevoir ; `dest` reste correctement termine par un octet nul dans les
     * limites de `n`. */
    {
        char petit[8];
        size_t r = miniature_construire_chemin(petit, sizeof(petit), "dir/foo.gcode", ".thumbs/foo-300x300.png");
        static const char *COMPLET = "dir/.thumbs/foo-300x300.png";
        VERIFIER(r == strlen(COMPLET));
        VERIFIER(r >= sizeof(petit));   /* signale bien une troncature */
        VERIFIER(strlen(petit) == sizeof(petit) - 1);
        VERIFIER(strncmp(petit, COMPLET, sizeof(petit) - 1) == 0);
    }

    /* n == 0, dest == NULL : calcule seulement la longueur, n'ecrit rien --
     * ne doit jamais planter (meme regle que snprintf() : s peut etre NULL
     * quand n vaut 0). */
    {
        size_t r = miniature_construire_chemin(NULL, 0, "dir/foo.gcode", ".thumbs/foo.png");
        VERIFIER(r == strlen("dir/.thumbs/foo.png"));
    }
}

/* --- rpc_lire_gcode_response (feature Console gcode, tache A) -------------
 * notify_gcode_response : params[0] est directement une STRING -- piege
 * distinct de rpc_fusionner_status (params[0] objet de statut) et de
 * rpc_lire_power_changed (params[0] tableau). */

static void section_lire_gcode_response(void)
{
    /* string simple */
    {
        char dest[128];
        static const char *MSG =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\","
            "\"params\":[\"// echo: G28\"]}";
        VERIFIER(rpc_lire_gcode_response(dest, sizeof(dest), MSG, strlen(MSG)));
        VERIFIER_TEXTE(dest, "// echo: G28");
    }

    /* reponse multi-lignes (separees par \n) : recopiee telle quelle, c'est
     * a l'appelant (moonraker_ws.c, tache B) de la decouper avant
     * console_log_ajouter() -- cette fonction ne fait QUE l'extraction. */
    {
        char dest[128];
        static const char *MSG =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\","
            "\"params\":[\"ligne1\\nligne2\\nligne3\"]}";
        VERIFIER(rpc_lire_gcode_response(dest, sizeof(dest), MSG, strlen(MSG)));
        VERIFIER_TEXTE(dest, "ligne1\nligne2\nligne3");
    }

    /* tampon trop court : troncature UTF-8-safe, PAS un echec (contrairement
     * a rpc_lire_methode ci-dessous) -- une reponse Klipper tronquee reste
     * plus utile qu'une reponse perdue. */
    {
        char petit[6];
        static const char *MSG =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\","
            "\"params\":[\"un texte bien plus long que le tampon\"]}";
        VERIFIER(rpc_lire_gcode_response(petit, sizeof(petit), MSG, strlen(MSG)));
        VERIFIER(strlen(petit) <= 5);
        VERIFIER_TEXTE(petit, "un te");
    }

    /* params absent => false, dest INTACT */
    {
        char dest[32];
        strcpy(dest, "sentinelle");
        static const char *SANS_PARAMS =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\"}";
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), SANS_PARAMS, strlen(SANS_PARAMS)));
        VERIFIER_TEXTE(dest, "sentinelle");
    }

    /* params[0] n'est pas une string (ex. objet de statut, comme
     * notify_status_update) => false */
    {
        char dest[32];
        strcpy(dest, "sentinelle");
        static const char *PARAMS0_OBJET =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":[{\"a\":1}]}";
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), PARAMS0_OBJET, strlen(PARAMS0_OBJET)));
        VERIFIER_TEXTE(dest, "sentinelle");
    }

    /* params n'est pas un tableau => false */
    {
        char dest[32];
        strcpy(dest, "sentinelle");
        static const char *PARAMS_OBJET =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":{\"a\":1}}";
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), PARAMS_OBJET, strlen(PARAMS_OBJET)));
        VERIFIER_TEXTE(dest, "sentinelle");
    }

    /* params vide => false (params[0] absent) */
    {
        char dest[32];
        strcpy(dest, "sentinelle");
        static const char *PARAMS_VIDE =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":[]}";
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), PARAMS_VIDE, strlen(PARAMS_VIDE)));
        VERIFIER_TEXTE(dest, "sentinelle");
    }

    /* JSON illisible / entrees NULL => false */
    {
        char dest[32];
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), "pas du json", 11));
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), "", 0));
        VERIFIER(!rpc_lire_gcode_response(dest, sizeof(dest), NULL, 0));
        VERIFIER(!rpc_lire_gcode_response(NULL, sizeof(dest), "{}", 2));
        VERIFIER(!rpc_lire_gcode_response(dest, 0, "{}", 2));
    }
}

/* --- rpc_lire_methode (feature Console gcode, tache A) --------------------
 * Extrait le champ "method" tel quel -- pour le dispatch par nom de methode
 * de la tache B (voir le commentaire de tete de moonraker_rpc.h : aucune
 * fonction existante n'exposait deja cette chaine, rpc_classifier() la lit
 * en interne mais ne la rend jamais a l'appelant). */

static void section_lire_methode(void)
{
    /* methode presente */
    {
        char dest[64];
        static const char *MSG =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":[\"ok\"]}";
        VERIFIER(rpc_lire_methode(dest, sizeof(dest), MSG, strlen(MSG)));
        VERIFIER_TEXTE(dest, "notify_gcode_response");
    }

    /* methode presente, message par ailleurs une REPONSE (result/id) : cette
     * fonction ne se soucie que du champ "method", absent ici => false */
    {
        char dest[64];
        strcpy(dest, "sentinelle");
        static const char *REPONSE = "{\"result\":{},\"id\":5}";
        VERIFIER(!rpc_lire_methode(dest, sizeof(dest), REPONSE, strlen(REPONSE)));
        VERIFIER_TEXTE(dest, "sentinelle");
    }

    /* "method" n'est pas une string => false */
    {
        char dest[64];
        strcpy(dest, "sentinelle");
        static const char *METHODE_NOMBRE = "{\"method\":42}";
        VERIFIER(!rpc_lire_methode(dest, sizeof(dest), METHODE_NOMBRE, strlen(METHODE_NOMBRE)));
        VERIFIER_TEXTE(dest, "sentinelle");
    }

    /* tampon trop court : false, JAMAIS de methode tronquee rendue comme
     * valide (contrairement a rpc_lire_gcode_response ci-dessus) -- une
     * methode tronquee pourrait se lire comme le prefixe d'une AUTRE methode
     * connue et fausser le dispatch de l'appelant. */
    {
        char petit[16];
        strcpy(petit, "sentinelle");
        static const char *MSG =
            "{\"jsonrpc\":\"2.0\",\"method\":\"notify_gcode_response\",\"params\":[\"ok\"]}";
        VERIFIER(!rpc_lire_methode(petit, sizeof(petit), MSG, strlen(MSG)));
        VERIFIER_TEXTE(petit, "sentinelle");
    }

    /* JSON illisible / entrees NULL => false */
    {
        char dest[32];
        VERIFIER(!rpc_lire_methode(dest, sizeof(dest), "pas du json", 11));
        VERIFIER(!rpc_lire_methode(dest, sizeof(dest), "", 0));
        VERIFIER(!rpc_lire_methode(dest, sizeof(dest), NULL, 0));
        VERIFIER(!rpc_lire_methode(NULL, sizeof(dest), "{}", 2));
        VERIFIER(!rpc_lire_methode(dest, 0, "{}", 2));
    }
}

/* --- rpc_methode_commande (tache 5) -------------------------------------- */

static void section_methode_commande(void)
{
    char methode[RPC_METHODE_COMMANDE_MAX];

    /* Les quatre actions connues -- meme couverture que
     * section_moonraker_chemin_commande() dans test_commandes.c pour le
     * pendant HTTP, methodes JSON-RPC (points) au lieu de chemins REST. */
    memset(methode, 0, sizeof(methode));
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_PAUSE, methode, sizeof(methode)) == true);
    VERIFIER_TEXTE(methode, "printer.print.pause");

    memset(methode, 0, sizeof(methode));
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_REPRENDRE, methode, sizeof(methode)) == true);
    VERIFIER_TEXTE(methode, "printer.print.resume");

    memset(methode, 0, sizeof(methode));
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_ANNULER, methode, sizeof(methode)) == true);
    VERIFIER_TEXTE(methode, "printer.print.cancel");

    memset(methode, 0, sizeof(methode));
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_URGENCE, methode, sizeof(methode)) == true);
    VERIFIER_TEXTE(methode, "printer.emergency_stop");

    /* Jamais un '/' -- meme piege que rpc_construire_abonnement(), voir son
     * commentaire de tete. */
    VERIFIER(strchr(methode, '/') == NULL);

    /* Action inconnue (BACKEND_ACTION_MACRO inclus -- support WS laisse a la
     * tache 6) : false, `methode` intact. */
    snprintf(methode, sizeof(methode), "sentinelle");
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_MACRO, methode, sizeof(methode)) == false);
    VERIFIER_TEXTE(methode, "sentinelle");

    snprintf(methode, sizeof(methode), "sentinelle");
    VERIFIER(rpc_methode_commande("action_inexistante", methode, sizeof(methode)) == false);
    VERIFIER_TEXTE(methode, "sentinelle");

    /* action/methode NULL, taille nulle : false, jamais un dereferencement. */
    VERIFIER(rpc_methode_commande(NULL, methode, sizeof(methode)) == false);
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_PAUSE, NULL, sizeof(methode)) == false);
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_PAUSE, methode, 0) == false);

    /* Tampon trop court : false, jamais une methode tronquee rendue comme
     * valide -- "printer.print.pause" fait 20 caracteres, 8 octets tronquent
     * a coup sur. */
    char court[8];
    VERIFIER(rpc_methode_commande(BACKEND_ACTION_PAUSE, court, sizeof(court)) == false);
}

/* --- profils bed_mesh (liste de profils, 2026-08-15) --------------------- */

static void section_profils_bed_mesh(void)
{
    /* Enveloppe nominale d'une reponse printer.objects.query : les matrices
       que chaque profil porte sont ignorees, seules les CLES comptent. */
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{\"eventtime\":123.4,\"status\":{\"bed_mesh\":"
        "{\"profiles\":{\"default\":{\"points\":[[0.1,0.2]],\"mesh_params\":{}},"
        "\"textured\":{\"points\":[[0.0]]}}}}},\"id\":7}";
    bed_mesh_profils_t profils;
    memset(&profils, 0xFF, sizeof(profils));
    VERIFIER(rpc_lire_profils_bed_mesh(&profils, json, strlen(json)));
    VERIFIER(profils.nb == 2);
    VERIFIER(!profils.tronques);
    VERIFIER_TEXTE(profils.noms[0], "default");
    VERIFIER_TEXTE(profils.noms[1], "textured");

    /* Zero profil sauvegarde : objet vide, liste vide, pas un echec. */
    const char *vide =
        "{\"jsonrpc\":\"2.0\",\"result\":{\"status\":{\"bed_mesh\":{\"profiles\":{}}}},\"id\":8}";
    VERIFIER(rpc_lire_profils_bed_mesh(&profils, vide, strlen(vide)));
    VERIFIER(profils.nb == 0);

    /* Nom trop long pour BED_MESH_PROFIL_NOM_MAX : ecarte + drapeau, les
       autres survivent. */
    const char *long_nom =
        "{\"result\":{\"status\":{\"bed_mesh\":{\"profiles\":{"
        "\"nom_vraiment_beaucoup_trop_long_pour_le_store\":{},\"ok\":{}}}}},\"id\":9}";
    VERIFIER(rpc_lire_profils_bed_mesh(&profils, long_nom, strlen(long_nom)));
    VERIFIER(profils.nb == 1);
    VERIFIER(profils.tronques);
    VERIFIER_TEXTE(profils.noms[0], "ok");

    /* Enveloppe sans profiles : false, dest INTACT. */
    profils.nb = 42;
    const char *sans = "{\"result\":{\"status\":{\"bed_mesh\":{}}},\"id\":10}";
    VERIFIER(!rpc_lire_profils_bed_mesh(&profils, sans, strlen(sans)));
    VERIFIER(profils.nb == 42);
    VERIFIER(!rpc_lire_profils_bed_mesh(&profils, NULL, 4));
    VERIFIER(!rpc_lire_profils_bed_mesh(NULL, sans, strlen(sans)));
}

/* --- Spoolman (spec 2026-08-15-spoolman-design.md) ----------------------- */

static void section_spoolman(void)
{
    /* Forme RELEVEE sur la machine reelle (Spoolman v0.26.1 via le proxy
       Moonraker), pas inventee d'apres la doc. */
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":[{"
        "\"id\":1,\"registered\":\"2026-08-15T16:42:02Z\","
        "\"filament\":{\"id\":1,\"name\":\"Galaxy Black\","
        "\"vendor\":{\"id\":1,\"name\":\"Prusament\"},"
        "\"material\":\"PETG\",\"density\":1.27,\"diameter\":1.75,"
        "\"weight\":1000.0,\"color_hex\":\"1A1A2E\"},"
        "\"remaining_weight\":820.5,\"used_weight\":179.5,"
        "\"location\":\"Etagere A\",\"archived\":false}],\"id\":9}";

    spoolman_liste_t liste;
    memset(&liste, 0xFF, sizeof(liste));
    VERIFIER(rpc_lire_spoolman_liste(&liste, json, strlen(json)));
    VERIFIER(liste.nb == 1);
    VERIFIER(liste.connue);
    VERIFIER(!liste.tronquee);
    VERIFIER(liste.bobines[0].id == 1);
    VERIFIER_TEXTE(liste.bobines[0].filament, "Galaxy Black");
    VERIFIER_TEXTE(liste.bobines[0].fabricant, "Prusament");
    VERIFIER_TEXTE(liste.bobines[0].matiere, "PETG");
    VERIFIER(liste.bobines[0].couleur_connue);
    VERIFIER(liste.bobines[0].couleur == 0x1A1A2Eu);
    VERIFIER(liste.bobines[0].restant_connu);
    VERIFIER(liste.bobines[0].restant_g > 820.4f && liste.bobines[0].restant_g < 820.6f);
    VERIFIER(liste.bobines[0].total_g > 999.9f && liste.bobines[0].total_g < 1000.1f);

    /* Bobine archivee IGNOREE ; bobine sans remaining_weight gardee mais
       marquee inconnue ; couleur invalide -> inconnue, jamais inventee ;
       couleur RRGGBBAA -> alpha ignore. */
    const char *melange =
        "{\"result\":["
        "{\"id\":2,\"archived\":true,\"filament\":{\"name\":\"Archivee\"}},"
        "{\"id\":3,\"filament\":{\"name\":\"SansPoids\",\"color_hex\":\"zzz\"}},"
        "{\"id\":4,\"filament\":{\"name\":\"Alpha\",\"color_hex\":\"11223344\"},"
        "\"remaining_weight\":100.0},"
        "{\"filament\":{\"name\":\"SansId\"}}"
        "]}";
    VERIFIER(rpc_lire_spoolman_liste(&liste, melange, strlen(melange)));
    VERIFIER(liste.nb == 2); /* l'archivee ET la sans-id sont ecartees */
    VERIFIER(liste.bobines[0].id == 3);
    VERIFIER(!liste.bobines[0].restant_connu);
    VERIFIER(!liste.bobines[0].couleur_connue); /* "zzz" n'est pas une couleur */
    VERIFIER(liste.bobines[1].id == 4);
    VERIFIER(liste.bobines[1].couleur_connue);
    VERIFIER(liste.bobines[1].couleur == 0x112233u); /* l'alpha 0x44 est ignore */

    /* Au-dela de SPOOLMAN_BOBINES_MAX : troncature SIGNALEE. */
    char gros[4096];
    size_t pos = 0;
    pos += (size_t)snprintf(gros + pos, sizeof(gros) - pos, "{\"result\":[");
    for (int i = 0; i < SPOOLMAN_BOBINES_MAX + 3; i++) {
        pos += (size_t)snprintf(gros + pos, sizeof(gros) - pos,
                                "%s{\"id\":%d,\"filament\":{\"name\":\"B%d\"}}",
                                i ? "," : "", i + 1, i + 1);
    }
    (void)snprintf(gros + pos, sizeof(gros) - pos, "]}");
    VERIFIER(rpc_lire_spoolman_liste(&liste, gros, strlen(gros)));
    VERIFIER(liste.nb == SPOOLMAN_BOBINES_MAX);
    VERIFIER(liste.tronquee);

    /* Enveloppes cassees : false, destination INTACTE. */
    liste.nb = 42;
    const char *pas_un_tableau = "{\"result\":{\"oops\":1}}";
    VERIFIER(!rpc_lire_spoolman_liste(&liste, pas_un_tableau, strlen(pas_un_tableau)));
    VERIFIER(liste.nb == 42);
    VERIFIER(!rpc_lire_spoolman_liste(&liste, "pas du json", 11));
    VERIFIER(!rpc_lire_spoolman_liste(NULL, json, strlen(json)));

    /* --- statut --- */
    spoolman_etat_t etat;
    const char *statut =
        "{\"result\":{\"spoolman_connected\":true,\"pending_reports\":[],\"spool_id\":7}}";
    VERIFIER(rpc_lire_spoolman_statut(&etat, statut, strlen(statut)));
    VERIFIER(etat.connecte);
    VERIFIER(etat.statut_connu);
    VERIFIER(etat.id_actif == 7);

    const char *aucune =
        "{\"result\":{\"spoolman_connected\":false,\"spool_id\":null}}";
    VERIFIER(rpc_lire_spoolman_statut(&etat, aucune, strlen(aucune)));
    VERIFIER(!etat.connecte);
    VERIFIER(etat.id_actif == SPOOLMAN_AUCUNE_BOBINE); /* null, PAS 0 */

    etat.id_actif = 99;
    const char *sans_result = "{\"params\":[]}";
    VERIFIER(!rpc_lire_spoolman_statut(&etat, sans_result, strlen(sans_result)));
    VERIFIER(etat.id_actif == 99);

    /* --- notification notify_active_spool_set --- */
    uint32_t id_msg = 123;
    const char *notif =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_active_spool_set\","
        "\"params\":[{\"spool_id\":5}]}";
    VERIFIER(rpc_classifier(notif, strlen(notif), &id_msg) == RPC_MSG_SPOOL_ACTIF);
    VERIFIER(id_msg == 0); /* une notification n'a pas d'id correle */
    int32_t actif = 0;
    VERIFIER(rpc_lire_notif_spool_actif(&actif, notif, strlen(notif)));
    VERIFIER(actif == 5);

    const char *notif_nulle =
        "{\"method\":\"notify_active_spool_set\",\"params\":[{\"spool_id\":null}]}";
    VERIFIER(rpc_lire_notif_spool_actif(&actif, notif_nulle, strlen(notif_nulle)));
    VERIFIER(actif == SPOOLMAN_AUCUNE_BOBINE);

    /* Cle absente : false, et `actif` garde sa valeur -- mieux vaut ne rien
       apprendre que d'effacer la bobine active sur un message incomplet. */
    actif = 77;
    const char *notif_vide = "{\"method\":\"notify_active_spool_set\",\"params\":[{}]}";
    VERIFIER(!rpc_lire_notif_spool_actif(&actif, notif_vide, strlen(notif_vide)));
    VERIFIER(actif == 77);
}

void suite_moonraker_rpc(void)
{
    printf("suite : protocole moonraker (JSON-RPC / WebSocket)\n");
    /* Tache 5 (panneau Limits) : la regle RAM documentee dans etat_klipper.h/
     * klipper_fichiers.h exige de connaitre sizeof(etat_klipper_t) apres tout
     * ajout de champ -- imprime ici plutot que devine, meme discipline que
     * les VERIFIER() qui suivent ne se fient jamais a une valeur non
     * mesuree. */
    printf("  sizeof(etat_klipper_t) = %zu octets\n", sizeof(etat_klipper_t));
    section_construire_requete();
    section_construire_abonnement();
    section_classifier();
    section_fusionner_status();
    section_fusionner_instantane();
    section_lire_reponse();
    section_lire_macros();
    section_lire_fichiers();
    section_lire_miniature();
    section_construire_chemin_miniature();
    section_lire_gcode_response();
    section_lire_methode();
    section_methode_commande();
    section_profils_bed_mesh();
    section_spoolman();
}
