#include "moonraker_rpc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "backend.h"
#include "moonraker_parse.h"

/* ------------------------------------------------------------------------
 * Construction de requêtes
 * ---------------------------------------------------------------------- */

bool rpc_construire_requete(char *sortie, size_t taille, uint32_t id,
                            const char *methode, const char *params_json)
{
    if (sortie == NULL || taille == 0 || methode == NULL) {
        return false;
    }

    int ecrit;
    if (params_json != NULL) {
        ecrit = snprintf(sortie, taille,
                          "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":%u}",
                          methode, params_json, (unsigned)id);
    } else {
        ecrit = snprintf(sortie, taille,
                          "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"id\":%u}",
                          methode, (unsigned)id);
    }
    /* snprintf() tronque en silence sans jamais le signaler par sa valeur de
     * retour a lui seul -- il faut la COMPARER a `taille` pour distinguer une
     * copie complete d'une copie tronquee (meme lecon que
     * moonraker_chemin_commande(), voir moonraker_parse.c). */
    if (ecrit < 0 || (size_t)ecrit >= taille) {
        return false;
    }
    return true;
}

bool rpc_construire_abonnement(char *sortie, size_t taille, uint32_t id)
{
    /* Liste figee des objets dont l'etat v2 a besoin (voir etat_klipper.h) :
     * pas de construction dynamique via cJSON ici, la liste ne depend
     * d'aucune donnee d'entree -- un texte constant est plus simple et tout
     * aussi verifiable qu'un arbre cJSON serialise puis libere.
     *
     * Fix round 1 (revue tache 3, CRITIQUE) : la methode JSON-RPC Moonraker
     * s'ecrit avec des POINTS ("printer.objects.subscribe"), jamais un '/'.
     * Le code d'origine encodait fidelement une erreur de la spec elle-meme
     * (printer.objects/subscribe, forme HTTP) -- voir RPC_ABONNEMENT_TAILLE_MIN
     * dans moonraker_rpc.h pour le recit complet du symptome. */
    static const char *PARAMS =
        "{\"objects\":{"
        "\"toolhead\":null,\"gcode_move\":null,"
        "\"extruder\":null,\"extruder1\":null,\"extruder2\":null,\"extruder3\":null,"
        "\"extruder4\":null,\"extruder5\":null,\"extruder6\":null,\"extruder7\":null,"
        "\"heater_bed\":null,\"fan\":null,"
        "\"print_stats\":null,\"virtual_sdcard\":null,\"webhooks\":null"
        "}}";
    return rpc_construire_requete(sortie, taille, id, "printer.objects.subscribe", PARAMS);
}

/* ------------------------------------------------------------------------
 * Conversions flottant -> entier bornées (fix round 1, C2)
 * ---------------------------------------------------------------------- */

/* Ces trois fonctions partagent un seul principe : convertir un flottant
 * hors de la plage représentable du type entier cible -- ou non fini -- est
 * un comportement indéfini (C11 6.3.1.4). Les comparaisons de bornes se
 * font en DOUBLE (mantisse 53 bits, qui représente EXACTEMENT
 * UINT16_MAX/UINT32_MAX/INT32_MIN/INT32_MAX) pour ne jamais laisser passer
 * une valeur tout juste hors plage à cause d'un arrondi de la borne
 * elle-même en float.
 *
 * Rendent false (et ne touchent PAS `*sortie`) uniquement si `d` n'est pas
 * fini : dans ce cas la règle habituelle de poison par champ de ce fichier
 * s'applique (champ laissé inchangé, voir moonraker_rpc.h). Une valeur
 * FINIE mais hors plage n'est PAS un poison -- exactement comme
 * moonraker_estimer_temps_restant_s() (moonraker_parse.h) plafonne une
 * estimation réelle mais démesurée plutôt que de la rejeter -- donc rendent
 * true avec `*sortie` clampé à la borne dépassée. L'appelant doit avoir
 * déjà appliqué l'arrondi au plus proche (+/-0.5) à `d` : ces fonctions ne
 * font que clamper puis tronquer. */

static bool double_vers_u32_borne(double d, uint32_t *sortie)
{
    if (!isfinite(d)) {
        return false;
    }
    if (d <= 0.0) {
        *sortie = 0;
    } else if (d >= (double)UINT32_MAX) {
        *sortie = UINT32_MAX;
    } else {
        *sortie = (uint32_t)d;
    }
    return true;
}

static bool double_vers_u16_borne(double d, uint16_t *sortie)
{
    if (!isfinite(d)) {
        return false;
    }
    if (d <= 0.0) {
        *sortie = 0;
    } else if (d >= (double)UINT16_MAX) {
        *sortie = UINT16_MAX;
    } else {
        *sortie = (uint16_t)d;
    }
    return true;
}

