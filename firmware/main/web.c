/* Serveur HTTP : c'est la seule interface de contrôle du firmware une fois le
 * câble série hors jeu. Cinq routes :
 *   GET  /        page d'état minimale, avec liens vers les autres routes
 *   GET  /status  JSON : slot en cours, version, IP, uptime, mémoire libre,
 *                 tactile disponible, compteur de démarrages
 *   GET  /state   JSON : état de la liaison avec l'hôte, génération et
 *                 dernier état Klipper connu (voir gestion_state() plus bas)
 *   GET  /log     texte brut, contenu du journal réseau (netlog_snapshot())
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
 * /revert est en POST délibérément : en GET, n'importe quelle requête d'un
 * navigateur, d'un aspirateur de liens ou d'un scanner réseau redémarrerait
 * l'appareil. */

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
#include "liaison.h"
#include "netlog.h"
#include "rescue.h"
#include "wifi.h"

static const char *TAG = "web";

/* Renseignés par app_main, volontairement découplés de LVGL et de rescue.c :
 * ce module ne connaît que ces deux valeurs, pas leur origine. */
static bool tactile_disponible;
static uint32_t compteur_demarrages;

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
        "<li>POST /revert — bascule vers l'autre slot et redémarre</li>"
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
 * boucle_etat_copier() est utilisé ici, JAMAIS boucle_etat() : ce dernier
 * rend un pointeur BRUT vers le tampon interne de la boucle, valide
 * seulement jusqu'au prochain cycle de la tâche d'interrogation (~1 s, voir
 * le commentaire de boucle_etat() dans boucle.h). Cette tâche httpd tourne
 * dans son propre contexte, sans aucune garantie d'être relue avant que la
 * tâche d'interrogation ne remette à zéro ce même tampon pour le cycle
 * suivant — un simple délai de scheduler suffirait à transformer un
 * pointeur brut en lecture de mémoire déjà écrasée. boucle_etat_copier()
 * copie sous mutex dans une structure locale à cette fonction, qui reste
 * valable et exacte quel que soit le temps que la construction du JSON
 * ci-dessous prend ensuite. */
static esp_err_t gestion_state(httpd_req_t *req)
{
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    /* Rend false si la boucle n'a jamais démarré (hôte non configuré, ou
     * boucle_demarrer() en échec) : `etat` reste alors la structure remise à
     * zéro ci-dessus plutôt qu'indéterminée, et liaison/generation ci-dessous
     * décrivent fidèlement cet état initial (LIAISON_CONNEXION, génération
     * 0) — pas d'erreur HTTP pour ce qui est un état normal de l'appareil. */
    boucle_etat_copier(&etat, sizeof(etat));

    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(racine, "liaison", liaison_nom(boucle_liaison()));
    cJSON_AddNumberToObject(racine, "generation", (double)boucle_generation());

    cJSON *etat_json = cJSON_AddObjectToObject(racine, "etat");
    if (etat_json != NULL) {
        cJSON_AddStringToObject(etat_json, "etat", etat.etat);

        cJSON *buse = cJSON_AddObjectToObject(etat_json, "buse");
        if (buse != NULL) {
            cJSON_AddNumberToObject(buse, "actuelle", (double)etat.buse_actuelle);
            cJSON_AddNumberToObject(buse, "consigne", (double)etat.buse_consigne);
        }

        cJSON *plateau = cJSON_AddObjectToObject(etat_json, "plateau");
        if (plateau != NULL) {
            cJSON_AddNumberToObject(plateau, "actuel", (double)etat.plateau_actuel);
            cJSON_AddNumberToObject(plateau, "consigne", (double)etat.plateau_consigne);
        }

        cJSON_AddStringToObject(etat_json, "fichier", etat.fichier);
        cJSON_AddNumberToObject(etat_json, "progression", (double)etat.progression);
        cJSON_AddNumberToObject(etat_json, "temps_restant_s", (double)etat.temps_restant_s);
        cJSON_AddBoolToObject(etat_json, "impression_en_cours", etat.impression_en_cours);
        cJSON_AddBoolToObject(etat_json, "impression_en_pause", etat.impression_en_pause);
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
    static const httpd_uri_t route_revert = {
        .uri = "/revert", .method = HTTP_POST, .handler = gestion_revert, .user_ctx = NULL,
    };

    enregistrer_route(serveur, &route_racine);
    enregistrer_route(serveur, &route_status);
    enregistrer_route(serveur, &route_state);
    enregistrer_route(serveur, &route_log);
    enregistrer_route(serveur, &route_revert);

    ESP_LOGI(TAG, "serveur HTTP demarre");
    return ESP_OK;
}
