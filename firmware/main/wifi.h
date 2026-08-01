#pragma once

/* Connexion WiFi station : identifiants issus de Kconfig (voir
 * Kconfig.projbuild), jamais du dépôt. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t wifi_start(void);
bool wifi_is_connected(void);

/* Recopie l'adresse IP courante dans `out` (chaîne "0.0.0.0" tant qu'aucune
 * adresse n'a été obtenue). Rend l'état de connexion courant. */
bool wifi_ip_string(char *out, size_t len);

/* Rend true et recopie dans `out` le message (esp_err_to_name()) du dernier
 * esp_wifi_connect() infructueux, s'il y en a un depuis la dernière
 * connexion réussie. Rend false (out inchangé) sinon. Sert à afficher la
 * cause d'un échec WiFi directement à l'écran : sans câble série, c'est le
 * seul canal de diagnostic qui survit à une panne WiFi. */
bool wifi_last_connect_error(char *out, size_t len);

/* Rend true et recopie dans `out` un texte ASCII court décrivant la dernière
 * raison de déconnexion (wifi_event_sta_disconnected_t::reason), avec son
 * code numérique entre parenthèses (ex. "NO_AP_FOUND (201)"), s'il y en a eu
 * une depuis la dernière connexion réussie. Rend false (out inchangé) sinon.
 * C'est le diagnostic le plus utile en cas d'échec d'association : SSID
 * introuvable, mot de passe refusé, etc. n'aboutissent jamais à la même
 * raison. */
bool wifi_last_disconnect_reason(char *out, size_t len);

/* Nombre de tentatives de connexion (esp_wifi_connect()) effectuées depuis ce
 * démarrage. Distingue « retente inlassablement » de « bloqué après un seul
 * essai » sur la ligne d'état. */
uint32_t wifi_connect_attempts(void);

/* Source des identifiants employés pour la connexion en cours : "cfg" si le
 * secours Kconfig a été utilisé, "nvs" s'ils viennent de la NVS partagée avec
 * le firmware d'origine, "aucun" si ni l'un ni l'autre n'avait de SSID.
 * Recopie aussi le SSID utilisé dans `ssid_out` (chaîne vide si aucun) — ne
 * recopie JAMAIS le mot de passe : cette information est destinée à l'écran
 * et à /log, tous deux accessibles à quiconque est sur le réseau ou tient une
 * photo de l'appareil. */
const char *wifi_credential_source(char *ssid_out, size_t len);

/* Un réseau vu lors d'un balayage (wifi_scanner()). `ssid` fait 32 octets + NUL
 * (la taille d'un SSID 802.11) ; `rssi` est la puissance reçue en dBm (négatif,
 * plus proche de zéro = meilleur signal) ; `chiffre` est vrai dès que le réseau
 * n'est pas ouvert (authmode ≠ WIFI_AUTH_OPEN). */
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool chiffre;
} wifi_reseau_t;

/* Balaye les réseaux WiFi à portée et remplit `sortie` (au plus `max` entrées)
 * avec les réseaux trouvés, dédupliqués par SSID (on garde la meilleure
 * puissance), triés par RSSI décroissant, les SSID cachés (vides) ignorés.
 * `*nb` reçoit le nombre d'entrées écrites. Rend ESP_OK même si aucun réseau
 * n'est trouvé (`*nb` = 0), une erreur esp_wifi_* si le balayage échoue.
 *
 * ATTENTION — CET APPEL EST BLOQUANT : esp_wifi_scan_start() est demandé en mode
 * synchrone et ne rend la main qu'une fois le balayage terminé, soit plusieurs
 * SECONDES. Il NE DOIT donc JAMAIS être appelé depuis un rappel LVGL (il gèlerait
 * l'affichage) : l'appelant (écran de réglages) doit le lancer hors du fil LVGL,
 * typiquement depuis une petite tâche dédiée qui reposte le résultat vers LVGL.
 * Cette fonction ne fournit que la primitive bloquante ; la gestion du fil est à
 * la charge de l'appelant. */
esp_err_t wifi_scanner(wifi_reseau_t *sortie, size_t max, size_t *nb);

/* Applique de NOUVEAUX identifiants WiFi À CHAUD, sans redémarrer, selon le
 * principe « essayer PUIS persister » — conçu pour qu'un mot de passe erroné
 * saisi à l'écran ne rende JAMAIS l'appareil injoignable :
 *   - les nouveaux identifiants sont appliqués EN RAM uniquement
 *     (esp_wifi_set_config, jamais la NVS WiFi d'esp_wifi) ;
 *   - ils ne sont PERSISTÉS (via reglages_definir_wifi) qu'une fois la connexion
 *     RÉELLEMENT établie (IP_EVENT_STA_GOT_IP reçu pendant qu'ils sont candidats) ;
 *   - en cas d'échec (plusieurs tentatives sans IP), les identifiants PRÉCÉDENTS
 *     (gardés en copie RAM) sont RESTAURÉS et rien n'est persisté — un simple
 *     redémarrage relit alors les derniers identifiants qui ont réellement connecté.
 * `pass` NULL ou vide = réseau ouvert. Rend ESP_OK si la reconfiguration a bien
 * été armée (le RÉSULTAT — réussite ou échec — s'observe ensuite de façon non
 * bloquante via wifi_reconfig_etat()), ou une erreur si les identifiants sont
 * invalides / si l'application RAM a échoué (aucun changement dans ce cas). */
esp_err_t wifi_reconfigurer(const char *ssid, const char *pass);

/* État de la connexion pour l'écran : recopie le SSID courant dans `ssid_sortie`
 * (au plus `taille` octets, chaîne vide si aucun) et pose `*connecte` à l'état de
 * connexion. Le mot de passe n'est JAMAIS exposé. `ssid_sortie` et/ou `connecte`
 * peuvent être NULL. */
void wifi_etat(char *ssid_sortie, size_t taille, bool *connecte);

/* Suivi non bloquant d'une reconfiguration à chaud (wifi_reconfigurer()) :
 *   - INACTIF : aucune reconfiguration depuis le démarrage ;
 *   - EN_COURS : reconfiguration armée, en attente d'une IP ;
 *   - REUSSI  : la dernière reconfiguration a connecté et a été persistée ;
 *   - ECHOUE  : la dernière reconfiguration a échoué ; les identifiants
 *               précédents ont été restaurés, rien n'a été persisté. */
typedef enum {
    WIFI_RECONFIG_INACTIF,
    WIFI_RECONFIG_EN_COURS,
    WIFI_RECONFIG_REUSSI,
    WIFI_RECONFIG_ECHOUE
} wifi_reconfig_etat_t;

wifi_reconfig_etat_t wifi_reconfig_etat(void);
