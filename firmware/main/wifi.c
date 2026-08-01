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
 * Deuxième piège, corrigé après le deuxième vol matériel : esp_wifi_connect()
 * rend ESP_OK dès qu'une tentative DÉMARRE, pas quand elle réussit. Un échec
 * d'association (mauvais SSID, mot de passe refusé, AP introuvable) ne
 * remonte que via WIFI_EVENT_STA_DISCONNECTED, dans son champ `reason` — un
 * gestionnaire qui se contente de relancer esp_wifi_connect() sans lire ce
 * champ jette la seule information qui distingue « mauvais SSID » de
 * « mauvais mot de passe » de « point d'accès injoignable ». D'où
 * nom_raison() et wifi_last_disconnect_reason() plus bas, affichés à l'écran
 * par app_main.c : sans WiFi, /log est injoignable, donc sans câble série,
 * l'écran est le seul canal de diagnostic qui survive à une panne WiFi.
 *
 * Troisième correction, après ce même deuxième vol : la priorité entre la
 * configuration héritée de la NVS partagée et le secours Kconfig est
 * inversée par rapport aux versions précédentes de ce fichier. La
 * configuration héritée (esp_wifi_get_config()) peut porter un
 * `sta.bssid_set = true` figé sur le point d'accès auquel le firmware
 * d'origine s'est associé la dernière fois : le réutiliser tel quel épingle
 * l'association à ce seul BSSID, et un BSSID caduc ou un point d'accès qui a
 * changé produit WIFI_REASON_NO_AP_FOUND même si le SSID est parfaitement
 * joignable. Elle porte aussi `scan_method`, `sort_method`, `threshold`,
 * `channel` et `pmf_cfg` réglés par le firmware d'origine, repris aveuglément.
 * Puisque des identifiants Kconfig sont désormais renseignés et vérifiés
 * présents, ils sont préférés : la configuration qu'on construit soi-même,
 * neuve, ne porte aucun de ces pièges. La NVS héritée ne sert plus que de
 * secours, et seulement après avoir été nettoyée de tout epinglage de BSSID.
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
 * Deux règles, non négociables, et qui ne changent pas avec l'inversion de
 * priorité ci-dessus :
 *   1. esp_wifi_set_storage(WIFI_STORAGE_RAM) est appelé immédiatement après
 *      esp_wifi_init(), avant toute autre opération WiFi. À partir de là,
 *      plus aucune écriture de configuration ne peut atteindre la NVS, quoi
 *      que fasse le reste de cette fonction.
 *   2. esp_wifi_set_config() n'est appelé qu'avec une configuration qui, si
 *      elle vient de la NVS de l'appareil, a déjà été nettoyée de son
 *      épinglage de BSSID/canal — jamais réappliquée telle quelle.
 *
 * Une coupure passagère (WIFI_EVENT_STA_DISCONNECTED) relance simplement
 * esp_wifi_connect() : ce n'est qu'une absence prolongée de réseau, jugée par
 * le minuteur de rescue.c, qui doit déclencher le sauvetage.
 *
 * Le gestionnaire de IP_EVENT_STA_GOT_IP ci-dessous désarme ce minuteur dès
 * qu'une IP est obtenue -- c'était autrefois le SEUL endroit qui le faisait.
 * Depuis le fix du multi-reboot au démarrage (jalon 3b, sous-projet 7), ce
 * n'est plus le cas : app_main.c désarme aussi le sauvetage, une fois tous
 * ses étages d'initialisation risqués passés (voir le commentaire à la fin
 * d'app_main() pour le détail et le compromis assumé). Les deux appellent
 * rescue_disarm()+rescue_reset_boot_count(), idempotents ; celui d'ici reste
 * le premier des deux à survenir si le réseau répond avant la fin de
 * l'initialisation, ce qui est le cas courant, mais n'est plus le seul. */

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

/* Dernier message d'erreur d'un esp_wifi_connect() infructueux (échec
 * synchrone de l'appel lui-même, rare depuis que STA_START le déclenche au
 * bon moment) ; vidé dès qu'une connexion réussit. */
static char derniere_erreur[32];

/* Dernière raison de déconnexion (wifi_event_sta_disconnected_t::reason) et
 * indicateur de validité : c'est le diagnostic qui compte réellement en cas
 * d'échec d'association. Vidé dès qu'une connexion réussit. */
static uint8_t derniere_raison;
static bool a_une_raison;

static uint32_t tentatives_connexion;

typedef enum { SOURCE_AUCUNE, SOURCE_NVS, SOURCE_CONFIG } source_identifiants_t;
static source_identifiants_t source_courante = SOURCE_AUCUNE;
static char ssid_utilise[33] = ""; /* wifi_sta_config_t.ssid fait 32 octets + NUL */

/* Pas d'aide officielle "raison vers texte" dans ESP-IDF : couverture des cas
 * qui distinguent réellement un diagnostic d'un autre. NULL pour le reste,
 * l'appelant se rabat alors sur le seul code numérique. */
static const char *nom_raison(uint8_t code)
{
    switch (code) {
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
        default: return NULL;
    }
}

