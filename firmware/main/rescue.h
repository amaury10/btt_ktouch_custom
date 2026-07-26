#pragma once

/* Sauvetage automatique : voir rescue.c pour le principe. */

#include <stdint.h>

#include "esp_err.h"

esp_err_t rescue_arm(uint32_t delai_ms);
void rescue_disarm(void);
esp_err_t rescue_switch_to_other_slot(void);

/* Déclenche la bascule vers l'autre slot en la déléguant à la tâche dédiée
 * de rescue.c (créée au besoin), jamais sur la pile de l'appelant : la
 * bascule implique une vérification SHA-256 et un esp_restart() qui invoque
 * des gestionnaires d'arrêt bloquants (dont esp_wifi_stop), du travail que ni
 * le rappel du minuteur, ni la tâche httpd (web.c), ni app_main lui-même ne
 * doivent porter directement. Ne bloque pas : la tâche notifiée s'en charge
 * et redémarre l'appareil. Point d'entrée commun au chemin du compteur de
 * démarrages (avant même rescue_arm()), à la route /revert (web.c), et au
 * minuteur lui-même — les trois profitent ainsi du même dernier recours
 * (effacement d'otadata) en cas d'échec de la bascule normale. */
void rescue_switch_now(void);

/* Au-delà de ce nombre de démarrages consécutifs (voir rescue_count_boot()),
 * app_main doit basculer immédiatement sur l'autre slot sans rien tenter
 * d'autre : une boucle de redémarrage, quelle qu'en soit la cause, se solde
 * ainsi par un retour au firmware d'origine. */
#define RESCUE_DEMARRAGES_MAX 3

/* Compteur de démarrages en mémoire RTC (survit aux redémarrages, pas aux
 * coupures d'alimentation) : ferme les pannes plus rapides que le minuteur
 * (panique, chien de garde, débordement de pile...), à condition qu'elles
 * surviennent après l'entrée dans app_main. Les échecs d'initialisation de
 * la PSRAM, appelée depuis cpu_start.c avant l'ordonnanceur, ne sont PAS
 * couverts par ce compteur — voir sdkconfig.defaults pour la parade. À
 * appeler tout au début d'app_main ; rend le nombre de démarrages
 * consécutifs, y compris celui-ci. */
uint32_t rescue_count_boot(void);

/* À appeler avec rescue_disarm(), depuis IP_EVENT_STA_GOT_IP : une connexion
 * réussie prouve que ce firmware est viable. */
void rescue_reset_boot_count(void);