static bool double_vers_i32_borne(double d, int32_t *sortie)
{
    if (!isfinite(d)) {
        return false;
    }
    if (d >= (double)INT32_MAX) {
        *sortie = INT32_MAX;
    } else if (d <= (double)INT32_MIN) {
        *sortie = INT32_MIN;
    } else {
        *sortie = (int32_t)d;
    }
    return true;
}

/* ------------------------------------------------------------------------
 * Classification
 * ---------------------------------------------------------------------- */

rpc_message_type_t rpc_classifier(const char *json, size_t longueur, uint32_t *id_sortie)
{
    if (id_sortie != NULL) {
        *id_sortie = 0;
    }
    if (json == NULL || longueur == 0) {
        return RPC_MSG_INVALIDE;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL || !cJSON_IsObject(racine)) {
        cJSON_Delete(racine);
        return RPC_MSG_INVALIDE;
    }

    const cJSON *methode = cJSON_GetObjectItemCaseSensitive(racine, "method");
    if (cJSON_IsString(methode) && methode->valuestring != NULL) {
        rpc_message_type_t type;
        if (strcmp(methode->valuestring, "notify_status_update") == 0) {
            type = RPC_MSG_STATUS_UPDATE;
        } else if (strcmp(methode->valuestring, "notify_klippy_ready") == 0) {
            type = RPC_MSG_KLIPPY_READY;
        } else if (strcmp(methode->valuestring, "notify_klippy_disconnected") == 0 ||
                   strcmp(methode->valuestring, "notify_klippy_shutdown") == 0) {
            /* Fix round 1 (revue, C4) : un arret Klippy (MCU shutdown,
             * thermal runaway...) est le cas le plus critique en pratique --
             * les notify_status_update cessent d'arriver, l'ecran ne doit
             * plus faire confiance a l'etat pousse, exactement comme une
             * deconnexion. */
            type = RPC_MSG_KLIPPY_DECONNECTE;
        } else {
            type = RPC_MSG_AUTRE;
        }
        cJSON_Delete(racine);
        return type;
    }

    /* Pas de "method" valide : ne peut etre qu'une reponse corrélée, et une
     * reponse SANS id numerique exploitable n'est pas exploitable (rien a
     * quoi la relier) -- classee invalide plutot que "reponse sans id",
     * pour que l'appelant n'ait qu'un seul type a rejeter. */
    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *erreur = cJSON_GetObjectItemCaseSensitive(racine, "error");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(racine, "id");
    if ((resultat != NULL || erreur != NULL) && cJSON_IsNumber(id)) {
        uint32_t id_borne;
        /* Fix round 1 (revue, C2) : id->valuedouble peut etre hostile
         * (-1, 1e300, 2^32, voire +infini pour "id":1e400) -- clampe avant
         * conversion plutot que UB. Un id non fini (cas degenere,
         * pratiquement jamais emis par un vrai Moonraker) est traite comme
         * un id absent : rien a quoi le correler. */
        if (double_vers_u32_borne(id->valuedouble, &id_borne)) {
            if (id_sortie != NULL) {
                *id_sortie = id_borne;
            }
            cJSON_Delete(racine);
            return RPC_MSG_REPONSE;
        }
    }

    cJSON_Delete(racine);
    return RPC_MSG_INVALIDE;
}

/* ------------------------------------------------------------------------
 * Fusion (partielle ou instantané complet) dans l'état
 * ---------------------------------------------------------------------- */

/* Lit un champ numerique d'un objet JSON, le convertit en float, et rend
 * false SANS TOUCHER `*sortie` si le champ est absent, n'est pas un nombre,
 * ou devient infini une fois retreci en float. cJSON stocke tout en double ;
 * un champ hostile comme 1e40 est un double fini mais un float infini --
 * meme piege que moonraker_parse.c (voir son commentaire de tete sur la
 * conversion C11 6.3.1.4 : la borne doit etre posee AVANT toute conversion
 * ulterieure, jamais apres). C'est le coeur de la decision de "poison par
 * champ" documentee en tete de moonraker_rpc.h : cette fonction est LE point
 * de passage de chaque valeur individuelle fusionnee dans l'etat, et elle ne
 * laisse jamais passer un NaN/Inf. */
static bool valeur_finie(const cJSON *v, float *sortie)
{
    if (!cJSON_IsNumber(v)) {
        return false;
    }
    float f = (float)v->valuedouble;
    if (!isfinite(f)) {
        return false;
    }
    *sortie = f;
    return true;
}

static bool nombre_fini(const cJSON *parent, const char *cle, float *sortie)
{
    return valeur_finie(cJSON_GetObjectItemCaseSensitive(parent, cle), sortie);
}