static void tenter_connexion(void)
{
    tentatives_connexion++;
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
        const wifi_event_sta_disconnected_t *evenement = (const wifi_event_sta_disconnected_t *)donnees;
        connectee = false;
        derniere_raison = evenement->reason;
        a_une_raison = true;
        const char *nom = nom_raison(derniere_raison);
        if (nom != NULL) {
            ESP_LOGW(TAG, "connexion perdue : %s (%u), nouvelle tentative", nom, (unsigned)derniere_raison);
        } else {
            ESP_LOGW(TAG, "connexion perdue : raison %u, nouvelle tentative", (unsigned)derniere_raison);
        }
        tenter_connexion();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evenement = (const ip_event_got_ip_t *)donnees;
        snprintf(adresse_ip, sizeof(adresse_ip), IPSTR, IP2STR(&evenement->ip_info.ip));
        connectee = true;
        derniere_erreur[0] = '\0';
        a_une_raison = false;
        ESP_LOGI(TAG, "adresse IP : %s", adresse_ip);
        /* Une connexion réussie prouve que ce firmware est viable : désarme
         * le sauvetage et remet le compteur de démarrages à zéro. N'est plus
         * le seul endroit à le faire depuis le fix du multi-reboot au
         * démarrage (voir le commentaire en tête de ce fichier et celui à la
         * fin d'app_main()) -- appeler ces deux fonctions ici alors qu'elles
         * ont déjà tourné depuis app_main.c est un doublon sans effet
         * (idempotentes), pas une erreur. */
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
     * partagée avec le firmware d'origine — quelle que soit la source de
     * configuration retenue plus bas. */
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

    /* Priorité aux identifiants Kconfig, désormais renseignés et vérifiés
     * présents dans la configuration compilée. Une configuration neuve,
     * mise à zéro, ne porte aucun des réglages du firmware d'origine (BSSID
     * épinglé, canal, méthode de balayage...) qui pourraient épingler
     * l'association à un point d'accès caduc. Seulement si aucun SSID
     * Kconfig n'est renseigné, on se rabat sur la configuration héritée de
     * la NVS partagée — nettoyée de tout épinglage avant d'être appliquée. */
    wifi_config_t config_cible = {0};
    if (CONFIG_KTOUCH_WIFI_SSID[0] != '\0') {
        strlcpy((char *)config_cible.sta.ssid, CONFIG_KTOUCH_WIFI_SSID, sizeof(config_cible.sta.ssid));
        strlcpy((char *)config_cible.sta.password, CONFIG_KTOUCH_WIFI_PASSWORD, sizeof(config_cible.sta.password));
        /* Un seuil à zéro équivaut à WIFI_AUTH_OPEN, et certains points
         * d'accès en mode mixte WPA2/WPA3 (le cas des routeurs Freebox)
         * rejettent une station qui n'annonce pas la capacité PMF (Protected
         * Management Frames). */
        config_cible.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        config_cible.sta.pmf_cfg.capable = true;

        source_courante = SOURCE_CONFIG;
        strlcpy(ssid_utilise, CONFIG_KTOUCH_WIFI_SSID, sizeof(ssid_utilise));
        ESP_LOGI(TAG, "identifiants Kconfig retenus, SSID '%s'", CONFIG_KTOUCH_WIFI_SSID);
    } else {
        /* esp_wifi_init() a déjà chargé, en mémoire, la configuration station
         * précédemment enregistrée dans la NVS par le firmware d'origine (ou
         * par un démarrage antérieur de celui-ci). */
        esp_err_t lecture = esp_wifi_get_config(WIFI_IF_STA, &config_cible);
        bool ssid_herite_present = (lecture == ESP_OK) && (config_cible.sta.ssid[0] != '\0');

        if (ssid_herite_present) {
            /* Ne jamais réutiliser tel quel un BSSID/canal épinglés par le
             * firmware d'origine : un point d'accès qui a changé ou un BSSID
             * caduc produirait WIFI_REASON_NO_AP_FOUND alors même que le SSID
             * reste parfaitement joignable. */
            config_cible.sta.bssid_set = false;
            memset(config_cible.sta.bssid, 0, sizeof(config_cible.sta.bssid));
            config_cible.sta.channel = 0;

            source_courante = SOURCE_NVS;
            strlcpy(ssid_utilise, (const char *)config_cible.sta.ssid, sizeof(ssid_utilise));
            ESP_LOGI(TAG, "identifiants herites de la NVS partagee, SSID '%s'", (const char *)config_cible.sta.ssid);
        } else {
            source_courante = SOURCE_AUCUNE;
            ssid_utilise[0] = '\0';
            ESP_LOGW(TAG, "aucun SSID disponible (ni Kconfig, ni NVS de l'appareil)");
            ESP_LOGW(TAG, "le sauvetage automatique se declenchera faute de connexion");
        }
    }

    if (source_courante != SOURCE_AUCUNE) {
        /* Le mot de passe n'est jamais journalisé — /log est exposé en HTTP,
         * lisible par quiconque est sur le réseau. */
        erreur = esp_wifi_set_config(WIFI_IF_STA, &config_cible);
        if (erreur != ESP_OK) {
            return erreur;
        }
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

bool wifi_last_disconnect_reason(char *out, size_t len)
{
    if (!a_une_raison) {
        return false;
    }
    if (out != NULL && len > 0) {
        const char *nom = nom_raison(derniere_raison);
        if (nom != NULL) {
            snprintf(out, len, "%s (%u)", nom, (unsigned)derniere_raison);
        } else {
            snprintf(out, len, "%u", (unsigned)derniere_raison);
        }
    }
    return true;
}

uint32_t wifi_connect_attempts(void)
{
    return tentatives_connexion;
}

const char *wifi_credential_source(char *ssid_out, size_t len)
{
    if (ssid_out != NULL && len > 0) {
        strlcpy(ssid_out, ssid_utilise, len);
    }
    switch (source_courante) {
        case SOURCE_CONFIG: return "cfg";
        case SOURCE_NVS: return "nvs";
        default: return "aucun";
    }
}
