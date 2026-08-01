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
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#include "reglages.h"
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

typedef enum { SOURCE_AUCUNE, SOURCE_NVS, SOURCE_CONFIG, SOURCE_REGLAGES } source_identifiants_t;
static source_identifiants_t source_courante = SOURCE_AUCUNE;
static char ssid_utilise[33] = ""; /* wifi_sta_config_t.ssid fait 32 octets + NUL */

/* --- Reconfiguration WiFi à chaud (« essayer PUIS persister ») ---
 *
 * État PARTAGÉ entre DEUX tâches qui s'exécutent réellement en parallèle sur cet
 * ESP32-S3 (SMP, deux cœurs) :
 *   - la tâche appelante de wifi_reconfigurer() (l'écran de réglages, via une
 *     tâche dédiée hors du fil LVGL) ARME la reconfiguration ;
 *   - la tâche `sys_evt` de l'event loop par défaut (sur_evenement() ci-dessous)
 *     la fait ABOUTIR (GOT_IP → persiste) ou ÉCHOUER (N déconnexions → restaure).
 * Le champ `en_attente` et le couple candidat/précédent/compteur sont donc lus et
 * écrits par les deux : toute opération composée (test-puis-agir, incrément)
 * passe sous `verrou_reconfig`, exactement pour la même raison que rescue_disarm()
 * (voir rescue.c). Les appels esp_wifi_* et NVS, eux, ne doivent JAMAIS tourner
 * sous portENTER_CRITICAL : on copie l'état utile dans des locales sous verrou,
 * puis on agit hors verrou. */
static portMUX_TYPE verrou_reconfig = portMUX_INITIALIZER_UNLOCKED;

/* Nombre de déconnexions consécutives (sans IP) tolérées sur les identifiants
 * candidats avant de restaurer les précédents. La déconnexion volontaire qui
 * lance la bascule (esp_wifi_disconnect() dans wifi_reconfigurer()) survient dans
 * l'ordre set_config → disconnect → armement : normalement elle est traitée par
 * sys_evt AVANT l'armement (donc non comptée), mais si sys_evt prend du retard
 * elle peut être traitée APRÈS l'armement et compter alors comme tentative n°1.
 * Ce n'est pas un problème : un comptage anticipé ne fait que RESTAURER plus tôt
 * vers des identifiants connus (le biais est du côté sûr). Le compteur mesure
 * donc « au plus » les échecs d'association avec les nouveaux identifiants. */
#define RECONFIG_MAX_TENTATIVES 3