/* Variante DOUBLE de valeur_finie() ci-dessus, pour tout champ dont la
 * DESTINATION est un entier (pas un `float` de etat_klipper_t) : rien ne
 * justifie de retrecir en float au passage, une etape qui perd de la
 * precision pour rien et deplace le point d'arrondi. Fix round 1 (revue,
 * C6) : avec le retrecissement en float, homing_origin[2] = -0.0755 mm
 * devenait -0.0754999965 (le float le plus proche), donc -75.4999... µm
 * apres mise a l'echelle -- arrondi a -75 au lieu de -76 (le float le plus
 * proche de 0.0755 n'est PAS equidistant de 75 et 76 une fois mis a
 * l'echelle). En double, -0.0755 mm est bien plus proche de l'exact, -75.5
 * µm pile, qui s'arrondit correctement en -76 (arrondi au plus proche en
 * s'eloignant de zero). Fix round 2 (re-revue) : le meme defaut affectait
 * encore speed_factor/extrude_factor (round 1 les clampait deja, mais les
 * lisait toujours via nombre_fini()/float) ; les trois champs mis a
 * l'echelle de fusionner_gcode_move() (vitesse_pct, flux_pct,
 * babystep_z_um) passent maintenant tous par cette variante double, pas
 * seulement babystep -- babystep n'avait rien de special, c'etait juste le
 * premier des trois corrige. */
static bool valeur_double_finie(const cJSON *v, double *sortie)
{
    if (!cJSON_IsNumber(v)) {
        return false;
    }
    double d = v->valuedouble;
    if (!isfinite(d)) {
        return false;
    }
    *sortie = d;
    return true;
}

/* Decode l'index d'un chauffeur extrudeur a partir du NOM d'objet Klipper
 * ("extruder" -> 0, "extruderN" -> N pour N=1..KLIPPER_EXTRUDEURS_MAX-1).
 * Rend -1 si `nom` ne correspond pas exactement a ce motif (un autre objet
 * dont le nom commence par "extruder", comme "extruder_stepper ...", ou un
 * index hors bornes comme "extruder8"/"extruder9") -- dans tous ces cas
 * l'appelant doit ignorer l'objet SANS faire echouer le reste du message. */
static int index_extrudeur_depuis_nom(const char *nom)
{
    static const char PREFIXE[] = "extruder";
    static const size_t LEN_PREFIXE = sizeof(PREFIXE) - 1;

    if (nom == NULL || strncmp(nom, PREFIXE, LEN_PREFIXE) != 0) {
        return -1;
    }
    const char *reste = nom + LEN_PREFIXE;
    if (*reste == '\0') {
        return 0;   /* "extruder" tout court == index 0 */
    }
    for (const char *p = reste; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;   /* p.ex. "extruder_stepper ..." */
        }
    }
    long valeur = strtol(reste, NULL, 10);
    if (valeur < 1 || valeur >= KLIPPER_EXTRUDEURS_MAX) {
        return -1;
    }
    return (int)valeur;
}

/* Masque bit0=X bit1=Y bit2=Z depuis une chaine Klipper "homed_axes" (p.ex.
 * "xyz", "", "xz"). Tout caractere autre que x/y/z est ignore silencieusement
 * (Klipper ne produit que ces trois lettres dans ce champ). */
static uint8_t masque_axes_depuis_texte(const char *s)
{
    uint8_t masque = 0;
    for (const char *p = s; *p != '\0'; p++) {
        switch (*p) {
            case 'x': masque |= 1u << 0; break;
            case 'y': masque |= 1u << 1; break;
            case 'z': masque |= 1u << 2; break;
            default: break;
        }
    }
    return masque;
}

static void fusionner_chauffeur(klipper_chauffeur_t *c, const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) {
        return;
    }
    float v;
    bool touche = false;
    if (nombre_fini(obj, "temperature", &v)) {
        c->actuelle = v;
        touche = true;
    }
    if (nombre_fini(obj, "target", &v)) {
        c->consigne = v;
        touche = true;
    }
    /* Fix round 1 (revue, C5/probe 11) : `presente` ne s'active QUE si
     * l'objet porte au moins un champ reconnu -- PAS sur la simple
     * existence de la clé JSON. Klipper répond `{}` pour un objet
     * interrogé mais absent de la machine (voir rpc_fusionner_instantane,
     * qui interroge extruder..extruder7 sans savoir à l'avance combien
     * existent réellement) : sans cette garde, une machine mono-extrudeur
     * verrait `nb_extrudeurs` gonfler à 8 dès le premier instantané. */
    if (touche) {
        c->presente = true;
    }
}

