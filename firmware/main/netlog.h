#pragma once

/* Journal réseau : tampon circulaire en RAM alimenté par les logs ESP-IDF,
 * consultable en HTTP sans port série. Voir netlog.c pour les précautions de
 * réentrance. */

#include <stddef.h>

#include "esp_err.h"

esp_err_t netlog_init(void);

/* Recopie dans `out` (taille `len`, toujours terminé par NUL) le contenu
 * actuel du tampon, du plus ancien au plus récent octet conservé. Rend le
 * nombre d'octets recopiés (hors terminateur). */
size_t netlog_snapshot(char *out, size_t len);