static volatile bool reconfig_en_attente;
static volatile wifi_reconfig_etat_t reconfig_etat = WIFI_RECONFIG_INACTIF;
static uint32_t reconfig_tentatives;
static char reconfig_candidat_ssid[33];
static char reconfig_candidat_pass[64];
static char reconfig_precedent_ssid[33];
static char reconfig_precedent_pass[64];

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

        /* Invariant anti-verrouillage : si une reconfiguration est en attente,
         * chaque déconnexion sans IP est un échec d'association avec les
         * identifiants candidats. Au-delà de RECONFIG_MAX_TENTATIVES, on RESTAURE
         * les identifiants précédents (copie RAM) et on ne persiste rien. On lit
         * l'état partagé et on construit la config de restauration SOUS verrou,
         * puis on applique esp_wifi_set_config HORS verrou. */
        bool restaurer = false;
        wifi_config_t config_restauration = {0};
        portENTER_CRITICAL(&verrou_reconfig);
        if (reconfig_en_attente) {
            reconfig_tentatives++;
            if (reconfig_tentatives >= RECONFIG_MAX_TENTATIVES) {
                restaurer = true;
                strlcpy((char *)config_restauration.sta.ssid, reconfig_precedent_ssid,
                        sizeof(config_restauration.sta.ssid));
                strlcpy((char *)config_restauration.sta.password, reconfig_precedent_pass,
                        sizeof(config_restauration.sta.password));
                if (reconfig_precedent_pass[0] == '\0') {
                    config_restauration.sta.threshold.authmode = WIFI_AUTH_OPEN;
                } else {
                    config_restauration.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                    config_restauration.sta.pmf_cfg.capable = true;
                }
                strlcpy(ssid_utilise, reconfig_precedent_ssid, sizeof(ssid_utilise));
                reconfig_en_attente = false;
                reconfig_etat = WIFI_RECONFIG_ECHOUE;
            }
        }
        portEXIT_CRITICAL(&verrou_reconfig);

        if (restaurer) {
            ESP_LOGW(TAG, "reconfiguration echouee apres %d tentatives, restauration des identifiants precedents",
                     RECONFIG_MAX_TENTATIVES);
            esp_err_t erreur = esp_wifi_set_config(WIFI_IF_STA, &config_restauration);
            if (erreur != ESP_OK) {
                ESP_LOGE(TAG, "restauration esp_wifi_set_config a echoue : %s", esp_err_to_name(erreur));
            }
        }

        tenter_connexion();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evenement = (const ip_event_got_ip_t *)donnees;
        snprintf(adresse_ip, sizeof(adresse_ip), IPSTR, IP2STR(&evenement->ip_info.ip));
        connectee = true;
        derniere_erreur[0] = '\0';
        a_une_raison = false;
        ESP_LOGI(TAG, "adresse IP : %s", adresse_ip);

        /* Invariant anti-verrouillage — le SEUL endroit où l'on PERSISTE : si une
         * reconfiguration est en attente, obtenir une IP prouve que les
         * identifiants candidats connectent réellement. On les fige alors dans les
         * réglages (NVS « ktouch », jamais la NVS WiFi d'esp_wifi). On copie les
         * candidats dans des locales SOUS verrou, puis on appelle
         * reglages_definir_wifi() HORS verrou (écriture NVS, bloquante — interdite
         * sous portENTER_CRITICAL). Une IP obtenue hors reconfiguration (connexion
         * normale) ne persiste rien : reconfig_en_attente reste faux. */
        bool persister = false;
        char candidat_ssid[33];
        char candidat_pass[64];
        portENTER_CRITICAL(&verrou_reconfig);
        if (reconfig_en_attente) {
            persister = true;
            strlcpy(candidat_ssid, reconfig_candidat_ssid, sizeof(candidat_ssid));
            strlcpy(candidat_pass, reconfig_candidat_pass, sizeof(candidat_pass));
            strlcpy(ssid_utilise, reconfig_candidat_ssid, sizeof(ssid_utilise));
            reconfig_en_attente = false;
            /* On NE fixe PAS encore reconfig_etat : la connexion est établie mais
             * la persistance n'a pas eu lieu. L'état final (REUSSI si la NVS a bien
             * été écrite, CONNECTE_NON_PERSISTE si l'écriture a échoué) est posé
             * hors verrou, une fois reglages_definir_wifi() revenu. reconfig_etat
             * reste EN_COURS d'ici là : cela suffit à rejeter une reconfiguration
             * concurrente pendant cette courte fenêtre. */
        }
        portEXIT_CRITICAL(&verrou_reconfig);

        if (persister) {
            source_courante = SOURCE_REGLAGES;
            /* Le mot de passe n'est jamais journalisé (SSID seul). */
            esp_err_t erreur = reglages_definir_wifi(candidat_ssid, candidat_pass);
            if (erreur != ESP_OK) {
                ESP_LOGE(TAG, "persistance des nouveaux identifiants wifi echouee : %s",
                         esp_err_to_name(erreur));
                /* Connecté mais NON persisté : ne pas mentir à l'écran avec REUSSI.
                 * Repli sûr — un reboot relira les identifiants précédents, jamais
                 * un candidat qu'on n'a pas pu enregistrer. */
                reconfig_etat = WIFI_RECONFIG_CONNECTE_NON_PERSISTE;
            } else {
                ESP_LOGI(TAG, "nouveaux identifiants wifi persistes, SSID '%s'", candidat_ssid);
                reconfig_etat = WIFI_RECONFIG_REUSSI;
            }
        }

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
    char reglages_ssid[33];
    char reglages_pass[64];
    if (reglages_wifi(reglages_ssid, sizeof(reglages_ssid), reglages_pass, sizeof(reglages_pass))) {
        /* Identifiants saisis par l'utilisateur depuis l'écran de
         * configuration (sous-projet 7), rangés dans NOTRE espace de noms NVS
         * « ktouch » — jamais dans la NVS WiFi d'esp_wifi. Prioritaires sur
         * tout le reste : un utilisateur qui a saisi ses identifiants à
         * l'écran ne doit jamais les voir ignorés au profit d'une valeur de
         * compilation (Kconfig) ou d'un réglage hérité du firmware d'origine.
         * Une configuration neuve, mise à zéro, comme pour la branche Kconfig
         * ci-dessous : aucun BSSID/canal épinglé. */
        strlcpy((char *)config_cible.sta.ssid, reglages_ssid, sizeof(config_cible.sta.ssid));
        strlcpy((char *)config_cible.sta.password, reglages_pass, sizeof(config_cible.sta.password));
        config_cible.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        config_cible.sta.pmf_cfg.capable = true;

        source_courante = SOURCE_REGLAGES;
        strlcpy(ssid_utilise, reglages_ssid, sizeof(ssid_utilise));
        ESP_LOGI(TAG, "identifiants reglages retenus, SSID '%s'", reglages_ssid);
    } else if (CONFIG_KTOUCH_WIFI_SSID[0] != '\0') {
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
        case SOURCE_REGLAGES: return "reglages";
        case SOURCE_CONFIG: return "cfg";
        case SOURCE_NVS: return "nvs";
        default: return "aucun";
    }
}

