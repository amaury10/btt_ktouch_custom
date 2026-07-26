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
