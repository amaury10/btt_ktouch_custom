/* Serveur HTTP : c'est la seule interface de contrôle du firmware une fois le
 * câble série hors jeu. Cinq routes, résumées dans le brief de la tâche :
 *   GET  /        page d'état minimale, avec liens vers les autres routes
 *   GET  /status  JSON : slot en cours, version, uptime, mémoire libre, tactile
 *   GET  /log     texte brut, contenu du journal réseau (netlog_snapshot())
 *   POST /revert  bascule vers l'autre slot et redémarre
 *   POST /update  reçoit une image applicative brute, l'écrit dans le slot
 *                 inactif et redémarre dessus
 *
 * /revert et /update sont en POST délibérément : en GET, n'importe quelle
 * requête d'un navigateur, d'un aspirateur de liens ou d'un scanner réseau
 * redémarrerait l'appareil. */

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

static const char *TAG = "web";

/* Renseigné par app_main après la vérification du périphérique tactile.
 * Volontairement découplé de LVGL : ce module ne connaît que ce booléen. */
static bool tactile_disponible;

void web_set_touch_available(bool disponible)
{
    tactile_disponible = disponible;
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
        "<li>POST /update — envoie une image applicative dans le slot inactif</li>"
        "</ul></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t gestion_status(httpd_req_t *req)
{
    const esp_partition_t *courante = esp_ota_get_running_partition();
    const esp_app_desc_t *description = esp_app_get_description();
    char reponse[384];

    int longueur = snprintf(reponse, sizeof(reponse),
        "{"
        "\"slot\":\"%s\","
        "\"version\":\"%s\","
        "\"uptime_ms\":%" PRId64 ","
        "\"free_heap\":%" PRIu32 ","
        "\"tactile\":%s"
        "}",
        courante != NULL ? courante->label : "?",
        description != NULL ? description->version : "?",
        (int64_t)(esp_timer_get_time() / 1000),
        (uint32_t)esp_get_free_heap_size(),
        tactile_disponible ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    if (longueur < 0) {
        return httpd_resp_send_500(req);
    }
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

static esp_err_t gestion_update(httpd_req_t *req)
{
    const esp_partition_t *cible = esp_ota_get_next_update_partition(NULL);
    if (cible == NULL) {
        ESP_LOGE(TAG, "aucun slot OTA disponible pour /update");
        return httpd_resp_send_500(req);
    }

    esp_ota_handle_t gestionnaire;
    esp_err_t erreur = esp_ota_begin(cible, OTA_SIZE_UNKNOWN, &gestionnaire);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin a echoue : %s", esp_err_to_name(erreur));
        return httpd_resp_send_500(req);
    }

    static char tampon[4096];
    size_t restant = req->content_len;
    size_t total_ecrit = 0;

    while (restant > 0) {
        size_t a_lire = restant < sizeof(tampon) ? restant : sizeof(tampon);
        int recu = httpd_req_recv(req, tampon, a_lire);
        if (recu <= 0) {
            /* Timeout ou erreur socket : sans esp_ota_abort() ici, le handle
             * reste ouvert et toute tentative suivante échoue — sans câble
             * série pour s'en remettre, ce serait irrécupérable. */
            ESP_LOGE(TAG, "reception interrompue (%d) apres %u octets", recu, (unsigned)total_ecrit);
            esp_ota_abort(gestionnaire);
            return httpd_resp_send_500(req);
        }

        erreur = esp_ota_write(gestionnaire, tampon, (size_t)recu);
        if (erreur != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write a echoue : %s", esp_err_to_name(erreur));
            esp_ota_abort(gestionnaire);
            return httpd_resp_send_500(req);
        }

        total_ecrit += (size_t)recu;
        restant -= (size_t)recu;
    }

    erreur = esp_ota_end(gestionnaire);
    if (erreur != ESP_OK) {
        /* esp_ota_end() libère le handle quel que soit son résultat : un
         * esp_ota_abort() ici viserait un handle déjà invalide. */
        ESP_LOGE(TAG, "esp_ota_end a echoue : %s", esp_err_to_name(erreur));
        return httpd_resp_send_500(req);
    }

    erreur = esp_ota_set_boot_partition(cible);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition a echoue : %s", esp_err_to_name(erreur));
        return httpd_resp_send_500(req);
    }

    ESP_LOGW(TAG, "image recue (%u octets), bascule vers %s, redemarrage", (unsigned)total_ecrit, cible->label);
    httpd_resp_sendstr(req, "image ecrite, redemarrage\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; /* jamais atteint */
}

esp_err_t web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* Les écritures OTA via esp_ota_write() consomment davantage de pile que
     * les 4 Kio par défaut. */
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
    static const httpd_uri_t route_log = {
        .uri = "/log", .method = HTTP_GET, .handler = gestion_log, .user_ctx = NULL,
    };
    static const httpd_uri_t route_revert = {
        .uri = "/revert", .method = HTTP_POST, .handler = gestion_revert, .user_ctx = NULL,
    };
    static const httpd_uri_t route_update = {
        .uri = "/update", .method = HTTP_POST, .handler = gestion_update, .user_ctx = NULL,
    };

    httpd_register_uri_handler(serveur, &route_racine);
    httpd_register_uri_handler(serveur, &route_status);
    httpd_register_uri_handler(serveur, &route_log);
    httpd_register_uri_handler(serveur, &route_revert);
    httpd_register_uri_handler(serveur, &route_update);

    ESP_LOGI(TAG, "serveur HTTP demarre");
    return ESP_OK;
}
