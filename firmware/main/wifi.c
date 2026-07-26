/* Connexion WiFi station.
 *
 * Piège évité ici, et qui aurait pu rendre l'appareil définitivement
 * injoignable : la partition NVS (0x9000) est partagée par les deux slots
 * applicatifs. Le firmware d'origine y range ses identifiants WiFi dans
 * l'espace de noms standard d'ESP-IDF. Le stockage par défaut d'esp_wifi
 * étant WIFI_STORAGE_FLASH, un esp_wifi_set_config() ordinaire — même avec un
 * SSID vide — écrirait dans cette même NVS et effacerait les identifiants du
 * firmware d'origine. Si notre firmware ne se connecte alors pas, le
 * sauvetage rebasculerait vers un firmware d'origine lui aussi privé de
 * WiFi : plus aucun accès possible, sur aucun des deux slots.
 *
 * Deux règles, non négociables :
 *   1. esp_wifi_set_storage(WIFI_STORAGE_RAM) est appelé immédiatement après
 *      esp_wifi_init(), avant toute autre opération WiFi. À partir de là,
 *      plus aucune écriture de configuration ne peut atteindre la NVS, quoi
 *      que fasse le reste de cette fonction.
 *   2. esp_wifi_set_config() n'est appelé QUE si la configuration héritée de
 *      la NVS (lue avec esp_wifi_get_config(), donc écrite là par le
 *      firmware d'origine avant notre premier démarrage) n'a pas de SSID.
 *      La NVS de l'appareil fait toujours autorité sur les options Kconfig,
 *      qui ne servent que de secours.
 *
 * Une coupure passagère (WIFI_EVENT_STA_DISCONNECTED) relance simplement
 * esp_wifi_connect() : ce n'est qu'une absence prolongée de réseau, jugée par
 * le minuteur de rescue.c, qui doit déclencher le sauvetage. Le seul endroit
 * qui désarme ce minuteur est le gestionnaire de IP_EVENT_STA_GOT_IP,
 * ci-dessous. */

#include "wifi.h"

#include <stdio.h>
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
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connectee = false;
        ESP_LOGW(TAG, "connexion perdue, nouvelle tentative");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evenement = (const ip_event_got_ip_t *)donnees;
        snprintf(adresse_ip, sizeof(adresse_ip), IPSTR, IP2STR(&evenement->ip_info.ip));
        connectee = true;
        ESP_LOGI(TAG, "adresse IP : %s", adresse_ip);
        /* Seul endroit du firmware qui désarme le sauvetage et qui remet le
         * compteur de démarrages à zéro : une connexion réussie prouve que ce
         * firmware est viable. */
        rescue_disarm();
        rescue_reset_boot_count();
    }
}

esp_err_t wifi_start(void)
{
    /* ESP_ERR_INVALID_STATE signifie simplement « déjà initialisé » pour ces
     * deux appels — ce n'est pas un échec ici, tant que quelqu'un d'autre l'a
     * fait avant nous (rien d'autre dans ce firmware ne le fait, mais rester
     * tolérant coûte peu et évite d'abandonner le WiFi pour un faux positif). */
    esp_err_t erreur = esp_netif_init();
    if (erreur != ESP_OK && erreur != ESP_ERR_INVALID_STATE) {
        return erreur;
    }

    erreur = esp_event_loop_create_default();
    if (erreur != ESP_OK && erreur != ESP_ERR_INVALID_STATE) {
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

    /* Garantie structurelle, pas seulement une convention : à partir d'ici,
     * plus rien dans cette fonction (ni ailleurs) ne peut écrire dans la NVS
     * partagée avec le firmware d'origine. */
    erreur = esp_wifi_set_storage(WIFI_STORAGE_RAM);
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

    /* esp_wifi_init() a déjà chargé, en mémoire, la configuration station
     * précédemment enregistrée dans la NVS par le firmware d'origine (ou par
     * un démarrage antérieur de celui-ci). On la relit explicitement pour
     * décider si un secours Kconfig est nécessaire. */
    wifi_config_t config_heritee = {0};
    esp_err_t lecture = esp_wifi_get_config(WIFI_IF_STA, &config_heritee);
    bool ssid_herite_present = (lecture == ESP_OK) && (config_heritee.sta.ssid[0] != '\0');

    if (ssid_herite_present) {
        /* La NVS de l'appareil fait toujours autorité : on ne touche à rien,
         * esp_wifi_set_config() n'est même pas appelé. */
        ESP_LOGI(TAG, "identifiants herites de la NVS partagee, SSID '%s'", (const char *)config_heritee.sta.ssid);
    } else if (CONFIG_KTOUCH_WIFI_SSID[0] != '\0') {
        /* Secours Kconfig, seulement parce que le stockage est déjà passé en
         * WIFI_STORAGE_RAM ci-dessus : cet appel ne peut pas écrire dans la
         * NVS. Le mot de passe n'est jamais journalisé — /log est exposé en
         * HTTP, lisible par quiconque est sur le réseau. */
        wifi_config_t config_secours = {0};
        strlcpy((char *)config_secours.sta.ssid, CONFIG_KTOUCH_WIFI_SSID, sizeof(config_secours.sta.ssid));
        strlcpy((char *)config_secours.sta.password, CONFIG_KTOUCH_WIFI_PASSWORD, sizeof(config_secours.sta.password));

        erreur = esp_wifi_set_config(WIFI_IF_STA, &config_secours);
        if (erreur != ESP_OK) {
            return erreur;
        }
        ESP_LOGI(TAG, "identifiants de secours (Kconfig) appliques, SSID '%s'", CONFIG_KTOUCH_WIFI_SSID);
    } else {
        ESP_LOGW(TAG, "aucun SSID disponible (ni NVS de l'appareil, ni secours Kconfig)");
        ESP_LOGW(TAG, "le sauvetage automatique se declenchera faute de connexion");
    }

    erreur = esp_wifi_set_mode(WIFI_MODE_STA);
    if (erreur != ESP_OK) {
        return erreur;
    }

    erreur = esp_wifi_start();
    if (erreur != ESP_OK) {
        return erreur;
    }

    return esp_wifi_connect();
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