/* Comparateur qsort : RSSI décroissant (le plus fort signal en tête). Les
 * valeurs sont des dBm négatifs, donc « plus grand » = « plus proche de zéro »
 * = meilleur. */
static int comparer_rssi_desc(const void *a, const void *b)
{
    const wifi_reseau_t *ra = (const wifi_reseau_t *)a;
    const wifi_reseau_t *rb = (const wifi_reseau_t *)b;
    return (int)rb->rssi - (int)ra->rssi;
}

esp_err_t wifi_scanner(wifi_reseau_t *sortie, size_t max, size_t *nb)
{
    if (nb != NULL) {
        *nb = 0;
    }
    if (sortie == NULL || max == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Balayage synchrone (bloquant) sur tous les canaux. NULL = configuration de
     * balayage par défaut ; true = l'appel ne rend la main qu'une fois terminé. */
    esp_err_t erreur = esp_wifi_scan_start(NULL, true);
    if (erreur != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_start a echoue : %s", esp_err_to_name(erreur));
        return erreur;
    }

    uint16_t nb_ap = 0;
    erreur = esp_wifi_scan_get_ap_num(&nb_ap);
    if (erreur != ESP_OK) {
        esp_wifi_clear_ap_list();
        return erreur;
    }
    if (nb_ap == 0) {
        /* esp_wifi_scan_get_ap_records() libère la liste interne ; sans AP, il
         * faut la libérer explicitement pour ne rien fuir. */
        esp_wifi_clear_ap_list();
        return ESP_OK;
    }

    wifi_ap_record_t *enregistrements = calloc(nb_ap, sizeof(*enregistrements));
    if (enregistrements == NULL) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }

    uint16_t nb_lu = nb_ap;
    erreur = esp_wifi_scan_get_ap_records(&nb_lu, enregistrements);
    if (erreur != ESP_OK) {
        /* En cas d'échec, esp_wifi_scan_get_ap_records() n'a pas forcément libéré
         * la liste interne : la libérer explicitement, comme les autres branches. */
        esp_wifi_clear_ap_list();
        free(enregistrements);
        return erreur;
    }

    /* Déduplication par SSID en gardant le meilleur RSSI, dans un tampon
     * intermédiaire dimensionné au pire cas (aucun doublon). */
    wifi_reseau_t *uniques = calloc(nb_lu, sizeof(*uniques));
    if (uniques == NULL) {
        free(enregistrements);
        return ESP_ERR_NO_MEM;
    }

    size_t nb_uniques = 0;
    for (uint16_t i = 0; i < nb_lu; i++) {
        const wifi_ap_record_t *ap = &enregistrements[i];
        if (ap->ssid[0] == '\0') {
            continue; /* SSID caché : ignoré. */
        }
        char ssid[33];
        strlcpy(ssid, (const char *)ap->ssid, sizeof(ssid));

        size_t j;
        for (j = 0; j < nb_uniques; j++) {
            if (strcmp(uniques[j].ssid, ssid) == 0) {
                break;
            }
        }
        if (j < nb_uniques) {
            /* Déjà vu : on ne garde que la meilleure puissance (et l'authmode
             * associé à cette meilleure occurrence). */
            if (ap->rssi > uniques[j].rssi) {
                uniques[j].rssi = ap->rssi;
                uniques[j].chiffre = (ap->authmode != WIFI_AUTH_OPEN);
            }
        } else {
            strlcpy(uniques[nb_uniques].ssid, ssid, sizeof(uniques[nb_uniques].ssid));
            uniques[nb_uniques].rssi = ap->rssi;
            uniques[nb_uniques].chiffre = (ap->authmode != WIFI_AUTH_OPEN);
            nb_uniques++;
        }
    }

    qsort(uniques, nb_uniques, sizeof(*uniques), comparer_rssi_desc);

    size_t a_copier = (nb_uniques < max) ? nb_uniques : max;
    for (size_t k = 0; k < a_copier; k++) {
        sortie[k] = uniques[k];
    }
    if (nb != NULL) {
        *nb = a_copier;
    }

    free(uniques);
    free(enregistrements);
    return ESP_OK;
}