static void fusionner_ventilateur(klipper_ventilateur_t *v, const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) {
        return;
    }
    float vitesse;
    /* Meme garde que fusionner_chauffeur() ci-dessus, meme raison. */
    if (nombre_fini(obj, "speed", &vitesse)) {
        v->vitesse = vitesse;
        v->present = true;
    }
}

static void fusionner_toolhead(etat_klipper_t *e, const cJSON *toolhead)
{
    if (!cJSON_IsObject(toolhead)) {
        return;
    }

    const cJSON *position = cJSON_GetObjectItemCaseSensitive(toolhead, "position");
    if (cJSON_IsArray(position) && cJSON_GetArraySize(position) >= 3) {
        for (int i = 0; i < 3; i++) {
            float v;
            if (valeur_finie(cJSON_GetArrayItem(position, i), &v)) {
                e->position[i] = v;
            }
        }
    }

    const cJSON *homed = cJSON_GetObjectItemCaseSensitive(toolhead, "homed_axes");
    if (cJSON_IsString(homed) && homed->valuestring != NULL) {
        e->axes_references = masque_axes_depuis_texte(homed->valuestring);
    }

    const cJSON *extrudeur_actif = cJSON_GetObjectItemCaseSensitive(toolhead, "extruder");
    if (cJSON_IsString(extrudeur_actif) && extrudeur_actif->valuestring != NULL) {
        int idx = index_extrudeur_depuis_nom(extrudeur_actif->valuestring);
        if (idx >= 0) {
            e->outil_actif = (uint8_t)idx;
        }
    }
}

static void fusionner_gcode_move(etat_klipper_t *e, const cJSON *gm)
{
    if (!cJSON_IsObject(gm)) {
        return;
    }

    /* Fix round 2 (re-revue) : speed_factor/extrude_factor sont lus en
     * DOUBLE via valeur_double_finie() -- EXACTEMENT le meme motif que
     * homing_origin[2] ci-dessous (fix round 1, C6). Round 1 avait clampe
     * ces deux champs correctement mais les lisait encore via nombre_fini()
     * (retrecissement en float AVANT la mise a l'echelle x100), le meme
     * defaut de precision que C6 avait diagnostique pour babystep sans
     * l'appliquer ici : speed_factor 1.045 devient le float le plus proche
     * 1.0449999570846558, x100+0.5 = 104.999995... -> 104, alors que le
     * calcul en double (1.045*100+0.5 = 105.0 pile) donne 105 -- une vraie
     * commande Klipper (M220 S104.5) afficherait 104 au lieu de 105. Les
     * valeurs de test qui coincidaient par hasard entre les deux chemins
     * (1.03/1.07) ne pouvaient pas reveler ce bug ; 1.045/1.055/0.905 si
     * (verifie en Python avant d'ecrire ce correctif, comme pour C6).
     * vitesse_pct/flux_pct/babystep_z_um sont tous trois des champs ENTIERS
     * de etat_klipper_t (pas des float) : aucun des trois n'a de raison de
     * transiter par un float intermediaire, seul le clamp final vers le
     * type entier cible importe. */
    double d;
    if (valeur_double_finie(cJSON_GetObjectItemCaseSensitive(gm, "speed_factor"), &d)) {
        /* speed_factor 1000.0 (M220 S100000, une commande Klipper reelle)
         * donne 100000 %, hors plage d'un uint16_t -- clampe a UINT16_MAX
         * plutot qu'UB (fix round 1, C2). L'arrondi au plus proche (+0.5,
         * valeurs toujours positives ici) est applique AVANT le clamp,
         * cote appelant, comme documente sur les helpers double_vers_*_borne
         * ci-dessus. */
        uint16_t pct;
        if (double_vers_u16_borne(d * 100.0 + 0.5, &pct)) {
            e->vitesse_pct = pct;
        }
    }
    if (valeur_double_finie(cJSON_GetObjectItemCaseSensitive(gm, "extrude_factor"), &d)) {
        uint16_t pct;
        if (double_vers_u16_borne(d * 100.0 + 0.5, &pct)) {
            e->flux_pct = pct;
        }
    }

    const cJSON *origine = cJSON_GetObjectItemCaseSensitive(gm, "homing_origin");
    if (cJSON_IsArray(origine) && cJSON_GetArraySize(origine) >= 3) {
        double z;
        if (valeur_double_finie(cJSON_GetArrayItem(origine, 2), &z)) {
            /* mm -> µm en DOUBLE de bout en bout, arrondi au plus proche EN
             * S'ELOIGNANT DE ZERO (+0.5 si positif, -0.5 si negatif) avant
             * clamp (z=1e7 mm est un homing_origin absurde mais FINI ; sans
             * clamp, le cast vers int32_t serait UB, fix round 1 C2). */
            double um = z * 1000.0;
            double arrondi = (um >= 0.0) ? um + 0.5 : um - 0.5;
            int32_t bs;
            if (double_vers_i32_borne(arrondi, &bs)) {
                e->babystep_z_um = bs;
            }
        }
    }

    const cJSON *absolu = cJSON_GetObjectItemCaseSensitive(gm, "absolute_coordinates");
    if (cJSON_IsBool(absolu)) {
        e->deplacement_absolu = cJSON_IsTrue(absolu);
    }
}

