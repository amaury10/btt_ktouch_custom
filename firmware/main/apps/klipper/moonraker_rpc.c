#include "moonraker_rpc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

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
     * aussi verifiable qu'un arbre cJSON serialise puis libere. */
    static const char *PARAMS =
        "{\"objects\":{"
        "\"toolhead\":null,\"gcode_move\":null,"
        "\"extruder\":null,\"extruder1\":null,\"extruder2\":null,\"extruder3\":null,"
        "\"extruder4\":null,\"extruder5\":null,\"extruder6\":null,\"extruder7\":null,"
        "\"heater_bed\":null,\"fan\":null,"
        "\"print_stats\":null,\"virtual_sdcard\":null,\"webhooks\":null"
        "}}";
    return rpc_construire_requete(sortie, taille, id, "printer.objects/subscribe", PARAMS);
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
        } else if (strcmp(methode->valuestring, "notify_klippy_disconnected") == 0) {
            type = RPC_MSG_KLIPPY_DECONNECTE;
        } else {
            type = RPC_MSG_AUTRE;
        }
        cJSON_Delete(racine);
        return type;
    }

    /* Pas de "method" valide : ne peut etre qu'une reponse corrélée, et une
     * reponse SANS id numerique n'est pas exploitable (rien a quoi la
     * relier) -- classee invalide plutot que "reponse sans id", pour que
     * l'appelant n'ait qu'un seul type a rejeter. */
    const cJSON *resultat = cJSON_GetObjectItemCaseSensitive(racine, "result");
    const cJSON *erreur = cJSON_GetObjectItemCaseSensitive(racine, "error");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(racine, "id");
    if ((resultat != NULL || erreur != NULL) && cJSON_IsNumber(id)) {
        if (id_sortie != NULL) {
            *id_sortie = (uint32_t)id->valuedouble;
        }
        cJSON_Delete(racine);
        return RPC_MSG_REPONSE;
    }

    cJSON_Delete(racine);
    return RPC_MSG_INVALIDE;
}

/* ------------------------------------------------------------------------
 * Fusion partielle d'un notify_status_update dans l'etat
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
    /* `presente` suit l'existence de l'objet JSON, jamais une valeur a
     * l'interieur (meme convention que moonraker_parse.c) : un chauffeur
     * present mais dont la temperature est hostile reste present. */
    c->presente = true;
    float v;
    if (nombre_fini(obj, "temperature", &v)) {
        c->actuelle = v;
    }
    if (nombre_fini(obj, "target", &v)) {
        c->consigne = v;
    }
}

static void fusionner_ventilateur(klipper_ventilateur_t *v, const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) {
        return;
    }
    v->present = true;
    float vitesse;
    if (nombre_fini(obj, "speed", &vitesse)) {
        v->vitesse = vitesse;
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

    float v;
    if (nombre_fini(gm, "speed_factor", &v)) {
        e->vitesse_pct = (uint16_t)(v * 100.0f + 0.5f);
    }
    if (nombre_fini(gm, "extrude_factor", &v)) {
        e->flux_pct = (uint16_t)(v * 100.0f + 0.5f);
    }

    const cJSON *origine = cJSON_GetObjectItemCaseSensitive(gm, "homing_origin");
    if (cJSON_IsArray(origine) && cJSON_GetArraySize(origine) >= 3) {
        float z;
        if (valeur_finie(cJSON_GetArrayItem(origine, 2), &z)) {
            float um = z * 1000.0f;
            e->babystep_z_um = (int32_t)(um >= 0.0f ? um + 0.5f : um - 0.5f);
        }
    }

    const cJSON *absolu = cJSON_GetObjectItemCaseSensitive(gm, "absolute_coordinates");
    if (cJSON_IsBool(absolu)) {
        e->deplacement_absolu = cJSON_IsTrue(absolu);
    }
}

static void fusionner_print_stats(etat_klipper_t *e, const cJSON *obj)
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
    /* print_duration : aucun champ dedie dans etat_klipper_t pour la duree
     * ecoulee brute. temps_restant_s (estimation) reste du seul ressort de
     * moonraker_parse.c (sondage HTTP) : le recalculer ici depuis une
     * fusion partielle demanderait de stocker un `print_duration` cumule
     * que la structure ne porte pas, pour un champ que le brief ne teste
     * pas -- decision de portee documentee dans le rapport de tache. */
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

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, statut) {
        const char *nom = item->string;
        if (nom == NULL) {
            continue;
        }
        int idx_extrudeur = index_extrudeur_depuis_nom(nom);
        if (idx_extrudeur >= 0) {
            fusionner_chauffeur(&local.extrudeurs[idx_extrudeur], item);
            continue;
        }
        if (strcmp(nom, "heater_bed") == 0) {
            fusionner_chauffeur(&local.plateau, item);
        } else if (strcmp(nom, "toolhead") == 0) {
            fusionner_toolhead(&local, item);
        } else if (strcmp(nom, "gcode_move") == 0) {
            fusionner_gcode_move(&local, item);
        } else if (strcmp(nom, "fan") == 0) {
            fusionner_ventilateur(&local.ventilateurs[0], item);
        } else if (strcmp(nom, "print_stats") == 0) {
            fusionner_print_stats(&local, item);
        } else if (strcmp(nom, "virtual_sdcard") == 0) {
            fusionner_virtual_sdcard(&local, item);
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
        if (local.extrudeurs[i].presente) {
            compte++;
        }
    }
    local.nb_extrudeurs = compte;

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
    if (resultat == NULL && erreur == NULL) {
        cJSON_Delete(racine);
        return false;
    }

    if (erreur != NULL) {
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

/* Traite un candidat "gcode_macro NOM" (element de tableau printer.objects/
 * list, ou nom de cle d'un objet configfile.config -- les deux partagent la
 * meme convention, voir moonraker_rpc.h) : l'ajoute a `e->macros` si c'est
 * bien un objet macro Klipper et que le nom tient dans le tampon fixe. */
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
            /* Ni printer.objects/list ni configfile : forme non reconnue. */
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
