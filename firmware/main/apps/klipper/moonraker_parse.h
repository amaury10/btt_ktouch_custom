/* Interprétation de la réponse de /printer/objects/query en état typé.
 *
 * Fonction pure, sans allocation persistante ni accès réseau : c'est ce qui
 * permet de la tester entièrement sur PC (voir host-test/). */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "etat_klipper.h"

/* Rend false si le JSON est absent, malformé, ou ne contient pas
 * result.status. Dans ce cas `sortie` n'est pas modifiée — l'appelant peut
 * ainsi conserver le dernier état connu. */
bool moonraker_parse_status(const char *json, size_t longueur, etat_klipper_t *sortie);
