#pragma once

/* Sauvetage automatique : voir rescue.c pour le principe. */

#include <stdint.h>

#include "esp_err.h"

esp_err_t rescue_arm(uint32_t delai_ms);
void rescue_disarm(void);
esp_err_t rescue_switch_to_other_slot(void);

/* Au-delà de ce nombre de démarrages consécutifs (voir rescue_count_boot()),
 * app_main doit basculer immédiatement sur l'autre slot sans rien tenter
 * d'autre : une boucle de redémarrage, quelle qu'en soit la cause, se solde
 * ainsi par un retour au firmware d'origine. */
#define RESCUE_DEMARRAGES_MAX 3

/* Compteur de démarrages en mémoire RTC (survit aux redémarrages, pas aux
 * coupures d'alimentation) : ferme les pannes plus rapides que le minuteur
 * (panique, chien de garde, débordement de pile...). À appeler tout au début
 * d'app_main ; rend le nombre de démarrages consécutifs, y compris celui-ci. */
uint32_t rescue_count_boot(void);

/* À appeler avec rescue_disarm(), depuis IP_EVENT_STA_GOT_IP : une connexion
 * réussie prouve que ce firmware est viable. */
void rescue_reset_boot_count(void);
