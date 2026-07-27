/* Réglages persistants du firmware.
 *
 * Rangés dans l'espace de noms NVS « ktouch ». La partition nvs est PARTAGÉE
 * avec le firmware d'origine, qui y garde ses identifiants WiFi : on n'écrit
 * que dans notre espace de noms, et on n'efface jamais la partition. */
#pragma once

#include <stdbool.h>

#include "backend.h"
#include "esp_err.h"

#define REGLAGES_ESPACE_NOMS "ktouch"

/* reglages_charger() est censé être appelé une fois au démarrage, avant tout
 * accesseur ci-dessous. Ce n'est pas une exigence dure : grâce aux
 * initialiseurs statiques du .c, un accesseur appelé avant tout chargement
 * (ou après un chargement qui a échoué) rend simplement les valeurs par
 * défaut — adresse vide, port 7125, backend "moonraker" — jamais une valeur
 * indéterminée. */
esp_err_t   reglages_charger(void);
bool        reglages_configures(void);   /* faux au tout premier démarrage */

bool        reglages_hote(backend_hote_t *sortie);
esp_err_t   reglages_definir_hote(const backend_hote_t *hote);

const char *reglages_backend(void);      /* "moonraker" par défaut */
esp_err_t   reglages_definir_backend(const char *nom);
