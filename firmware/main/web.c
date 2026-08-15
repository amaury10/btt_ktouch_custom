/* Serveur HTTP : c'est la seule interface de contrôle du firmware une fois le
 * câble série hors jeu. Douze routes :
 *   GET  /            page d'état minimale, avec liens vers les autres routes
 *   GET  /status      JSON : slot en cours, version, IP, uptime, mémoire libre,
 *                     tactile disponible, compteur de démarrages, état de la
 *                     sauvegarde BTT (voir /backup-btt ci-dessous)
 *   GET  /state       JSON : état de la liaison avec l'hôte, génération et
 *                     dernier état Klipper connu (voir gestion_state() plus bas)
 *   GET  /log         texte brut, contenu du journal réseau (netlog_snapshot())
 *   GET  /revert      page HTML avec un bouton qui déclenche le POST (pratique
 *                     depuis un navigateur : Firefox, sans outil POST)
 *   POST /revert      bascule vers l'autre slot et redémarre
 *   GET  /backup-btt  page HTML : état de la sauvegarde BTT + bouton (même
 *                     principe que /revert ci-dessus, un GET ne déclenche rien)
 *   POST /backup-btt  copie app0 (BTT) vers spiffs, brut, avec vérification
 *                     SHA-256 après relecture (voir ota.c/ota_backup_btt) --
 *                     N'écrit JAMAIS dans un slot app, seulement dans spiffs
 *   GET  /ota         page HTML : upload d'une image .bin + état slot/version/
 *                     sauvegarde (voir gestion_ota_page ci-dessous)
 *   POST /ota         `?dry_run=1` : reçoit le corps EN FLUX, calcule son
 *                     SHA-256 à la volée (mbedtls, jamais l'image entière en
 *                     RAM), vérifie le magic 0xE9 et la taille contre la
 *                     partition ciblée par une future OTA (voir
 *                     ota.c/ota_verifier_flux) — N'ÉCRIT RIEN, nulle part (ni
 *                     app, ni spiffs). Sans `dry_run=1` (absent ou différent
 *                     de "1") : COMMIT RÉEL (voir ota.c/ota_appliquer_flux) —
 *                     écrit dans le slot OTA inactif, GARDÉ par une
 *                     sauvegarde BTT valide tant qu'app0 n'a jamais encore
 *                     été écrasé (voir plus bas), puis rebascule le
 *                     démarrage dessus et arme le filet rescue.c avant de
 *                     répondre au client et de redémarrer.
 *   GET  /restore-btt page HTML : état de la sauvegarde BTT + bouton (même
 *                     principe que /revert et /backup-btt ci-dessus, un GET
 *                     ne déclenche rien)
 *   POST /restore-btt écrit la sauvegarde BTT (spiffs) dans le slot OTA
 *                     inactif (voir ota.c/ota_restaurer_btt) — l'assurance
 *                     qui rend l'OTA réversible. EXIGE une sauvegarde BTT
 *                     valide (refuse sinon, sans écrire quoi que ce soit),
 *                     puis rebascule le démarrage dessus, arme le filet
 *                     rescue.c, répond au client et redémarre — même
 *                     discipline que POST /ota ci-dessus.
 *
 * Le commit OTA réel (écriture dans un slot app) est désormais câblé, mais
 * GARDÉ : ota_appliquer_flux() (ota.c) refuse d'appeler esp_ota_begin() sur
 * app0 tant qu'aucune sauvegarde BTT valide (ota_backup_etat() ==
 * OTA_BACKUP_VALIDE) n'existe ET qu'aucun commit précédent n'a déjà écrasé
 * app0 (drapeau NVS dédié, posé au premier commit réussi visant app0) — voir
 * le commentaire de tête de ota_appliquer_flux() dans ota.h pour le détail
 * de la garde et de la séquence esp_ota_begin/write/end/set_boot_partition.
 * Ce firmware tourne depuis app1 (le slot que l'OTA du firmware d'origine
 * choisit) ; avec deux slots seulement, le slot inactif vu depuis app1 est
 * app0 — celui du firmware d'origine, jusqu'à ce que la garde ci-dessus
 * l'autorise à être écrasé. Ce fichier (web.c) ne contient TOUJOURS aucun
 * esp_ota_begin/esp_ota_write — il délègue entièrement à ota.c
 * (ota_appliquer_flux), qui porte seul ces appels et ses propres
 * vérifications ; ce fichier se contente de recevoir la réponse, de répondre
 * au client, puis de redémarrer après un court délai (même discipline que
 * /revert ci-dessous). rescue.c (rollback automatique) n'est pas
 * réimplémenté ici ni dans ota.c : ota_appliquer_flux() se contente
 * d'appeler rescue_arm() après un set_boot_partition() réussi. L'itération
 * sur le pinout repasse par /revert puis par le /update du firmware
 * d'origine (voir docs/hardware/flashing.md).
 *
 * La BASCULE elle-même reste en POST délibérément : en GET, n'importe quelle
 * requête d'un navigateur (préchargement d'URL, restauration d'onglet au
 * démarrage), d'un aspirateur de liens ou d'un scanner réseau redémarrerait
 * l'appareil. Le GET /revert, lui, ne fait que SERVIR une page avec un bouton —
 * il ne redémarre jamais rien de lui-même ; c'est le clic sur le bouton qui
 * envoie le POST. On garde ainsi la propriété de sécurité d'origine tout en
 * permettant de déclencher la bascule depuis un simple navigateur (Firefox),
 * sans outil capable de POST. */

#include "web.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"

#include "boucle.h"
#include "etat_klipper.h"
#include "journal.h"
#include "klipper_fichiers.h"
#include "liaison.h"
#include "netlog.h"
#include "ota.h"
#include "pandatouch_display.h"
#include "rescue.h"
#include "web_macros.h"
#include "wifi.h"

static const char *TAG = "web";

/* Renseignés par app_main, volontairement découplés de LVGL et de rescue.c :
 * ce module ne connaît que ces deux valeurs, pas leur origine. */
static bool tactile_disponible;
static uint32_t compteur_demarrages;

/* Plus petite marge de pile jamais observée dans gestion_state() (voir plus
 * bas) : UINT32_MAX au départ, jamais réellement atteinte par
 * uxTaskGetStackHighWaterMark(), donc la première requête journalise
 * toujours. Lue et écrite uniquement par la tâche httpd (aucun accès
 * concurrent, donc pas de mutex nécessaire) — ce serveur ne traite qu'une
 * requête à la fois par défaut (HTTPD_DEFAULT_CONFIG().max_open_sockets et
 * la configuration mono-tâche de esp_http_server ici). */
static uint32_t s_marge_pile_min = UINT32_MAX;

void web_set_touch_available(bool disponible)
{
    tactile_disponible = disponible;
}

void web_set_boot_count(uint32_t compteur)
{
    compteur_demarrages = compteur;
}

/* Chaîne statique fournie par app_main (raison_reset_nom()) -- gardée par
 * pointeur, jamais copiée, voir le contrat dans web.h. "?" tant qu'app_main
 * n'a pas appelé le setter (fenêtre courte : web_start() tourne après). */
static const char *raison_reset = "?";

void web_set_reset_reason(const char *nom)
{
    raison_reset = (nom != NULL) ? nom : "?";
}

