#pragma once

/* Serveur HTTP minimal : état, journal, retour manuel. Voir web.c pour le
 * détail des quatre routes — il n'y a délibérément aucune route de mise à
 * jour, voir le commentaire en tête de web.c. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t web_start(void);

/* Signale si un périphérique tactile a été enregistré, pour /status. Ce
 * module ne dépend pas de LVGL : app_main appelle ceci après avoir vérifié
 * lui-même le tactile. */
void web_set_touch_available(bool disponible);

/* Renseigne le compteur de démarrages consécutifs affiché par /status
 * (voir rescue_count_boot()). app_main appelle ceci une fois, avec la valeur
 * obtenue tout au début du démarrage. */
void web_set_boot_count(uint32_t compteur);

/* Renseigne le nom de la raison du reset précédent affiché par /status
 * (diagnostic des reboots en boucle : BROWNOUT = alimentation, PANIC/TASK_WDT
 * = logiciel -- voir le bloc correspondant d'app_main.c). `nom` doit pointer
 * vers une chaîne à durée de vie statique : ce module la garde telle quelle,
 * sans copie. */
void web_set_reset_reason(const char *nom);
