/* Connexion WiFi station classique. Une coupure passagère (WIFI_EVENT_STA_
 * DISCONNECTED) relance simplement esp_wifi_connect() : ce n'est qu'une
 * absence prolongée de réseau, jugée par le minuteur de rescue.c, qui doit
 * déclencher le sauvetage. Le seul endroit qui désarme ce minuteur est le
 * gestionnaire de IP_EVENT_STA_GOT_IP, ci-dessous. */

#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "rescue.h"

static const char *TAG = "wifi";

static volatile bool connectee;
static char adresse_ip[16] = "0.0.0.0";

static void sur_evenement(void *arg, esp_event_base_t base, int32_t id, void *donnees)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connectee = false;
        ESP_LOGW(TAG, "connexion perdue, nouvelle tentative");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evenement = (const ip_event_got_ip_t *)donnees;
        snprintf(adresse_ip, sizeof(adresse_ip), IPSTR, IP2STR(&evenement->ip_info.ip));
        connectee = true;
        ESP_LOGI(TAG, "adresse IP : %s", adresse_ip);
        /* Seul endroit du firmware qui désarme le sauvetage. */
        rescue_disarm();
    }
}

esp_err_t wifi_start(void)
{
    esp_err_t erreur = esp_netif_init();
    if (erreur != ESP_OK) {
        return erreur;
    }

    erreur = esp_event_loop_create_default();
    if (erreur != ESP_OK) {
        return erreur;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t config_init = WIFI_INIT_CONFIG_DEFAULT();
    erreur = esp_wifi_init(&config_init);
    if (erreur != ESP_OK) {
        return erreur;
    }

    erreur = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sur_evenement, NULL, NULL);
    if (erreur != ESP_OK) {
        return erreur;
    }
    erreur = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sur_evenement, NULL, NULL);
    if (erreur != ESP_OK) {
        return erreur;
    }

    wifi_config_t config_sta = {0};
    strlcpy((char *)config_sta.sta.ssid, CONFIG_KTOUCH_WIFI_SSID, sizeof(config_sta.sta.ssid));
    strlcpy((char *)config_sta.sta.password, CONFIG_KTOUCH_WIFI_PASSWORD, sizeof(config_sta.sta.password));

    erreur = esp_wifi_set_mode(WIFI_MODE_STA);
    if (erreur != ESP_OK) {
        return erreur;
    }
    erreur = esp_wifi_set_config(WIFI_IF_STA, &config_sta);
    if (erreur != ESP_OK) {
        return erreur;
    }

    ESP_LOGI(TAG, "connexion au reseau '%s'", CONFIG_KTOUCH_WIFI_SSID);
    return esp_wifi_start();
}

bool wifi_is_connected(void)
{
    return connectee;
}

bool wifi_ip_string(char *out, size_t len)
{
    if (out == NULL || len == 0) {
        return connectee;
    }
    strlcpy(out, adresse_ip, len);
    return connectee;
}
