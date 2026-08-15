/* Parseur PUR de la réponse de sonde du parc (spec
 * 2026-08-15-gestion-parc-design.md) : extrait les 4 champs de parc_etat_t
 * depuis la réponse JSON de
 *   GET /printer/objects/query?print_stats&extruder&heater_bed&display_status
 * Volontairement MINUSCULE -- jamais le parseur etat_klipper_t complet : la
 * sonde ne nourrit que des tuiles d'aperçu, pas l'UI temps réel.
 * Fonction pure (cJSON, aucune allocation conservée), testée host. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "parc_imprimantes.h" /* parc_etat_t */

/* Rend vrai et remplit `sortie` (sonde=true, atteignable=true, champs
 * absents tolérés à zéro / chaîne vide) si `json` est un JSON valide ;
 * faux sinon (`sortie` alors remis à zéro). NULL refusés. */
bool parc_parse_reponse(const char *json, size_t longueur, parc_etat_t *sortie);
