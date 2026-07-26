/* Serveur HTTP : c'est la seule interface de contrôle du firmware une fois le
 * câble série hors jeu. Quatre routes :
 *   GET  /        page d'état minimale, avec liens vers les autres routes
 *   GET  /status  JSON : slot en cours, version, IP, uptime, mémoire libre,
 *                 tactile disponible, compteur de démarrages
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
    esp_err_t erreur = rescue_switch_to_other_slot();
    if (erreur != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_sendstr(req, "bascule effectuee, redemarrage\n");
    /* Laisser la réponse partir avant de couper le réseau. */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; /* jamais atteint */
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
    static const httpd_uri_t route_log = {
        .uri = "/log", .method = HTTP_GET, .handler = gestion_log, .user_ctx = NULL,
    };
    static const httpd_uri_t route_revert = {
        .uri = "/revert", .method = HTTP_POST, .handler = gestion_revert, .user_ctx = NULL,
    };

    enregistrer_route(serveur, &route_racine);
    enregistrer_route(serveur, &route_status);
    enregistrer_route(serveur, &route_log);
    enregistrer_route(serveur, &route_revert);

    ESP_LOGI(TAG, "serveur HTTP demarre");
    return ESP_OK;
}
