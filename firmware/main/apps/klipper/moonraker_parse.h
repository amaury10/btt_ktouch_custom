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

/* Taille de tampon suffisante pour tout chemin rendu par
 * moonraker_chemin_commande() ci-dessous : le plus long ("printer/emergency_stop",
 * 22 octets) plus marge pour une future action, jusqu'à l'octet nul. */
#define MOONRAKER_CHEMIN_COMMANDE_MAX 32

/* Traduit une action commune (BACKEND_ACTION_PAUSE/REPRENDRE/ANNULER/URGENCE,
 * voir core/backend.h) en chemin d'API Moonraker (sans '/' de tête, voir
 * moonraker_construire_url() dans backend_moonraker.c), copié dans `chemin`
 * (qui doit faire au moins MOONRAKER_CHEMIN_COMMANDE_MAX octets). Fonction
 * PURE, sans accès réseau : c'est ce qui permet de tester la correspondance
 * action -> chemin sur PC, indépendamment de esp_http_client (voir
 * host-test/tests/test_commandes.c), exactement comme moonraker_parse_status()
 * ci-dessus est testée sans jamais toucher le réseau.
 *
 * Rend false SANS TOUCHER `chemin` si `action` est NULL, si `chemin` est NULL,
 * si `taille` vaut 0, ou si `action` ne correspond à aucune des quatre actions
 * connues — c'est ce qui permet à backend_moonraker_commande() de rendre
 * ESP_ERR_NOT_SUPPORTED sans jamais émettre de requête pour une action
 * inconnue (même contrat que backend_desc_t::commande, voir core/backend.h). */
bool moonraker_chemin_commande(const char *action, char *chemin, size_t taille);
