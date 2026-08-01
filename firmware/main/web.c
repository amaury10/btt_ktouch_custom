/* Serveur HTTP : c'est la seule interface de contrôle du firmware une fois le
 * câble série hors jeu. Cinq routes :
 *   GET  /        page d'état minimale, avec liens vers les autres routes
 *   GET  /status  JSON : slot en cours, version, IP, uptime, mémoire libre,
 *                 tactile disponible, compteur de démarrages
 *   GET  /state   JSON : état de la liaison avec l'hôte, génération et
 *                 dernier état Klipper connu (voir gestion_state() plus bas)
 *   GET  /log     texte brut, contenu du journal réseau (netlog_snapshot())
 *   GET  /revert  page HTML avec un bouton qui déclenche le POST (pratique
 *                 depuis un navigateur : Firefox, sans outil POST)
 *   POST /revert  bascule vers l'autre slot et redémarre
 *
 * Délibérément AUCUNE route de mise à jour. Ce firmware tourne depuis app1
 * (le slot que l'OTA du firmware d'origine choisit) ; avec deux slots
 * seulement, le slot inactif vu depuis app1 est app0 — celui du firmware
 * d'origine. Un /update ici n'aurait nulle part ailleurs où écrire, et
 * esp_ota_begin(OTA_SIZE_UNKNOWN) efface la partition cible avant même de
 * recevoir un octet : la première mise à jour effacerait le firmware
 * d'origine, après quoi le sauvetage n'aurait plus rien vers quoi basculer.
 * Aucun esp_ota_begin/esp_ota_write ne doit figurer dans ce fichier — la
 * seule écriture flash de tout le firmware est celle d'otadata, dans
 * rescue.c. L'itération sur le pinout repasse par /revert puis par le
 * /update du firmware d'origine (voir docs/hardware/flashing.md).
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
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"

#include "boucle.h"
#include "etat_klipper.h"
#include "journal.h"
#include "liaison.h"
#include "netlog.h"
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
        "</ul></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t gestion_status(httpd_req_t *req)
{
    const esp_partition_t *courante = esp_ota_get_running_partition();
    const esp_app_desc_t *description = esp_app_get_description();
    char adresse_ip[16];
    wifi_ip_string(adresse_ip, sizeof(adresse_ip));

    char reponse[448];
    int longueur = snprintf(reponse, sizeof(reponse),
        "{"
        "\"slot\":\"%s\","
        "\"version\":\"%s\","
        "\"ip\":\"%s\","
        "\"uptime_ms\":%" PRId64 ","
        "\"free_heap\":%" PRIu32 ","
        "\"tactile\":%s,"
        "\"boot_count\":%" PRIu32
        "}",
        courante != NULL ? courante->label : "?",
        description != NULL ? description->version : "?",
        adresse_ip,
        (int64_t)(esp_timer_get_time() / 1000),
        (uint32_t)esp_get_free_heap_size(),
        tactile_disponible ? "true" : "false",
        compteur_demarrages);

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
    etat_klipper_t etat;
    uint32_t generation = 0;
    liaison_etat_t liaison = LIAISON_CONNEXION;
    bool copie_reussie = boucle_instantane(&etat, sizeof(etat), &generation, &liaison);

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
            cJSON_AddStringToObject(etat_json, "etat", etat.etat);

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
                    if (!etat.extrudeurs[i].presente) {
                        continue;
                    }
                    cJSON *item = cJSON_CreateObject();
                    if (item == NULL) {
                        continue;
                    }
                    cJSON_AddNumberToObject(item, "index", i);
                    cJSON_AddNumberToObject(item, "actuelle", (double)etat.extrudeurs[i].actuelle);
                    cJSON_AddNumberToObject(item, "consigne", (double)etat.extrudeurs[i].consigne);
                    cJSON_AddItemToArray(extrudeurs, item);
                }
            }
            cJSON_AddNumberToObject(etat_json, "nb_extrudeurs", etat.nb_extrudeurs);
            cJSON_AddNumberToObject(etat_json, "outil_actif", etat.outil_actif);

            cJSON *plateau = cJSON_AddObjectToObject(etat_json, "plateau");
            if (plateau != NULL) {
                cJSON_AddBoolToObject(plateau, "presente", etat.plateau.presente);
                cJSON_AddNumberToObject(plateau, "actuelle", (double)etat.plateau.actuelle);
                cJSON_AddNumberToObject(plateau, "consigne", (double)etat.plateau.consigne);
            }

            cJSON *ventilateurs = cJSON_AddArrayToObject(etat_json, "ventilateurs");
            if (ventilateurs != NULL) {
                for (uint8_t i = 0; i < KLIPPER_VENTILATEURS_MAX; i++) {
                    if (!etat.ventilateurs[i].present) {
                        continue;
                    }
                    cJSON *item = cJSON_CreateObject();
                    if (item == NULL) {
                        continue;
                    }
                    cJSON_AddNumberToObject(item, "index", i);
                    cJSON_AddNumberToObject(item, "vitesse", (double)etat.ventilateurs[i].vitesse);
                    cJSON_AddItemToArray(ventilateurs, item);
                }
            }

            cJSON_AddItemToObject(etat_json, "position", cJSON_CreateFloatArray(etat.position, 3));
            cJSON_AddNumberToObject(etat_json, "axes_references", etat.axes_references);
            cJSON_AddBoolToObject(etat_json, "deplacement_absolu", etat.deplacement_absolu);

            cJSON_AddNumberToObject(etat_json, "vitesse_pct", etat.vitesse_pct);
            cJSON_AddNumberToObject(etat_json, "flux_pct", etat.flux_pct);
            cJSON_AddNumberToObject(etat_json, "babystep_z_um", etat.babystep_z_um);

            /* `macros` en tableau de chaines : le nom de macro Klipper est
             * la seule information utile a un client, l'index dans le
             * tampon fixe etat_klipper_t::macros n'a pas de sens hors du
             * firmware.
             *
             * Fix round 1 (revue tache 1, MAJOR) : `nb_macros` est ecrit par
             * un producteur amont (voir web_macros.h) et n'est PAS garanti
             * <= KLIPPER_MACROS_MAX a ce point -- c'est exactement le cas
             * que `macros_tronquees` existe pour signaler. Passer
             * `etat.nb_macros` non borne a cJSON_CreateStringArray() ferait
             * lire ce dernier au-dela des KLIPPER_MACROS_MAX entrees
             * remplies de `noms_macros` ci-dessous, dereferencant de la
             * memoire de pile non initialisee comme autant de pointeurs de
             * chaine -- undefined behaviour, plantage probable, fuite
             * d'information possible sur une route de diagnostic. Une SEULE
             * valeur bornee (`n`) alimente a la fois la boucle de
             * remplissage et le compte passe a cJSON : aucune chance que les
             * deux divergent a nouveau. */
            uint8_t n = web_nb_macros_serialisables(etat.nb_macros);
            const char *noms_macros[KLIPPER_MACROS_MAX];
            for (uint8_t i = 0; i < n; i++) {
                noms_macros[i] = etat.macros[i];
            }
            cJSON_AddItemToObject(etat_json, "macros", cJSON_CreateStringArray(noms_macros, n));
            cJSON_AddBoolToObject(etat_json, "macros_tronquees", etat.macros_tronquees);

            /* Tache 2, jalon "browser de fichiers" : `fichiers` en tableau de
             * chaines, exactement comme `macros` ci-dessus, et pour la meme
             * raison (borner `nb_fichiers` -- un producteur amont n'est pas
             * garanti <= KLIPPER_FICHIERS_MAX, voir le commentaire du fix
             * MAJOR juste au-dessus pour `macros`/`noms_macros` -- meme piege,
             * meme defense). */
            uint8_t n_fichiers = etat.nb_fichiers < KLIPPER_FICHIERS_MAX
                                      ? etat.nb_fichiers
                                      : (uint8_t)KLIPPER_FICHIERS_MAX;
            const char *noms_fichiers[KLIPPER_FICHIERS_MAX];
            for (uint8_t i = 0; i < n_fichiers; i++) {
                noms_fichiers[i] = etat.fichiers[i];
            }
            cJSON_AddItemToObject(etat_json, "fichiers", cJSON_CreateStringArray(noms_fichiers, n_fichiers));
            cJSON_AddBoolToObject(etat_json, "fichiers_tronques", etat.fichiers_tronques);

            cJSON_AddStringToObject(etat_json, "fichier", etat.fichier);
            cJSON_AddNumberToObject(etat_json, "progression", (double)etat.progression);
            cJSON_AddNumberToObject(etat_json, "temps_restant_s", (double)etat.temps_restant_s);
            cJSON_AddBoolToObject(etat_json, "impression_en_cours", etat.impression_en_cours);
            cJSON_AddBoolToObject(etat_json, "impression_en_pause", etat.impression_en_pause);
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
    /* Statique : 16 Kio sur la pile de la tâche httpd serait excessif. */
    static char instantane[16 * 1024];
    size_t longueur = netlog_snapshot(instantane, sizeof(instantane));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, instantane, longueur);
}

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

    enregistrer_route(serveur, &route_racine);
    enregistrer_route(serveur, &route_status);
    enregistrer_route(serveur, &route_state);
    enregistrer_route(serveur, &route_log);
    enregistrer_route(serveur, &route_revert_page);
    enregistrer_route(serveur, &route_revert);

    ESP_LOGI(TAG, "serveur HTTP demarre");
    return ESP_OK;
}