esp_err_t wifi_reconfigurer(const char *ssid, const char *pass)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (pass == NULL) {
        pass = ""; /* NULL == réseau ouvert, comme une chaîne vide. */
    }
    if (strlen(ssid) >= sizeof(reconfig_candidat_ssid) ||
        strlen(pass) >= sizeof(reconfig_candidat_pass)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* REJET DE RÉENTRANCE (contrat : un seul appelant logique à la fois). Deux
     * appels concurrents pourraient entrelacer leurs get_config → set_config (en
     * RAM) puis leurs armements : l'un finirait par écrire dans `candidat` des
     * identifiants DIFFÉRENTS de ceux réellement appliqués en RAM par l'autre, et
     * GOT_IP persisterait alors, sur une connexion réussie, des identifiants qui
     * n'ont PAS connecté -> reboot sur de mauvais identifiants -> appareil
     * injoignable (précisément l'interdit du brief). On revendique donc le créneau
     * de façon ATOMIQUE (check-and-set de reconfig_etat sous le même portMUX que
     * l'armement) AVANT toute opération esp_wifi_* : tant qu'une reconfiguration
     * est EN_COURS, tout autre appel est refusé, l'entrelacement disparaît.
     * reconfig_etat sert de verrou logique ; en_attente n'est armé qu'à la fin. */
    wifi_reconfig_etat_t etat_precedent;
    portENTER_CRITICAL(&verrou_reconfig);
    if (reconfig_etat == WIFI_RECONFIG_EN_COURS) {
        portEXIT_CRITICAL(&verrou_reconfig);
        return ESP_ERR_INVALID_STATE;
    }
    etat_precedent = reconfig_etat;
    reconfig_etat = WIFI_RECONFIG_EN_COURS; /* créneau revendiqué */
    portEXIT_CRITICAL(&verrou_reconfig);

    /* Copie RAM des identifiants ACTUELLEMENT appliqués, à restaurer en cas
     * d'échec. esp_wifi_get_config() rend la configuration station courante (en
     * RAM depuis WIFI_STORAGE_RAM). Copies bornées à la main : les champs ssid
     * (32 o) et password (64 o) ne sont pas garantis terminés par NUL. */
    wifi_config_t actuelle = {0};
    esp_err_t erreur = esp_wifi_get_config(WIFI_IF_STA, &actuelle);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_config a echoue : %s", esp_err_to_name(erreur));
        /* Rien n'a été appliqué en RAM : on libère le créneau en rendant l'état
         * antérieur (aucune reconfiguration réputée en cours). */
        portENTER_CRITICAL(&verrou_reconfig);
        reconfig_etat = etat_precedent;
        portEXIT_CRITICAL(&verrou_reconfig);
        return erreur;
    }
    char precedent_ssid[33];
    char precedent_pass[64];
    memcpy(precedent_ssid, actuelle.sta.ssid, 32);
    precedent_ssid[32] = '\0';
    memcpy(precedent_pass, actuelle.sta.password, sizeof(precedent_pass) - 1);
    precedent_pass[sizeof(precedent_pass) - 1] = '\0';

    /* Construction d'une configuration NEUVE (aucun BSSID/canal épinglé) avec les
     * identifiants candidats. Mot de passe vide == réseau ouvert : on ne force
     * PAS WPA2 dans ce cas (au contraire du secours Kconfig de wifi_start(), qui
     * reste inchangé). */
    wifi_config_t candidate = {0};
    strlcpy((char *)candidate.sta.ssid, ssid, sizeof(candidate.sta.ssid));
    strlcpy((char *)candidate.sta.password, pass, sizeof(candidate.sta.password));
    if (pass[0] == '\0') {
        candidate.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        candidate.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        candidate.sta.pmf_cfg.capable = true;
    }

    /* Application EN RAM uniquement (WIFI_STORAGE_RAM posé dans wifi_start()). */
    erreur = esp_wifi_set_config(WIFI_IF_STA, &candidate);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config (candidat) a echoue : %s", esp_err_to_name(erreur));
        /* Créneau libéré : l'armement n'a pas eu lieu, en_attente reste faux. */
        portENTER_CRITICAL(&verrou_reconfig);
        reconfig_etat = etat_precedent;
        portEXIT_CRITICAL(&verrou_reconfig);
        return erreur;
    }

    /* ORDRE VOULU : set_config → disconnect → armement. La déconnexion volontaire
     * ci-dessous déclenche un WIFI_EVENT_STA_DISCONNECTED géré par sur_evenement()
     * sur la tâche sys_evt, qui reconnecte alors avec les NOUVEAUX identifiants.
     * L'armement (reconfig_en_attente) n'ayant pas encore eu lieu, cette
     * déconnexion est normalement traitée sans être comptée ; si sys_evt tarde et
     * la traite après l'armement, elle compte au pire comme tentative n°1 — sûr,
     * car un comptage anticipé ne fait que restaurer plus tôt vers des identifiants
     * connus. Et aucune IP obtenue avant l'armement ne peut être prise à tort pour
     * une réussite candidate : on ne persiste jamais sans avoir armé en_attente. */
    erreur = esp_wifi_disconnect();
    if (erreur != ESP_OK) {
        /* La station n'était peut-être pas connectée : la boucle de reconnexion
         * (tenter_connexion() sur chaque DISCONNECTED) emploiera de toute façon la
         * nouvelle configuration. On journalise sans échouer. */
        ESP_LOGW(TAG, "esp_wifi_disconnect a echoue : %s", esp_err_to_name(erreur));
    }

    /* Armement de l'état partagé sous verrou. La réentrance étant rejetée plus
     * haut, aucune reconfiguration ne peut être en attente ici : la copie
     * « précédente » reflète bien les derniers identifiants réellement appliqués.
     * reconfig_etat est déjà EN_COURS (créneau revendiqué). */
    portENTER_CRITICAL(&verrou_reconfig);
    strlcpy(reconfig_precedent_ssid, precedent_ssid, sizeof(reconfig_precedent_ssid));
    strlcpy(reconfig_precedent_pass, precedent_pass, sizeof(reconfig_precedent_pass));
    strlcpy(reconfig_candidat_ssid, ssid, sizeof(reconfig_candidat_ssid));
    strlcpy(reconfig_candidat_pass, pass, sizeof(reconfig_candidat_pass));
    reconfig_tentatives = 0;
    reconfig_en_attente = true;
    portEXIT_CRITICAL(&verrou_reconfig);

    ESP_LOGI(TAG, "reconfiguration wifi armee, SSID candidat '%s'", ssid);
    return ESP_OK;
}

void wifi_etat(char *ssid_sortie, size_t taille, bool *connecte)
{
    if (ssid_sortie != NULL && taille > 0) {
        strlcpy(ssid_sortie, ssid_utilise, taille);
    }
    if (connecte != NULL) {
        *connecte = connectee;
    }
}

wifi_reconfig_etat_t wifi_reconfig_etat(void)
{
    return reconfig_etat;
}