static esp_err_t gestion_racine(httpd_req_t *req)
{
    static const char page[] =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>K-Touch custom</title></head><body>"
        "<h1>K-Touch custom</h1>"
        "<ul>"
        "<li><a href=\"/status\">/status</a> — état (JSON)</li>"
        "<li><a href=\"/state\">/state</a> — état Klipper courant (JSON)</li>"
        "<li><a href=\"/log\">/log</a> — journal réseau</li>"
        "<li><a href=\"/revert\">/revert</a> — bouton de redémarrage (bascule OTA)</li>"
        "<li><a href=\"/backup-btt\">/backup-btt</a> — sauvegarde du firmware BTT vers spiffs</li>"
        "<li><a href=\"/ota\">/ota</a> — mise a jour OTA (panneau : etat + verification a blanc + flash reel)</li>"
        "<li><a href=\"/restore-btt\">/restore-btt</a> — restauration du firmware BTT depuis la sauvegarde</li>"
        "<li><a href=\"/coredump\">/coredump</a> — dump du dernier crash (404 si aucun)</li>"
        "</ul></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

/* "absent"/"valide"/"corrompu" -- meme vocabulaire cote JSON (/status) et
   cote page HTML (/backup-btt) pour l'etat de la sauvegarde BTT. */
static const char *ota_backup_etat_nom(ota_backup_etat_t etat)
{
    switch (etat) {
    case OTA_BACKUP_VALIDE:
        return "valide";
    case OTA_BACKUP_CORROMPU:
        return "corrompu";
    case OTA_BACKUP_ABSENT:
    default:
        return "absent";
    }
}

static esp_err_t gestion_status(httpd_req_t *req)
{
    const esp_partition_t *courante = esp_ota_get_running_partition();
    const esp_app_desc_t *description = esp_app_get_description();
    char adresse_ip[16];
    wifi_ip_string(adresse_ip, sizeof(adresse_ip));

    /* ota_backup_etat() relit spiffs et recalcule un SHA-256 dessus (voir
       ota.c) : plus couteux qu'un simple champ en mémoire, mais borné (au
       plus quelques Mio en streaming) et /status n'est pas interrogé en
       boucle serrée comme /state (voir son commentaire de tête). */
    const char *backup_btt = ota_backup_etat_nom(ota_backup_etat());

    /* Empreinte ELF : la version git seule ne distingue PAS deux builds
       construits autour du même commit (constaté : deux binaires "4d22d1e",
       un par slot) -- voir le bloc correspondant d'app_main.c. */
    char empreinte_elf[17];
    esp_app_get_elf_sha256(empreinte_elf, sizeof(empreinte_elf));

    char reponse[512];
    int longueur = snprintf(reponse, sizeof(reponse),
        "{"
        "\"slot\":\"%s\","
        "\"version\":\"%s\","
        "\"sha\":\"%s\","
        "\"reset\":\"%s\","
        "\"ip\":\"%s\","
        "\"uptime_ms\":%" PRId64 ","
        "\"free_heap\":%" PRIu32 ","
        "\"heap_interne\":%" PRIu32 ","
        "\"tactile\":%s,"
        "\"boot_count\":%" PRIu32 ","
        "\"backup_btt\":\"%s\""
        "}",
        courante != NULL ? courante->label : "?",
        description != NULL ? description->version : "?",
        empreinte_elf,
        raison_reset,
        adresse_ip,
        (int64_t)(esp_timer_get_time() / 1000),
        (uint32_t)esp_get_free_heap_size(),
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        tactile_disponible ? "true" : "false",
        compteur_demarrages,
        backup_btt);

    if (longueur < 0) {
        /* snprintf a échoué : ne pas envoyer une réponse tronquée étiquetée
         * comme JSON valide. */
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    size_t a_envoyer = (size_t)longueur < sizeof(reponse) ? (size_t)longueur : sizeof(reponse) - 1;
    return httpd_resp_send(req, reponse, a_envoyer);
}

/* GET /state — seul moyen, à ce jalon, de vérifier à distance que
 * l'analyseur (moonraker_parse.c) lit correctement une vraie machine : il
 * n'y a pas encore d'écran pour l'afficher (sous-jalon 2b).
 *
 * boucle_instantane() est utilisé ici, JAMAIS boucle_etat() : ce dernier
 * rend un pointeur BRUT vers le tampon interne de la boucle, valide
 * seulement jusqu'au prochain cycle de la tâche d'interrogation (~1 s, voir
 * le commentaire de boucle_etat() dans boucle.h). Cette tâche httpd tourne
 * dans son propre contexte, sans aucune garantie d'être relue avant que la
 * tâche d'interrogation ne remette à zéro ce même tampon pour le cycle
 * suivant — un simple délai de scheduler suffirait à transformer un
 * pointeur brut en lecture de mémoire déjà écrasée. boucle_instantane()
 * copie sous mutex dans une structure locale à cette fonction (qui reste
 * valable et exacte quel que soit le temps que la construction du JSON
 * ci-dessous prend ensuite) ET lit `generation`/`liaison` dans la même
 * prise de verrou — contrairement à trois appels séparés
 * (boucle_etat_copier()+boucle_generation()+boucle_liaison()), qui
 * laisseraient la tâche d'interrogation permuter le magasin d'état entre
 * deux d'entre eux et exposeraient un client à une `generation` qui ne
 * correspond pas au contenu qu'il vient de lire. */
static esp_err_t gestion_state(httpd_req_t *req)
{
    /* JAMAIS un etat_klipper_t local (coredump du 2026-08-15, PANIC
     * LoadProhibited via listes de taches corrompues) : ~1,8 Ko sur la pile
     * httpd (4-6 Kio) SOUS la serialisation cJSON complete -- le
     * debordement sautait le canari et ecrasait le TCB voisin, panic differe
     * dans l'ordonnanceur, uniquement imprimante EN LIGNE (generation != 0
     * = chemin profond). Scratch PSRAM paresseux, meme patron que le tampon
     * de gestion_log() : serveur mono-tache, une requete a la fois. */
    static etat_klipper_t *etat_scratch;
    if (etat_scratch == NULL) {
        etat_scratch = (etat_klipper_t *)heap_caps_malloc(sizeof(*etat_scratch), MALLOC_CAP_SPIRAM);
        if (etat_scratch == NULL) {
            return httpd_resp_send_500(req);
        }
    }
    etat_klipper_t *etat_ptr = etat_scratch;
    uint32_t generation = 0;
    liaison_etat_t liaison = LIAISON_CONNEXION;
    bool copie_reussie = boucle_instantane(etat_ptr, sizeof(*etat_ptr), &generation, &liaison);

    /* Ni `copie_reussie` ni `generation != 0` ne suffisent seuls.
     * `copie_reussie` est faux si la boucle n'a jamais démarré (hôte non
     * configuré, ou boucle_demarrer() en échec) — mais RESTE VRAI dès que la
     * boucle a démarré, même si elle n'a encore jamais réussi le moindre
     * cycle : boucle_demarrer() réussit dès que demarrer() du backend
     * réussit, et pour le backend Moonraker, demarrer() ne fait que créer un
     * client HTTP, sans contacter quoi que ce soit (voir
     * backend_moonraker.c). Un hôte configuré mais injoignable produit donc
     * `copie_reussie == true` avec `etat` intégralement à zéro (le tampon
     * initial, jamais rempli par un rafraîchissement réussi) — publier ce
     * zéro reviendrait à présenter une machine jamais jointe comme une
     * machine mesurée au repos. `generation != 0` est le seul signal qui
     * distingue « pas encore de lecture réussie » de « tous les champs
     * valent authentiquement zéro » (voir aussi flashing.md) : elle ne passe
     * à une valeur non nulle qu'après un premier etat_store_valider()
     * réussi, donc après un premier cycle de rafraîchissement réellement
     * abouti. Publier `etat` sans ce garde serait une lecture fabriquée
     * présentée comme mesurée, à côté d'une `liaison`/`generation` pourtant
     * valides — exactement ce que ce jalon interdit. */
    bool etat_disponible = copie_reussie && generation != 0;

    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(racine, "liaison", liaison_nom(liaison));
    /* generation vaut 0 tant qu'aucun relevé n'a jamais été validé par la
     * boucle (boucle non démarrée, démarrée mais pas encore de premier cycle
     * réussi, ou démarrée et en échec permanent) — c'est le seul signal qui
     * distingue « pas encore de lecture » de « tous les champs valent
     * authentiquement zéro », d'où son importance documentée ici et dans
     * flashing.md. */
    cJSON_AddNumberToObject(racine, "generation", (double)generation);

    if (etat_disponible) {
        cJSON *etat_json = cJSON_AddObjectToObject(racine, "etat");
        if (etat_json != NULL) {
            cJSON_AddStringToObject(etat_json, "etat", etat_ptr->etat);

            /* v2 (tache 1, jalon 3a) : `extrudeurs` est un tableau des SEULS
             * chauffeurs presents -- un client ne doit jamais avoir a
             * deviner qu'un index absent du tableau signifie "n'existe pas"
             * plutot que "zero". `index` porte la position dans
             * etat_klipper_t::extrudeurs, puisque le tableau JSON, lui,
             * saute les absents. Les températures sont des `float`, promues
             * en `double` pour cJSON_AddNumberToObject() ; cJSON les imprime
             * avec "%1.15g"/"%1.17g" (cJSON_Print, print_number()), ce qui
             * dépend de la libc pour une implémentation complète de %g sur
             * les doubles -- CONFIG_LIBC_NEWLIB_NANO_FORMAT DOIT rester
             * désactivé (défaut de ce projet), la newlib "nano" ne
             * l'implémente pas et rendrait ces nombres silencieusement faux. */
            cJSON *extrudeurs = cJSON_AddArrayToObject(etat_json, "extrudeurs");
            if (extrudeurs != NULL) {
                for (uint8_t i = 0; i < KLIPPER_EXTRUDEURS_MAX; i++) {
                    if (!etat_ptr->extrudeurs[i].presente) {
                        continue;
                    }
                    cJSON *item = cJSON_CreateObject();
                    if (item == NULL) {
                        continue;
                    }
                    cJSON_AddNumberToObject(item, "index", i);
                    cJSON_AddNumberToObject(item, "actuelle", (double)etat_ptr->extrudeurs[i].actuelle);
                    cJSON_AddNumberToObject(item, "consigne", (double)etat_ptr->extrudeurs[i].consigne);
                    cJSON_AddItemToArray(extrudeurs, item);
                }
            }
            cJSON_AddNumberToObject(etat_json, "nb_extrudeurs", etat_ptr->nb_extrudeurs);
            cJSON_AddNumberToObject(etat_json, "outil_actif", etat_ptr->outil_actif);

            cJSON *plateau = cJSON_AddObjectToObject(etat_json, "plateau");
            if (plateau != NULL) {
                cJSON_AddBoolToObject(plateau, "presente", etat_ptr->plateau.presente);
                cJSON_AddNumberToObject(plateau, "actuelle", (double)etat_ptr->plateau.actuelle);
                cJSON_AddNumberToObject(plateau, "consigne", (double)etat_ptr->plateau.consigne);
            }

            cJSON *ventilateurs = cJSON_AddArrayToObject(etat_json, "ventilateurs");
            if (ventilateurs != NULL) {
                for (uint8_t i = 0; i < KLIPPER_VENTILATEURS_MAX; i++) {
                    if (!etat_ptr->ventilateurs[i].present) {
                        continue;
                    }
                    cJSON *item = cJSON_CreateObject();
                    if (item == NULL) {
                        continue;
                    }
                    cJSON_AddNumberToObject(item, "index", i);
                    cJSON_AddNumberToObject(item, "vitesse", (double)etat_ptr->ventilateurs[i].vitesse);
                    cJSON_AddItemToArray(ventilateurs, item);
                }
            }

            cJSON_AddItemToObject(etat_json, "position", cJSON_CreateFloatArray(etat_ptr->position, 3));
            cJSON_AddNumberToObject(etat_json, "axes_references", etat_ptr->axes_references);
            cJSON_AddBoolToObject(etat_json, "deplacement_absolu", etat_ptr->deplacement_absolu);

            cJSON_AddNumberToObject(etat_json, "vitesse_pct", etat_ptr->vitesse_pct);
            cJSON_AddNumberToObject(etat_json, "flux_pct", etat_ptr->flux_pct);
            cJSON_AddNumberToObject(etat_json, "babystep_z_um", etat_ptr->babystep_z_um);

            /* Tache 5 (panneau Limits, sous-projet "panneaux KlipperScreen") :
             * les quatre limites toolhead, memes noms de cle que les champs
             * etat_klipper_t (voir core/etat_klipper.h) -- 0 signifie "pas
             * encore recu", publie tel quel plutot que masque en null : un
             * client de /state qui veut distinguer les deux peut deja le
             * faire via `generation` (voir plus haut), meme convention que
             * vitesse_pct/flux_pct ci-dessus (0 = pas recu, publie sans
             * traitement special). */
            cJSON_AddNumberToObject(etat_json, "limite_velocity", (double)etat_ptr->limite_velocity);
            cJSON_AddNumberToObject(etat_json, "limite_accel", (double)etat_ptr->limite_accel);
            cJSON_AddNumberToObject(etat_json, "limite_square_corner", (double)etat_ptr->limite_square_corner);
            cJSON_AddNumberToObject(etat_json, "limite_accel_to_decel", (double)etat_ptr->limite_accel_to_decel);

            /* Tache 6 (panneau Retraction, sous-projet "panneaux
             * KlipperScreen") : les quatre champs firmware_retraction, meme
             * convention que limite_* ci-dessus (0 = pas encore recu ou
             * objet absent de la machine, publie tel quel). */
            cJSON_AddNumberToObject(etat_json, "retr_length", (double)etat_ptr->retr_length);
            cJSON_AddNumberToObject(etat_json, "retr_speed", (double)etat_ptr->retr_speed);
            cJSON_AddNumberToObject(etat_json, "retr_unretract_extra", (double)etat_ptr->retr_unretract_extra);
            cJSON_AddNumberToObject(etat_json, "retr_unretract_speed", (double)etat_ptr->retr_unretract_speed);

            /* `macros` en tableau de chaines : le nom de macro Klipper est
             * la seule information utile a un client, l'index dans le
             * tampon fixe etat_klipper_t::macros n'a pas de sens hors du
             * firmware.
             *
             * Fix round 1 (revue tache 1, MAJOR) : `nb_macros` est ecrit par
             * un producteur amont (voir web_macros.h) et n'est PAS garanti
             * <= KLIPPER_MACROS_MAX a ce point -- c'est exactement le cas
             * que `macros_tronquees` existe pour signaler. Passer
             * `etat_ptr->nb_macros` non borne a cJSON_CreateStringArray() ferait
             * lire ce dernier au-dela des KLIPPER_MACROS_MAX entrees
             * remplies de `noms_macros` ci-dessous, dereferencant de la
             * memoire de pile non initialisee comme autant de pointeurs de
             * chaine -- undefined behaviour, plantage probable, fuite
             * d'information possible sur une route de diagnostic. Une SEULE
             * valeur bornee (`n`) alimente a la fois la boucle de
             * remplissage et le compte passe a cJSON : aucune chance que les
             * deux divergent a nouveau. */
            uint8_t n = web_nb_macros_serialisables(etat_ptr->nb_macros);
            const char *noms_macros[KLIPPER_MACROS_MAX];
            for (uint8_t i = 0; i < n; i++) {
                noms_macros[i] = etat_ptr->macros[i];
            }
            cJSON_AddItemToObject(etat_json, "macros", cJSON_CreateStringArray(noms_macros, n));
            cJSON_AddBoolToObject(etat_json, "macros_tronquees", etat_ptr->macros_tronquees);

            /* Tache 2, jalon "browser de fichiers" : `fichiers` en tableau de
             * chaines, exactement comme `macros` ci-dessus. La liste ne vit
             * plus dans etat_klipper_t (sortie vers un store dedie, voir
             * klipper_fichiers.h : ~2 Ko dupliques dans chaque copie d'etat
             * epuisaient la RAM interne) -- on la lit ici sous verrou dans une
             * copie locale. `nb` est deja borne a KLIPPER_FICHIERS_MAX par
             * rpc_lire_fichiers(), mais on reborne par prudence (defense en
             * profondeur, meme esprit que `macros`/web_nb_macros_serialisables()
             * ci-dessus). */
            klipper_fichiers_t fics;
            klipper_fichiers_lire(&fics);
            uint8_t n_fichiers = fics.nb < KLIPPER_FICHIERS_MAX
                                      ? fics.nb
                                      : (uint8_t)KLIPPER_FICHIERS_MAX;
            const char *noms_fichiers[KLIPPER_FICHIERS_MAX];
            for (uint8_t i = 0; i < n_fichiers; i++) {
                noms_fichiers[i] = fics.noms[i];
            }
            cJSON_AddItemToObject(etat_json, "fichiers", cJSON_CreateStringArray(noms_fichiers, n_fichiers));
            cJSON_AddBoolToObject(etat_json, "fichiers_tronques", fics.tronques);

            cJSON_AddStringToObject(etat_json, "fichier", etat_ptr->fichier);
            cJSON_AddNumberToObject(etat_json, "progression", (double)etat_ptr->progression);
            cJSON_AddNumberToObject(etat_json, "temps_restant_s", (double)etat_ptr->temps_restant_s);
            cJSON_AddBoolToObject(etat_json, "impression_en_cours", etat_ptr->impression_en_cours);
            cJSON_AddBoolToObject(etat_json, "impression_en_pause", etat_ptr->impression_en_pause);
        }
    } else {
        /* `null`, jamais un objet rempli de zéros : dit honnêtement "rien à
         * publier" plutôt que de laisser croire à une machine au repos.
         * `liaison` et `generation` ci-dessus restent significatifs seuls. */
        cJSON_AddNullToObject(racine, "etat");
    }

    /* cJSON plutôt qu'un snprintf à la main (voir gestion_status()
     * ci-dessus, qui peut se permettre le snprintf parce qu'aucun de ses
     * champs ne vient d'ailleurs que de ce firmware) : `fichier` vient de
     * Moonraker, donc en dernier ressort d'un nom de fichier choisi par un
     * utilisateur — un guillemet ou un antislash dedans casserait un JSON
     * construit à la main sans qu'aucun test hôte ne puisse le voir, ceux-ci
     * ne travaillant que sur du JSON déjà écrit à la main en entrée. */
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t resultat = httpd_resp_send(req, texte, HTTPD_RESP_USE_STRLEN);
    cJSON_free(texte);

    /* Mesure, pas estimation : la tâche httpd garde la pile par défaut de
     * 4096 octets, et ce gestionnaire ajoute un arbre cJSON complet plus le
     * formatage %g pleine précision de newlib pour chaque flottant. Journalisé
     * uniquement quand la marge empire : /state a vocation à être interrogée
     * en boucle (c'est son usage prévu à l'étape de vérification), et netlog
     * est un tampon circulaire de 16 Kio qui écrase silencieusement ses plus
     * anciennes lignes — une trace à CHAQUE requête chasserait la séquence de
     * démarrage et les alertes en quelques centaines de requêtes, soit le
     * seul canal de diagnostic de l'appareil. Ce filtre donne une ligne à la
     * première requête, puis seulement quand le pire jamais observé
     * s'aggrave — ce qui reste l'information recherchée (le minimum jamais
     * atteint) tout en bornant sa fréquence. */
    uint32_t marge_pile = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if (marge_pile < s_marge_pile_min) {
        s_marge_pile_min = marge_pile;
        JOURNAL_INFO(TAG, "gestion_state : nouvelle marge de pile minimale %u octets",
                     (unsigned)marge_pile);
    }

    return resultat;
}

static esp_err_t gestion_log(httpd_req_t *req)
{
    /* Jamais sur la pile de la tâche httpd (16 Kio), et EN PSRAM plutôt
     * qu'en .bss RAM interne depuis le fix RAM interne du 2026-08-14 (même
     * raison que le tampon de netlog.c, voir son commentaire). Allocation
     * paresseuse au premier /log, conservée ensuite ; une seule requête à la
     * fois sur ce serveur mono-tâche, pas de verrou nécessaire. */
    static char *instantane;
    if (instantane == NULL) {
        instantane = (char *)heap_caps_malloc(NETLOG_TAILLE, MALLOC_CAP_SPIRAM);
        if (instantane == NULL) {
            return httpd_resp_send_500(req);
        }
    }
    size_t longueur = netlog_snapshot(instantane, NETLOG_TAILLE);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, instantane, longueur);
}

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
/* GET /coredump : rapatrie le dump ELF brut du dernier crash (partition
 * `coredump`, déjà présente dans la table BTT d'origine -- voir
 * partitions.csv et le bloc coredump d'app_main.c). Analyse sur PC :
 * `esp-coredump info_corefile -c coredump.bin -t raw build/ktouch-custom.elf`
 * avec l'ELF du build dont l'empreinte (/status, champ "sha") correspond.
 * Un GET est sûr ici (lecture seule, même principe que /log) ; le dump n'est
 * jamais effacé par cette route, le prochain crash l'écrase de lui-même. */
static esp_err_t gestion_coredump(httpd_req_t *req)
{
    size_t adresse = 0;
    size_t taille = 0;
    if (esp_core_dump_image_get(&adresse, &taille) != ESP_OK || taille == 0) {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, "aucun coredump en flash\n", HTTPD_RESP_USE_STRLEN);
    }

    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (partition == NULL || adresse < partition->address ||
        (adresse - partition->address) + taille > partition->size) {
        /* Image annoncée hors de la partition : ne rien servir plutôt que de
           lire de la flash arbitraire. */
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"coredump.bin\"");

    /* Statique, même raison que le tampon de gestion_log() ci-dessus : la
       pile de la tâche httpd est comptée (voir s_marge_pile_min) ; une seule
       requête à la fois sur ce serveur mono-tâche, pas de concurrence. */
    static char morceau[1024];
    size_t decalage = adresse - partition->address;
    size_t restant = taille;
    bool entame = false; /* au moins un chunk parti = les en-têtes 200 sont partis */
    while (restant > 0) {
        size_t a_lire = restant < sizeof(morceau) ? restant : sizeof(morceau);
        esp_err_t lu = esp_partition_read(partition, decalage, morceau, a_lire);
        if (lu != ESP_OK) {
            /* JAMAIS silencieux (revue du 2026-08-14, L3) : un dump tronqué
               fait échouer esp-coredump de façon inintelligible, autant que
               /log dise pourquoi. Avant le premier chunk, les en-têtes ne
               sont PAS encore partis (httpd ne les émet qu'au premier
               send_chunk) : un vrai 500 est encore possible. Après, on ne
               peut plus que tronquer. */
            ESP_LOGE(TAG, "coredump : lecture flash echouee a l'offset %u : %s",
                     (unsigned)decalage, esp_err_to_name(lu));
            if (!entame) {
                return httpd_resp_send_500(req);
            }
            break;
        }
        if (httpd_resp_send_chunk(req, morceau, a_lire) != ESP_OK) {
            ESP_LOGE(TAG, "coredump : envoi interrompu (%u octets restants)", (unsigned)restant);
            break;
        }
        entame = true;
        decalage += a_lire;
        restant -= a_lire;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}
#endif

static esp_err_t gestion_revert_page(httpd_req_t *req)
{
    /* GET /revert : sert une page HTML avec un bouton qui, lui, fait le
     * POST /revert. Permet de déclencher la bascule depuis un navigateur
     * (Firefox) sans outil capable d'émettre un POST. Un GET ne redémarre
     * JAMAIS l'appareil (voir la justification de sécurité en tête de fichier) :
     * un préchargement, un scanner ou un aspirateur de liens qui frappe cette
     * route ne fait que récupérer ce HTML inoffensif. La page est
     * auto-portée (aucune ressource externe) pour rester joignable sur un
     * réseau isolé. */
    static const char page[] =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Redemarrer la K-Touch</title></head>"
        "<body style=\"font-family:sans-serif;max-width:32em;margin:2em auto;padding:0 1em\">"
        "<h1>Redemarrer sur l'autre firmware</h1>"
        "<p>Bascule vers l'autre slot OTA puis redemarre — a utiliser apres un "
        "flash pour demarrer le nouveau firmware, ou pour revenir au firmware "
        "d'origine.</p>"
        "<form method=\"POST\" action=\"/revert\">"
        "<button type=\"submit\" style=\"font-size:1.2em;padding:.6em 1.2em\">"
        "Redemarrer maintenant</button></form>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t gestion_revert(httpd_req_t *req)
{
    /* La bascule proprement dite (vérification SHA-256 de l'image cible,
     * éventuel effacement d'otadata en dernier recours, esp_restart() qui
     * invoque esp_wifi_stop) est déléguée à rescue_switch_now(), donc à la
     * tâche dédiée de rescue.c : la pile de la tâche httpd n'a pas vocation
     * à porter ce travail-là. On répond d'abord, pour que le client reçoive
     * confirmation avant que le réseau ne soit coupé. */
    httpd_resp_sendstr(req, "bascule demandee, redemarrage\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    rescue_switch_now();
    return ESP_OK;
}

static esp_err_t gestion_backup_btt_page(httpd_req_t *req)
{
    /* GET /backup-btt : meme principe que GET /revert ci-dessus -- sert une
       page HTML avec un bouton qui, lui, fait le POST. Un GET ne declenche
       JAMAIS la sauvegarde (aucune ecriture flash ici), pour la meme raison
       de securite que /revert : un prechargement, un scanner ou un
       aspirateur de liens ne doit jamais avoir d'effet de bord flash. */
    ota_backup_etat_t etat = ota_backup_etat();
    char page[1200];
    int longueur = snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Sauvegarde du firmware BTT</title></head>"
        "<body style=\"font-family:sans-serif;max-width:32em;margin:2em auto;padding:0 1em\">"
        "<h1>Sauvegarde du firmware BTT</h1>"
        "<p>Etat actuel de la sauvegarde : <strong>%s</strong></p>"
        "<p>Copie l'image BTT (partition app0) vers la partition spiffs, "
        "inutilisee par ce firmware, avec verification SHA-256 apres relecture. "
        "app0 n'est JAMAIS modifiee par cette operation -- relancable a volonte, "
        "sans le moindre risque pour le demarrage. Prend quelques secondes.</p>"
        "<form method=\"POST\" action=\"/backup-btt\">"
        "<button type=\"submit\" style=\"font-size:1.2em;padding:.6em 1.2em\">"
        "Lancer la sauvegarde maintenant</button></form>"
        "</body></html>",
        ota_backup_etat_nom(etat));

    if (longueur < 0) {
        /* Meme garde que gestion_status() : ne jamais envoyer une reponse
           tronquee etiquetee comme HTML valide si snprintf a echoue. */
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t a_envoyer = (size_t)longueur < sizeof(page) ? (size_t)longueur : sizeof(page) - 1;
    return httpd_resp_send(req, page, a_envoyer);
}

static esp_err_t gestion_backup_btt(httpd_req_t *req)
{
    /* ota_backup_btt() delegue le travail flash (lecture app0, effacement +
       ecriture spiffs, relecture de verification -- quelques secondes) a sa
       propre tache dediee interne (voir ota.c) : cet appel bloque jusqu'a la
       fin, mais sur un semaphore, jamais en boucle d'E/S flash active sur
       CETTE pile-ci (celle de la tache httpd). N'ecrit jamais dans un slot
       app -- seulement dans spiffs. */
    char msg[160];
    /* Ecran noir pendant l'ecriture spiffs (meme raison que gestion_ota_post :
       le cache flash coupe affame la dalle RGB, ~4,5 Mo ici). PAS de reboot sur
       ce chemin -> restaurer le retroeclairage dans tous les cas, succes comme
       echec. */
    uint32_t retro = pt_backlight_get();
    pt_backlight_set(0);
    esp_err_t resultat = ota_backup_btt(msg, sizeof(msg));
    pt_backlight_set(retro);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (resultat != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    return httpd_resp_sendstr(req, msg);
}

/* GET /ota : panneau de controle complet -- etat (slot/version/sauvegarde
   BTT) + verification a blanc (dry-run) + ecriture reelle (flash), celle-ci
   gardee cote navigateur par un dry-run reussi sur le MEME fichier (bouton
   desactive tant que ce n'est pas le cas -- la garde faisant foi reste bien
   sur cote firmware, voir ota.c) + boutons de sauvegarde/restauration du
   firmware BTT.
   Deliberement PAS un <form multipart> classique : ota_verifier_flux() (voir
   ota.c) lit le corps de la requete comme un flux OCTET BRUT (httpd_req_recv
   direct, sans parseur multipart/form-data cote firmware) -- un vrai
   formulaire multipart envelopperait le fichier choisi dans des
   en-tetes/limites qui casseraient le controle du magic 0xE9 des les
   premiers octets. La page utilise donc un `<input type="file">` lu cote
   navigateur (File API) et poste son contenu BRUT via fetch()/XHR, ce qui
   correspond exactement a ce que le firmware sait lire. */
static esp_err_t gestion_ota_page(httpd_req_t *req)
{
    const esp_partition_t *courante = esp_ota_get_running_partition();
    const esp_app_desc_t *description = esp_app_get_description();
    const char *backup_btt = ota_backup_etat_nom(ota_backup_etat());

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    /* En-tete : seules les 3 valeurs d'etat passent par snprintf (tampon
       modeste). Le gros corps HTML+JS ci-dessous est un litteral statique sans
       aucun format, envoye tel quel en chunk -- pas de tampon geant, et
       -Werror=format-truncation reste hors de portee sur la partie volumineuse.
       Le corps POSTe le fichier en octet BRUT (File API + XHR/fetch), jamais un
       form multipart, pour rester compatible avec la lecture octet brut cote
       firmware (magic 0xE9 des les premiers octets). */
    char tete[640];
    int n = snprintf(tete, sizeof(tete),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>OTA K-Touch</title></head>"
        "<body style=\"font-family:sans-serif;max-width:34em;margin:1.5em auto;padding:0 1em\">"
        "<h1>Mise a jour OTA</h1>"
        "<p>Slot : <strong id=\"slot\">%s</strong> — version : "
        "<strong id=\"ver\">%s</strong> — sauvegarde BTT : "
        "<strong id=\"bkp\">%s</strong> "
        "<button type=\"button\" onclick=\"rafraichir()\">Rafraichir</button></p>",
        courante != NULL ? courante->label : "?",
        description != NULL ? description->version : "?",
        backup_btt);
    if (n < 0) {
        return httpd_resp_send_500(req);
    }
    esp_err_t e = httpd_resp_send_chunk(req, tete, HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }

    static const char corps[] =
        "<h2>Firmware</h2>"
        "<p>SHA-256 attendu (optionnel) :<br>"
        "<input type=\"text\" id=\"sha\" size=\"66\" placeholder=\"64 hex, optionnel\"></p>"
        "<p>Fichier .bin : <input type=\"file\" id=\"fichier\" accept=\".bin\"></p>"
        "<p><button type=\"button\" onclick=\"verifier()\">Verifier (dry-run)</button> "
        "<button type=\"button\" id=\"btnflash\" onclick=\"flasher()\" disabled>"
        "Flasher (ecriture reelle)</button></p>"
        "<progress id=\"prog\" value=\"0\" max=\"100\" style=\"width:100%;display:none\"></progress>"
        "<pre id=\"res\" style=\"white-space:pre-wrap;background:#eee;padding:.5em;min-height:2em\"></pre>"
        "<h2>Sauvegarde BTT</h2>"
        "<p><button type=\"button\" onclick=\"sauver()\">Sauvegarder BTT vers spiffs</button> "
        "<button type=\"button\" onclick=\"restaurer()\">Restaurer BTT (redemarre sur BTT)</button></p>"
        "<pre id=\"resb\" style=\"white-space:pre-wrap;background:#eee;padding:.5em;min-height:2em\"></pre>"
        "<script>"
        "var okFichier=null;"
        "function elt(i){return document.getElementById(i);}"
        "function majFlash(){elt('btnflash').disabled=(okFichier===null||elt('fichier').files[0]!==okFichier);}"
        "elt('fichier').addEventListener('change',function(){okFichier=null;majFlash();elt('res').textContent='';});"
        "function verifier(){"
        "var f=elt('fichier').files[0];var r=elt('res');"
        "if(!f){r.textContent='choisir un fichier .bin d\\'abord';return;}"
        "var sha=elt('sha').value;"
        "var url='/ota?dry_run=1'+(sha?('&sha='+encodeURIComponent(sha)):'');"
        "r.textContent='verification en cours...';"
        "fetch(url,{method:'POST',body:f}).then(function(resp){return resp.text().then(function(t){"
        "r.textContent='HTTP '+resp.status+'\\n'+t;"
        "if(resp.status===200&&t.indexOf('image valide')>=0){okFichier=f;}majFlash();"
        "});}).catch(function(x){r.textContent='erreur reseau : '+x;});"
        "}"
        "function flasher(){"
        "var f=elt('fichier').files[0];var r=elt('res');"
        "if(!f||f!==okFichier){r.textContent='faire un dry-run valide sur ce fichier d\\'abord';return;}"
        "if(!confirm('Ecriture REELLE dans le slot inactif, puis redemarrage. Continuer ?'))return;"
        "var p=elt('prog');p.style.display='block';p.value=0;"
        "var x=new XMLHttpRequest();"
        "x.upload.onprogress=function(ev){if(ev.lengthComputable){p.value=Math.round(ev.loaded/ev.total*100);}};"
        "x.onload=function(){r.textContent='HTTP '+x.status+'\\n'+x.responseText+"
        "(x.status===200?'\\n(l\\'ecran va noircir puis redemarrer)':'');};"
        "x.onerror=function(){r.textContent='connexion coupee (attendu si l\\'appareil redemarre)';};"
        "x.open('POST','/ota');x.send(f);"
        "}"
        "function sauver(){"
        "if(!confirm('Sauvegarder le firmware BTT (app0) vers spiffs ? Quelques secondes.'))return;"
        "var r=elt('resb');r.textContent='sauvegarde en cours...';"
        "fetch('/backup-btt',{method:'POST'}).then(function(resp){return resp.text().then(function(t){"
        "r.textContent='HTTP '+resp.status+'\\n'+t;rafraichir();"
        "});}).catch(function(x){r.textContent='erreur reseau : '+x;});"
        "}"
        "function restaurer(){"
        "if(!confirm('RESTAURER BTT : reecrit le slot inactif et REDEMARRE la dalle sur le firmware BTT. Continuer ?'))return;"
        "var r=elt('resb');r.textContent='restauration en cours...';"
        "fetch('/restore-btt',{method:'POST'}).then(function(resp){return resp.text().then(function(t){"
        "r.textContent='HTTP '+resp.status+'\\n'+t;"
        "});}).catch(function(x){r.textContent='connexion coupee (attendu si l\\'appareil redemarre)';});"
        "}"
        "function rafraichir(){"
        "fetch('/status').then(function(r){return r.json();}).then(function(j){"
        "elt('slot').textContent=j.slot;elt('ver').textContent=j.version;elt('bkp').textContent=j.backup_btt;"
        "}).catch(function(){});"
        "}"
        "</script>"
        "</body></html>";
    e = httpd_resp_send_chunk(req, corps, HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* POST /ota : route unique, distinguee par la query `?dry_run=1` (+
   `?sha=...` optionnel, dry-run seulement). `dry_run=1` route vers
   ota_verifier_flux() (tache 4, inchangee) -- RIEN n'est jamais ecrit en
   flash sur ce chemin. Tout le reste (absent, ou different de "1") route
   desormais vers le COMMIT REEL, ota_appliquer_flux() (tache 5, ota.c) --
   garde par une sauvegarde BTT valide tant qu'app0 n'a pas deja ete ecrase
   (voir ota.h pour le detail de la garde). */
static esp_err_t gestion_ota_post(httpd_req_t *req)
{
    bool dry_run = false;
    char sha[65] = {0};

    size_t longueur_query = httpd_req_get_url_query_len(req);
    if (longueur_query > 0) {
        /* +1 pour le terminateur nul attendu par httpd_req_get_url_query_str().
           Une query plus longue que ce tampon est tronquee proprement (pas de
           depassement) -- au pire `dry_run`/`sha` restent absents/vides,
           traites comme "non fournis" plus bas. */
        char requete[192];
        size_t a_lire = longueur_query + 1 < sizeof(requete) ? longueur_query + 1 : sizeof(requete);
        if (httpd_req_get_url_query_str(req, requete, a_lire) == ESP_OK) {
            char valeur_dry_run[8];
            if (httpd_query_key_value(requete, "dry_run", valeur_dry_run, sizeof(valeur_dry_run)) == ESP_OK) {
                dry_run = (strcmp(valeur_dry_run, "1") == 0);
            }
            /* Ignore silencieusement si absent ou trop long (> 64 caracteres,
               taille de `sha`) : ota_verifier_flux() traite alors sha comme
               non fourni (chaine vide) plutot que de faire echouer toute la
               requete pour un parametre optionnel mal forme. */
            httpd_query_key_value(requete, "sha", sha, sizeof(sha));
        }
    }

    if (dry_run) {
        /* ota_verifier_flux() lit le corps de `req` en flux (voir ota.c) : ne
           consomme jamais l'image entiere en RAM, et n'appelle jamais
           esp_ota_begin/write -- dry-run reel, pas seulement par convention de
           nommage de cette route. */
        char msg[256];
        esp_err_t resultat = ota_verifier_flux(req, sha[0] != '\0' ? sha : NULL, msg, sizeof(msg));
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        if (resultat != ESP_OK) {
            httpd_resp_set_status(req, "422 Unprocessable Entity");
        }
        return httpd_resp_sendstr(req, msg);
    }

    /* Commit reel : ota_appliquer_flux() (ota.c) porte a elle seule la garde
       (refus sans sauvegarde BTT valide tant qu'app0 n'a pas deja ete
       ecrase), la reception/ecriture en flux, la validation esp_ota_end(),
       esp_ota_set_boot_partition() et l'armement du filet rescue.c -- tout
       cela AVANT de revenir ici. Ce fichier ne fait toujours jamais lui-meme
       esp_ota_begin/write (voir le commentaire de tete de ce fichier). */
    char msg[256];
    /* Ecran noir pendant l'ecriture : un erase/write flash desactive le cache
       flash, ce qui affame le DMA de la dalle RGB (framebuffer en PSRAM) et
       affiche du bruit. pt_backlight_set() ecrit un duty LEDC (registre, rapide,
       maintenu par le peripherique pendant l'ecriture). Sur echec, on ne
       redemarre pas -> restaurer. Sur succes, le chemin plus bas repond puis
       esp_restart() et l'ecran revient au boot. */
    uint32_t retro = pt_backlight_get();
    pt_backlight_set(0);
    esp_err_t resultat = ota_appliquer_flux(req, msg, sizeof(msg));
    if (resultat != ESP_OK) {
        pt_backlight_set(retro);
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (resultat != ESP_OK) {
        /* 409 : la garde a refuse (sauvegarde BTT absente/invalide) -- le
           client peut corriger (POST /backup-btt) et reessayer, ce n'est pas
           une erreur sur cette requete-ci. 400 : image structurellement
           invalide (magic errone/taille aberrante/argument invalide). Tout
           le reste (reception interrompue, echec d'ecriture ou de validation
           esp_ota_end/set_boot_partition) reste une erreur serveur : rien de
           tout cela n'est imputable au contenu envoye. */
        if (resultat == ESP_ERR_INVALID_STATE) {
            httpd_resp_set_status(req, "409 Conflict");
        } else if (resultat == ESP_ERR_INVALID_VERSION || resultat == ESP_ERR_INVALID_SIZE
                   || resultat == ESP_ERR_INVALID_ARG) {
            httpd_resp_set_status(req, "400 Bad Request");
        } else {
            httpd_resp_set_status(req, "500 Internal Server Error");
        }
        return httpd_resp_sendstr(req, msg);
    }

    /* Succes : repondre AVANT de redemarrer, pour que le client recoive
       confirmation avant que le reseau ne soit coupe -- meme principe que
       gestion_revert() ci-dessus. esp_restart() est appele ICI, directement
       -- PAS rescue_switch_now() : ce dernier basculerait vers l'AUTRE slot
       (le precedent), ce qui annulerait le commit qui vient de reussir.
       ota_appliquer_flux() a deja appele esp_ota_set_boot_partition() sur la
       BONNE cible et arme rescue_arm() (le filet de sauvetage normal, pour
       le cas ou CETTE nouvelle image ne rejoindrait jamais le reseau) avant
       de revenir ici. */
    httpd_resp_sendstr(req, "mise a jour ecrite, redemarrage\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t gestion_restore_btt_page(httpd_req_t *req)
{
    /* GET /restore-btt : meme principe que GET /revert et GET /backup-btt
       ci-dessus -- sert une page HTML avec un bouton qui, lui, fait le POST.
       Un GET ne declenche JAMAIS la restauration (aucune ecriture flash ici),
       meme raison de securite que les routes soeurs : un prechargement, un
       scanner ou un aspirateur de liens ne doit jamais avoir d'effet de bord
       flash. */
    ota_backup_etat_t etat = ota_backup_etat();
    char page[1200];
    int longueur = snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Restauration du firmware BTT</title></head>"
        "<body style=\"font-family:sans-serif;max-width:32em;margin:2em auto;padding:0 1em\">"
        "<h1>Restauration du firmware BTT</h1>"
        "<p>Etat actuel de la sauvegarde : <strong>%s</strong></p>"
        "<p>Ecrit la sauvegarde BTT (partition spiffs) dans le slot OTA inactif "
        "puis redemarre dessus — l'assurance qui rend une mise a jour OTA "
        "reversible. Refuse si aucune sauvegarde BTT valide n'existe (voir "
        "/backup-btt) — aucune ecriture flash dans ce cas. "
        "<strong>Redemarre l'appareil sur BTT en cas de succes.</strong> "
        "Prend quelques secondes.</p>"
        "<form method=\"POST\" action=\"/restore-btt\">"
        "<button type=\"submit\" style=\"font-size:1.2em;padding:.6em 1.2em\">"
        "Restaurer BTT maintenant</button></form>"
        "</body></html>",
        ota_backup_etat_nom(etat));

    if (longueur < 0) {
        /* Meme garde que gestion_status()/gestion_backup_btt_page() : ne
           jamais envoyer une reponse tronquee etiquetee comme HTML valide si
           snprintf a echoue. */
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t a_envoyer = (size_t)longueur < sizeof(page) ? (size_t)longueur : sizeof(page) - 1;
    return httpd_resp_send(req, page, a_envoyer);
}

/* POST /restore-btt : restauration reelle -- ota_restaurer_btt() (ota.c)
   porte a elle seule la garde (refus sans sauvegarde BTT valide), la lecture
   spiffs, l'ecriture du slot OTA inactif, la validation esp_ota_end(),
   esp_ota_set_boot_partition() et l'armement du filet rescue.c -- tout cela
   AVANT de revenir ici. Ce fichier ne fait toujours jamais lui-meme
   esp_ota_begin/write (voir le commentaire de tete de ce fichier). */
static esp_err_t gestion_restore_btt(httpd_req_t *req)
{
    /* ota_restaurer_btt() delegue le travail flash (lecture spiffs, ecriture
       du slot OTA inactif, relecture/validation -- quelques secondes) a sa
       propre tache dediee interne (voir ota.c), meme motif que
       ota_backup_btt() plus haut : cet appel bloque jusqu'a la fin, mais sur
       un semaphore, jamais en boucle d'E/S flash active sur CETTE pile-ci
       (celle de la tache httpd). */
    char msg[256];
    /* Meme masquage que gestion_ota_post() : ecran noir pendant l'ecriture
       flash de la restauration, restaure sur echec (pas de reboot), reste noir
       sur succes (esp_restart plus bas). */
    uint32_t retro = pt_backlight_get();
    pt_backlight_set(0);
    esp_err_t resultat = ota_restaurer_btt(msg, sizeof(msg));
    if (resultat != ESP_OK) {
        pt_backlight_set(retro);
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (resultat != ESP_OK) {
        /* 409 : la garde a refuse (sauvegarde BTT absente/invalide, voir
           ota_restaurer_btt()) -- le client peut corriger (POST /backup-btt)
           et reessayer, ce n'est pas une erreur sur cette requete-ci. Tout le
           reste (partition introuvable, image trop grande pour le slot,
           echec de lecture/ecriture/validation) reste une erreur serveur :
           rien de tout cela n'est imputable au client. */
        httpd_resp_set_status(req, resultat == ESP_ERR_INVALID_STATE ? "409 Conflict"
                                                                      : "500 Internal Server Error");
        return httpd_resp_sendstr(req, msg);
    }

    /* Succes : repondre AVANT de redemarrer, pour que le client recoive
       confirmation avant que le reseau ne soit coupe -- meme principe que
       gestion_revert()/gestion_ota_post() ci-dessus. ota_restaurer_btt() a
       deja appele esp_ota_set_boot_partition() sur la BONNE cible (le slot
       qui vient de recevoir BTT) et arme rescue_arm() avant de revenir ici. */
    httpd_resp_sendstr(req, "restauration ecrite, redemarrage sur BTT\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static void enregistrer_route(httpd_handle_t serveur, const httpd_uri_t *route)
{
    esp_err_t erreur = httpd_register_uri_handler(serveur, route);
    if (erreur != ESP_OK) {
        /* Une route de secours qui ne s'enregistre pas silencieusement est
         * pire qu'une route absente : au moins ici c'est visible dans /log. */
        ESP_LOGE(TAG, "echec d'enregistrement de la route '%s' : %s", route->uri, esp_err_to_name(erreur));
    }
}

esp_err_t web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* 12 routes sont enregistrees plus bas. Le defaut de HTTPD_DEFAULT_CONFIG
       pour max_uri_handlers vaut 8 : sans ce relevement, l'enregistrement des
       routes au-dela de la 8e echouait silencieusement (ESP_ERR_HTTPD_HANDLERS_FULL,
       visible seulement dans /log), et ces URI repondaient 404 -- notamment
       GET/POST /ota et GET/POST /restore-btt. Invisible a la compilation comme
       en host-test (echec d'EXECUTION). 16 laisse de la marge pour de futures
       routes sans re-toucher ceci. */
    config.max_uri_handlers = 16;
    /* Pile relevée (coredump du 2026-08-15, débordement dans gestion_state
       imprimante EN LIGNE) : le défaut (4096) ne laissait aucune marge sous
       la sérialisation cJSON complète de /state -- l'etat_klipper_t local a
       été sorti de la pile (scratch PSRAM, voir gestion_state), ce relevé
       assume le RESTE (récursion cJSON, snprintf, TLS de send). 8 Kio en RAM
       interne : coût unique et borné, tâche pérenne. s_marge_pile_min (/log)
       continue de surveiller le minimum réel. */
    config.stack_size = 8192;

    httpd_handle_t serveur = NULL;
    esp_err_t erreur = httpd_start(&serveur, &config);
    if (erreur != ESP_OK) {
        return erreur;
    }

    static const httpd_uri_t route_racine = {
        .uri = "/", .method = HTTP_GET, .handler = gestion_racine, .user_ctx = NULL,
    };
    static const httpd_uri_t route_status = {
        .uri = "/status", .method = HTTP_GET, .handler = gestion_status, .user_ctx = NULL,
    };
    static const httpd_uri_t route_state = {
        .uri = "/state", .method = HTTP_GET, .handler = gestion_state, .user_ctx = NULL,
    };
    static const httpd_uri_t route_log = {
        .uri = "/log", .method = HTTP_GET, .handler = gestion_log, .user_ctx = NULL,
    };
    static const httpd_uri_t route_revert_page = {
        .uri = "/revert", .method = HTTP_GET, .handler = gestion_revert_page, .user_ctx = NULL,
    };
    static const httpd_uri_t route_revert = {
        .uri = "/revert", .method = HTTP_POST, .handler = gestion_revert, .user_ctx = NULL,
    };
    static const httpd_uri_t route_backup_btt_page = {
        .uri = "/backup-btt", .method = HTTP_GET, .handler = gestion_backup_btt_page, .user_ctx = NULL,
    };
    static const httpd_uri_t route_backup_btt = {
        .uri = "/backup-btt", .method = HTTP_POST, .handler = gestion_backup_btt, .user_ctx = NULL,
    };
    static const httpd_uri_t route_ota_page = {
        .uri = "/ota", .method = HTTP_GET, .handler = gestion_ota_page, .user_ctx = NULL,
    };
    static const httpd_uri_t route_ota_post = {
        .uri = "/ota", .method = HTTP_POST, .handler = gestion_ota_post, .user_ctx = NULL,
    };
    static const httpd_uri_t route_restore_btt_page = {
        .uri = "/restore-btt", .method = HTTP_GET, .handler = gestion_restore_btt_page, .user_ctx = NULL,
    };
    static const httpd_uri_t route_restore_btt = {
        .uri = "/restore-btt", .method = HTTP_POST, .handler = gestion_restore_btt, .user_ctx = NULL,
    };
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    static const httpd_uri_t route_coredump = {
        .uri = "/coredump", .method = HTTP_GET, .handler = gestion_coredump, .user_ctx = NULL,
    };
#endif

    enregistrer_route(serveur, &route_racine);
    enregistrer_route(serveur, &route_status);
    enregistrer_route(serveur, &route_state);
    enregistrer_route(serveur, &route_log);
    enregistrer_route(serveur, &route_revert_page);
    enregistrer_route(serveur, &route_revert);
    enregistrer_route(serveur, &route_backup_btt_page);
    enregistrer_route(serveur, &route_backup_btt);
    enregistrer_route(serveur, &route_ota_page);
    enregistrer_route(serveur, &route_ota_post);
    enregistrer_route(serveur, &route_restore_btt_page);
    enregistrer_route(serveur, &route_restore_btt);
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    enregistrer_route(serveur, &route_coredump);
#endif

    ESP_LOGI(TAG, "serveur HTTP demarre");
    return ESP_OK;
}
