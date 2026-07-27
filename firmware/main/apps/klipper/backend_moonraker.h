/* Backend Moonraker : parle en HTTP à une vraie machine Klipper.
 *
 * Deuxième consommateur réel du contrat backend_desc_t (voir backend.h), aux
 * côtés de backend_factice.h. C'est celui-ci que la boucle d'interrogation
 * (core/boucle.h) fait tourner sur l'appareil. */
#pragma once

#include "backend.h"

const backend_desc_t *backend_moonraker_desc(void);