/* `duree_vue`/`duree` : sortie annexe pour rpc_fusionner_status()/
 * rpc_fusionner_instantane(), qui recalculent temps_restant_s APRES la
 * boucle de fusion (voir fusionner_objet_statut ci-dessous) avec la
 * progression la plus a jour, quel que soit l'ordre d'arrivee de
 * print_stats/virtual_sdcard dans le meme message JSON. */
static void fusionner_print_stats(etat_klipper_t *e, const cJSON *obj,
                                   bool *duree_vue, float *duree)
{
    if (!cJSON_IsObject(obj)) {
        return;
    }
    const cJSON *etat_txt = cJSON_GetObjectItemCaseSensitive(obj, "state");
    if (cJSON_IsString(etat_txt) && etat_txt->valuestring != NULL) {
        snprintf(e->etat, sizeof(e->etat), "%s", etat_txt->valuestring);
        e->impression_en_cours = (strcmp(e->etat, "printing") == 0);
        e->impression_en_pause = (strcmp(e->etat, "paused") == 0);
    }
    const cJSON *fichier = cJSON_GetObjectItemCaseSensitive(obj, "filename");
    if (cJSON_IsString(fichier) && fichier->valuestring != NULL) {
        snprintf(e->fichier, sizeof(e->fichier), "%s", fichier->valuestring);
    }
    float v;
    if (nombre_fini(obj, "print_duration", &v)) {
        *duree_vue = true;
        *duree = v;
    }
}

static void fusionner_virtual_sdcard(etat_klipper_t *e, const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) {
        return;
    }
    float progression;
    if (nombre_fini(obj, "progress", &progression)) {
        e->progression = progression;
    }
}

/* Coeur commun a rpc_fusionner_status() (params[0]) et
 * rpc_fusionner_instantane() (result.status) : `statut` est deja
 * verifie comme un objet JSON par l'appelant, qui n'a plus qu'a extraire
 * son enveloppe propre (notification vs reponse) avant d'appeler ceci.
 * Fix round 1 (revue, C5) : cette extraction est CE QUI PERMET a
 * rpc_fusionner_instantane() de reutiliser exactement le meme moteur, sans
 * dupliquer la moindre regle de fusion. */
static void fusionner_objet_statut(etat_klipper_t *local, const cJSON *statut)
{
    bool duree_vue = false;
    float duree = 0.0f;

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, statut) {
        const char *nom = item->string;
        if (nom == NULL) {
            continue;
        }
        int idx_extrudeur = index_extrudeur_depuis_nom(nom);
        if (idx_extrudeur >= 0) {
            fusionner_chauffeur(&local->extrudeurs[idx_extrudeur], item);
            continue;
        }
        if (strcmp(nom, "heater_bed") == 0) {
            fusionner_chauffeur(&local->plateau, item);
        } else if (strcmp(nom, "toolhead") == 0) {
            fusionner_toolhead(local, item);
        } else if (strcmp(nom, "gcode_move") == 0) {
            fusionner_gcode_move(local, item);
        } else if (strcmp(nom, "fan") == 0) {
            fusionner_ventilateur(&local->ventilateurs[0], item);
        } else if (strcmp(nom, "print_stats") == 0) {
            fusionner_print_stats(local, item, &duree_vue, &duree);
        } else if (strcmp(nom, "virtual_sdcard") == 0) {
            fusionner_virtual_sdcard(local, item);
        }
        /* Tout autre nom (webhooks, mcu, configfile, un objet inconnu ou un
         * "extruderN" hors bornes deja ecarte ci-dessus par idx_extrudeur ==
         * -1) est ignore : message par ailleurs valide, cet objet-ci n'a
         * simplement pas de correspondant dans etat_klipper_t v2. */
    }

    /* `nb_extrudeurs` est recalcule sur l'etat COMPLET (les 8 emplacements),
     * pas seulement ceux touches par CE message : `presente` ne redescend
     * jamais a false ici (aucun mecanisme de ce module ne "retire" un
     * extrudeur), donc un extrudeur vu une fois reste compte. */
    uint8_t compte = 0;
    for (int i = 0; i < KLIPPER_EXTRUDEURS_MAX; i++) {
        if (local->extrudeurs[i].presente) {
            compte++;
        }
    }
    local->nb_extrudeurs = compte;

    /* Fix round 1 (revue, C3) : temps_restant_s n'etait recalcule que par
     * le sondage HTTP (moonraker_parse_status), jamais par la fusion
     * WebSocket -- une fois le WS en ligne (le chemin NOMINAL du jalon,
     * voir spec §4), le temps restant restait bloque a 0 pour toujours,
     * regression visible sur l'ecran d'impression. Recalcule ici avec la
     * MEME formule que le sondage HTTP (moonraker_estimer_temps_restant_s,
     * moonraker_parse.h) des que ce message a apporte un print_duration
     * exploitable, combine a la progression la plus a jour de `local`
     * (qu'elle vienne de CE message ou d'un message anterieur — la fusion
     * est partielle, `local->progression` porte deja la meilleure valeur
     * connue). Un print_duration absent ou poison (non fini) laisse
     * temps_restant_s a sa valeur courante, comme tout autre champ. */
    if (duree_vue) {
        local->temps_restant_s = moonraker_estimer_temps_restant_s(local->progression, duree);
    }
}

