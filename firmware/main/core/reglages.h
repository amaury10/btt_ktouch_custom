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

/* Si aucun hôte n'a jamais été enregistré en NVS (ni sous la clé actuelle, ni
 * sous les deux anciennes clés de migration), reglages_charger() se rabat sur
 * CONFIG_KTOUCH_KLIPPER_HOST/CONFIG_KTOUCH_KLIPPER_PORT (Kconfig.projbuild) —
 * même rôle que CONFIG_KTOUCH_WIFI_SSID pour le WiFi. Ce secours EST pris en
 * compte par reglages_configures() ci-dessous (il rend vrai dès qu'une
 * adresse est disponible, quelle qu'en soit la source) : c'est ce qui permet
 * à app_main.c de démarrer la boucle d'interrogation même sur un appareil où
 * l'écran de première configuration (ECRAN_CONFIGURATION, sous-jalon 2b) n'a
 * jamais eu l'occasion de servir — écran indisponible (pt_display_init() en
 * échec), appareil de développement flashé la toute première fois sans
 * jamais y toucher, ou tout simplement personne devant l'appareil. Texte
 * corrigé (revue tâche 8, round 1, Q6) : la version précédente de ce
 * commentaire disait "pas encore d'écran de configuration" -- l'écran existe
 * depuis la tâche 8, ce secours reste utile pour les cas ci-dessus, pas pour
 * son absence. Il n'est en revanche JAMAIS réécrit en NVS : un réglage
 * jamais saisi par l'utilisateur ne doit pas devenir un état persistant. */
bool        reglages_hote(backend_hote_t *sortie);
esp_err_t   reglages_definir_hote(const backend_hote_t *hote);

const char *reglages_backend(void);      /* "moonraker" par défaut */
esp_err_t   reglages_definir_backend(const char *nom);
