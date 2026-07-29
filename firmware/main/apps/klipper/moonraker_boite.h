/* Boîte aux lettres portable entre la tâche WebSocket et la boucle
 * d'interrogation (spec §4, jalon 3a tâche 5) : UN SLOT, pas une file — un
 * état fusionné déposé ÉCRASE le précédent, jamais rejoué. Moonraker pousse
 * des `notify_status_update` bien plus vite que la boucle ne les consomme
 * (WS quasi instantané contre un drain à 250 ms) ; ce que l'écran doit voir
 * est le DERNIER état connu, pas l'historique de ce qui a changé entre-temps
 * — exactement la même politique de fraîcheur que le double tampon de
 * `etat_store.h`, un cran plus tôt dans le pipeline.
 *
 * Portable à dessein : ce fichier n'inclut ni FreeRTOS ni aucun en-tête
 * ESP-IDF, pour rester testable au harnais hôte (voir
 * host-test/tests/test_moonraker_boite.c). Il ne pose donc AUCUN verrou lui
 * même — `boite_deposer`/`boite_drainer` ne sont PAS thread-safe entre elles.
 * Côté ESP, c'est l'appelant (`moonraker_ws.c`, tâche 5 également, qui lui
 * inclut FreeRTOS) qui fournit ce verrou autour de chaque appel : la tâche WS
 * dépose sous verrou, `backend_moonraker.c` draine sous le même verrou via
 * les enveloppes exposées par `moonraker_ws.h`. Les tests hôte, eux,
 * n'appellent jamais cette boîte que depuis un seul fil d'exécution — aucun
 * verrou n'y est donc nécessaire non plus. */
#pragma once

#include <stdbool.h>

#include "etat_klipper.h"

typedef struct {
    etat_klipper_t etat; /* dernier état fusionné déposé, valide seulement si neuf */
    bool            neuf; /* true entre un dépôt et le prochain drain réussi */
} moonraker_boite_t;

/* Zéro-initialisation suffisante (`= {0}` ou `memset`) : `neuf` à false,
 * aucun invariant supplémentaire à établir. Pas de fonction d'init dédiée. */

/* Copie `*etat` dans la boîte et marque `neuf`. ÉCRASE sans condition tout
 * dépôt précédent non encore drainé — c'est le contrat central de ce
 * fichier (voir l'en-tête ci-dessus) : deux dépôts consécutifs sans drain
 * entre les deux ne laissent QUE le second survivre. */
void boite_deposer(moonraker_boite_t *b, const etat_klipper_t *etat);

/* Si la boîte a du neuf : copie l'état dans `*sortie`, remet `neuf` à false,
 * rend true. Sinon : rend false SANS TOUCHER `*sortie` — l'appelant garde
 * ainsi la responsabilité de conserver son dernier état connu (même
 * politique que `moonraker_parse_status()`/`rpc_fusionner_status()` : une
 * absence de nouveauté n'efface jamais ce qu'on savait déjà). */
bool boite_drainer(moonraker_boite_t *b, etat_klipper_t *sortie);

/* true si un dépôt attend d'être drainé. Lecture seule, sans effet de bord
 * (contrairement à `boite_drainer`) — utile pour un appelant qui veut
 * décider quoi que ce soit sans consommer le dépôt (ex. journal, tests). */
bool boite_a_du_neuf(const moonraker_boite_t *b);