bool rpc_fusionner_status(etat_klipper_t *etat, const char *json, size_t longueur)
{
    if (etat == NULL || json == NULL || longueur == 0) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL || !cJSON_IsObject(racine)) {
        cJSON_Delete(racine);
        return false;
    }

    /* Piege classique du protocole : `params` est un TABLEAU dont le
     * PREMIER element est l'objet de statut (le second est un horodatage
     * sans interet ici) -- pas `params` lui-meme. Une enveloppe qui ne
     * respecte pas cette forme (params absent, pas un tableau, ou dont le
     * premier element n'est pas un objet) est une anomalie D'ENVELOPPE :
     * elle invalide le message ENTIER (voir la decision de poison en tete
     * de moonraker_rpc.h), contrairement a un champ individuel hostile
     * plus bas, qui lui n'invalide que ce champ. */
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(racine, "params");
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) {
        cJSON_Delete(racine);
        return false;
    }
    const cJSON *statut = cJSON_GetArrayItem(params, 0);
    if (!cJSON_IsObject(statut)) {
        cJSON_Delete(racine);
        return false;
    }

    /* A partir d'ici l'enveloppe est valide : on travaille sur une COPIE de
     * l'etat courant (pas un etat vide -- c'est une fusion PARTIELLE, tout
     * champ absent de `statut` doit garder sa valeur actuelle), et on ne
     * l'ecrit dans `*etat` qu'une fois la fusion terminee, en un seul bloc
     * -- meme discipline que moonraker_parse_status(). */
    etat_klipper_t local = *etat;
    fusionner_objet_statut(&local, statut);
    *etat = local;

    cJSON_Delete(racine);
    return true;
}

bool rpc_fusionner_instantane(etat_klipper_t *etat, const char *json, size_t longueur)
{
    if (etat == NULL || json == NULL || longueur == 0) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL || !cJSON_IsObject(racine)) {
        cJSON_Delete(racine);
        return false;
    }

    /* Reponse a printer.objects.subscribe : result.status, PAS params[0]
     * (ce n'est pas une notification poussee, c'est LA reponse corrélée a
     * la requete d'abonnement elle-meme). */
    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *statut = cJSON_GetObjectItemCaseSensitive(resultat, "status");
    if (!cJSON_IsObject(statut)) {
        cJSON_Delete(racine);
        return false;
    }

    etat_klipper_t local = *etat;
    fusionner_objet_statut(&local, statut);
    *etat = local;

    cJSON_Delete(racine);
    return true;
}

/* ------------------------------------------------------------------------
 * Lecture d'une reponse correlee (result/error)
 * ---------------------------------------------------------------------- */

/* Copie `source` dans `sortie` (tampon de `taille` octets, NUL compris),
 * tronque a `taille` si necessaire SANS jamais couper au milieu d'une
 * sequence UTF-8 multi-octets : un octet de continuation a la forme
 * 10xxxxxx (0x80-0xBF). Si la coupe naive tombe sur un tel octet, on recule
 * jusqu'au dernier caractere COMPLET plutot que de laisser trainer un
 * fragment de sequence en fin de tampon -- un message d'erreur Klipper peut
 * contenir des caracteres accentues ou "°C" (0xC2 0xB0), et une coupe
 * aveugle produirait une chaine mal formee que l'afficheur ne saurait pas
 * rendre proprement. */
static void copier_texte_utf8_borne(const char *source, char *sortie, size_t taille)
{
    size_t max_octets = taille - 1;   /* place utile hors le '\0' final */
    size_t len = strlen(source);
    size_t coupe = (len <= max_octets) ? len : max_octets;

    while (coupe > 0 && ((unsigned char)source[coupe] & 0xC0) == 0x80) {
        coupe--;
    }

    memcpy(sortie, source, coupe);
    sortie[coupe] = '\0';
}

