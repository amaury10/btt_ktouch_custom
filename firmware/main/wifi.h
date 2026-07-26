#pragma once

/* Connexion WiFi station : identifiants issus de Kconfig (voir
 * Kconfig.projbuild), jamais du dépôt. */

#include <stdbool.h>
#include <stddef.h>

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
