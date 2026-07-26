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