bool rpc_lire_reponse(const char *json, size_t longueur, bool *succes,
                      char *erreur_texte, size_t taille_erreur)
{
    if (json == NULL || longueur == 0 || succes == NULL) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL || !cJSON_IsObject(racine)) {
        cJSON_Delete(racine);
        return false;
    }

    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *erreur = cJSON_GetObjectItemCaseSensitive(racine, "error");
    /* Fix round 1 (revue, C7) : "error" doit etre un OBJET JSON reel pour
     * compter comme un echec -- un "error":null a cote d'un "result"
     * valide (defensif chez un client, ou produit par un bug ailleurs) ne
     * doit jamais se lire comme un echec juste parce que la CLE existe. */
    bool erreur_valide = cJSON_IsObject(erreur);
    if (resultat == NULL && !erreur_valide) {
        cJSON_Delete(racine);
        return false;
    }

    if (erreur_valide) {
        *succes = false;
        const cJSON *message = cJSON_GetObjectItemCaseSensitive(erreur, "message");
        const char *texte = (cJSON_IsString(message) && message->valuestring != NULL)
                                 ? message->valuestring : "";
        if (erreur_texte != NULL && taille_erreur > 0) {
            copier_texte_utf8_borne(texte, erreur_texte, taille_erreur);
        }
    } else {
        *succes = true;
        if (erreur_texte != NULL && taille_erreur > 0) {
            erreur_texte[0] = '\0';
        }
    }

    cJSON_Delete(racine);
    return true;
}

/* ------------------------------------------------------------------------
 * Liste des macros
 * ---------------------------------------------------------------------- */

/* Traite un candidat "gcode_macro NOM" (element de tableau
 * printer.objects.list, ou nom de cle d'un objet configfile.config -- les
 * deux partagent la meme convention, voir moonraker_rpc.h) : l'ajoute a
 * `e->macros` si c'est bien un objet macro Klipper et que le nom tient dans
 * le tampon fixe. */
static void traiter_candidat_macro(etat_klipper_t *e, const char *nom_complet)
{
    static const char PREFIXE[] = "gcode_macro ";
    static const size_t LEN_PREFIXE = sizeof(PREFIXE) - 1;

    if (nom_complet == NULL || strncmp(nom_complet, PREFIXE, LEN_PREFIXE) != 0) {
        return;
    }
    const char *nom = nom_complet + LEN_PREFIXE;
    size_t len = strlen(nom);
    if (len == 0) {
        return;
    }
    if (len >= KLIPPER_MACRO_NOM_MAX) {
        /* Trop long pour tenir avec le '\0' : IGNORE, jamais tronque -- un
         * nom tronque pourrait designer une AUTRE macro existante et
         * l'executer par erreur (voir moonraker_rpc.h). Ne positionne pas
         * `macros_tronquees`, qui documente specifiquement le depassement
         * du COMPTE (KLIPPER_MACROS_MAX), pas une longueur de nom. */
        return;
    }
    if (e->nb_macros >= KLIPPER_MACROS_MAX) {
        e->macros_tronquees = true;
        return;
    }
    memcpy(e->macros[e->nb_macros], nom, len + 1);
    e->nb_macros++;
}

bool rpc_lire_macros(etat_klipper_t *etat, const char *json, size_t longueur)
{
    if (etat == NULL || json == NULL || longueur == 0) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL) {
        return false;
    }

    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *objets = cJSON_GetObjectItemCaseSensitive(resultat, "objects");
    const cJSON *configfile_config = NULL;
    if (!cJSON_IsArray(objets)) {
        const cJSON *statut = cJSON_GetObjectItemCaseSensitive(resultat, "status");
        const cJSON *configfile = cJSON_GetObjectItemCaseSensitive(statut, "configfile");
        configfile_config = cJSON_GetObjectItemCaseSensitive(configfile, "config");
        if (!cJSON_IsObject(configfile_config)) {
            /* Ni printer.objects.list ni configfile : forme non reconnue. */
            cJSON_Delete(racine);
            return false;
        }
    }

    /* Instantane COMPLET (pas une fusion partielle comme rpc_fusionner_status
     * : chaque appel remplace la liste entiere, voir moonraker_rpc.h) --
     * travaille aussi sur une copie, ecrite en bloc a la fin, pour ne jamais
     * laisser `*etat` a moitie remplace si un cas imprevu apparaissait. */
    etat_klipper_t local = *etat;
    local.nb_macros = 0;
    local.macros_tronquees = false;
    memset(local.macros, 0, sizeof(local.macros));

    const cJSON *item = NULL;
    if (cJSON_IsArray(objets)) {
        cJSON_ArrayForEach(item, objets) {
            if (cJSON_IsString(item) && item->valuestring != NULL) {
                traiter_candidat_macro(&local, item->valuestring);
            }
        }
    } else {
        cJSON_ArrayForEach(item, configfile_config) {
            traiter_candidat_macro(&local, item->string);
        }
    }

    *etat = local;
    cJSON_Delete(racine);
    return true;
}

