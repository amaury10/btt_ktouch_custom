/* Connexion WiFi station.
 *
 * Piège corrigé après le premier vol matériel : esp_wifi_start() est
 * asynchrone, il se contente de poster WIFI_EVENT_STA_START, et l'interface
 * station n'est prête qu'à la réception de cet événement. Appeler
 * esp_wifi_connect() directement après esp_wifi_start() (comme le faisait ce
 * fichier avant ce correctif) échoue donc typiquement avec
 * ESP_ERR_WIFI_NOT_STARTED — et comme aucune tentative de connexion n'a
 * jamais réellement eu lieu, aucun WIFI_EVENT_STA_DISCONNECTED ne se produit
 * non plus pour relancer quoi que ce soit via le gestionnaire ci-dessous : le
 * WiFi restait mort jusqu'à l'échéance du sauvetage, sans qu'aucune tentative
 * n'ait jamais été faite. C'est exactement ce que l'exemple officiel
 * "station" d'ESP-IDF évite en appelant esp_wifi_connect() depuis le
 * gestionnaire de WIFI_EVENT_STA_START — repris ici.
 *
 * Ce bug n'a été trouvé qu'après un vol matériel et un post-mortem, car sans
 * WiFi le journal réseau est injoignable : le seul canal qui aurait montré
 * l'erreur est justement celui que l'erreur elle-même désactive. D'où
 * l'importance de app_main.c qui affiche désormais l'état WiFi (et la
 * dernière erreur de connexion, via wifi_last_connect_error()) directement à
 * l'écran — seul canal de diagnostic qui survive à une panne WiFi sans
 * câble série.
 *
 * Piège évité par ailleurs, et qui aurait pu rendre l'appareil définitivement
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

/* Dernier message d'erreur d'un esp_wifi_connect() infructueux ; vidé dès
 * qu'une connexion réussit. Affiché à l'écran par app_main.c : c'est le seul
 * canal de diagnostic qui survive à une panne WiFi sans câble série. */
static char derniere_erreur[32];

static void tenter_connexion(void)
{
    esp_err_t erreur = esp_wifi_connect();
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect a echoue : %s", esp_err_to_name(erreur));
        strlcpy(derniere_erreur, esp_err_to_name(erreur), sizeof(derniere_erreur));
    }
}

static void sur_evenement(void *arg, esp_event_base_t base, int32_t id, void *donnees)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* Premier appel possible à esp_wifi_connect() : l'interface station
         * n'existe, du point de vue d'esp_wifi, qu'à partir de cet
         * événement. */
        tenter_connexion();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connectee = false;
        ESP_LOGW(TAG, "connexion perdue, nouvelle tentative");
        tenter_connexion();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evenement = (const ip_event_got_ip_t *)donnees;
        snprintf(adresse_ip, sizeof(adresse_ip), IPSTR, IP2STR(&evenement->ip_info.ip));
        connectee = true;
        derniere_erreur[0] = '\0';
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

    /* Le mode doit être fixé avant tout esp_wifi_get_config()/
     * esp_wifi_set_config() : esp_wifi.h documente que ces deux appels ne
     * fonctionnent que si l'interface est activée, sous peine de
     * ESP_ERR_WIFI_IF. Rester après esp_wifi_set_storage() ne change rien à
     * la sécurité NVS, puisque le stockage est déjà en RAM. */
    erreur = esp_wifi_set_mode(WIFI_MODE_STA);
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

    /* esp_wifi_start() est asynchrone : il ne fait que poster
     * WIFI_EVENT_STA_START. C'est ce gestionnaire, plus haut, qui appelle
     * esp_wifi_connect() — pas cette fonction. Rendre directement le
     * résultat d'esp_wifi_start() : un esp_wifi_connect() synchrone ici,
     * comme avant ce correctif, échouerait presque toujours avec
     * ESP_ERR_WIFI_NOT_STARTED. */
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

bool wifi_last_connect_error(char *out, size_t len)
{
    if (derniere_erreur[0] == '\0') {
        return false;
    }
    if (out != NULL && len > 0) {
        strlcpy(out, derniere_erreur, len);
    }
    return true;
}
