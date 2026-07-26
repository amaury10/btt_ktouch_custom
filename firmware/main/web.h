#pragma once

/* Serveur HTTP minimal : état, journal, retour manuel et mise à jour de
 * l'image applicative. Voir web.c pour le détail des cinq routes. */

#include <stdbool.h>

#include "esp_err.h"

esp_err_t web_start(void);

/* Signale si un périphérique tactile a été enregistré, pour /status. Ce
 * module ne dépend pas de LVGL : app_main appelle ceci après avoir vérifié
 * lui-même le tactile. */
void web_set_touch_available(bool disponible);