/* ------------------------------------------------------------------------
 * Liste des fichiers gcode
 * ---------------------------------------------------------------------- */

/* Traite un élément du tableau `result` de `server.files.list` : l'ajoute à
 * `e->fichiers` si c'est bien un objet portant un champ `.path` chaîne et que
 * le chemin tient dans le tampon fixe. Même structure que
 * traiter_candidat_macro() ci-dessus, une différence près : la source est un
 * OBJET (pas une chaîne nue), dont on extrait `.path`. */
static void traiter_candidat_fichier(etat_klipper_t *e, const cJSON *item)
{
    if (!cJSON_IsObject(item)) {
        return;
    }
    const cJSON *chemin = cJSON_GetObjectItemCaseSensitive(item, "path");
    if (!cJSON_IsString(chemin) || chemin->valuestring == NULL) {
        return;
    }
    const char *nom = chemin->valuestring;
    size_t len = strlen(nom);
    if (len == 0) {
        return;
    }
    if (len >= KLIPPER_FICHIER_MAX) {
        /* Trop long pour tenir avec le '\0' : IGNORE, jamais tronque -- un
         * chemin tronque pourrait designer un AUTRE fichier existant (meme
         * raisonnement que traiter_candidat_macro() pour les noms de macro
         * trop longs). Ne positionne pas `fichiers_tronques`, qui documente
         * specifiquement le depassement du COMPTE (KLIPPER_FICHIERS_MAX),
         * pas une longueur de chemin. */
        return;
    }
    if (e->nb_fichiers >= KLIPPER_FICHIERS_MAX) {
        e->fichiers_tronques = true;
        return;
    }
    memcpy(e->fichiers[e->nb_fichiers], nom, len + 1);
    e->nb_fichiers++;
}

bool rpc_lire_fichiers(etat_klipper_t *etat, const char *json, size_t longueur)
{
    if (etat == NULL || json == NULL || longueur == 0) {
        return false;
    }

    cJSON *racine = cJSON_ParseWithLength(json, longueur);
    if (racine == NULL) {
        return false;
    }

    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    if (!cJSON_IsArray(resultat)) {
        cJSON_Delete(racine);
        return false;
    }

    /* Instantane COMPLET (pas une fusion partielle) -- travaille sur une
     * copie, ecrite en bloc a la fin, meme discipline que rpc_lire_macros(). */
    etat_klipper_t local = *etat;
    local.nb_fichiers = 0;
    local.fichiers_tronques = false;
    memset(local.fichiers, 0, sizeof(local.fichiers));

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, resultat) {
        traiter_candidat_fichier(&local, item);
    }

    *etat = local;
    cJSON_Delete(racine);
    return true;
}

/* ------------------------------------------------------------------------
 * Correspondance action -> méthode JSON-RPC (transport WebSocket)
 * ---------------------------------------------------------------------- */

bool rpc_methode_commande(const char *action, char *methode, size_t taille)
{
    if (action == NULL || methode == NULL || taille == 0) {
        return false;
    }

    const char *trouve;
    if (strcmp(action, BACKEND_ACTION_PAUSE) == 0) {
        trouve = "printer.print.pause";
    } else if (strcmp(action, BACKEND_ACTION_REPRENDRE) == 0) {
        trouve = "printer.print.resume";
    } else if (strcmp(action, BACKEND_ACTION_ANNULER) == 0) {
        trouve = "printer.print.cancel";
    } else if (strcmp(action, BACKEND_ACTION_URGENCE) == 0) {
        trouve = "printer.emergency_stop";
    } else {
        /* Action inconnue (BACKEND_ACTION_MACRO comprise -- tâche 6, voir le
         * commentaire de tête dans moonraker_rpc.h) : ne rien écrire dans
         * `methode`, même politique que moonraker_chemin_commande(). */
        return false;
    }

    /* Même garde qu'ailleurs dans ce fichier et dans moonraker_parse.c :
     * snprintf() tronque en silence, il faut COMPARER sa valeur de retour à
     * `taille` pour distinguer une copie complète d'une copie tronquée
     * jamais rendue comme un succès. */
    int ecrit = snprintf(methode, taille, "%s", trouve);
    if (ecrit < 0 || (size_t)ecrit >= taille) {
        return false;
    }
    return true;
}
