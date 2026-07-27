/* Corps pur d'un cycle de la boucle d'interrogation.
 *
 * Extrait de boucle_tache() (core/boucle.c) pour rester compilable et
 * testable sur PC, exactement pour la raison que host-test/README.md pose en
 * principe : « si un fichier de core/ a besoin d'un en-tête [FreeRTOS/ESP-IDF],
 * c'est le signe qu'il fait trop de choses et qu'il faut en extraire la
 * partie pure et testable ». boucle.c en avait besoin uniquement pour la
 * tâche, la file de commandes et le mutex — jamais pour la logique d'un
 * cycle lui-même, qui ne touche que backend.h/etat_store.h/liaison.h, tous
 * déjà portables. C'est cette logique de cycle qui portait le défaut du
 * backend factice (progression figée, magasin d'état jamais mis à jour) :
 * sans ce fichier, aucun test hôte ne pouvait la traverser dans l'ordre
 * exact où boucle_tache() l'exécute, et le bug n'était visible qu'après un
 * flash complet sur l'appareil. */
#pragma once

#include <stdbool.h>

#include "backend.h"
#include "etat_store.h"
#include "liaison.h"

/* Rafraîchit `*store` via `desc->rafraichir()` sur son tampon arrière
 * (remis à zéro par cette fonction avant l'appel — voir le contrat documenté
 * sur backend_desc_t::rafraichir dans backend.h, qu'un backend DOIT
 * respecter), puis met à jour `*liaison` selon le résultat.
 *
 * PAS DE VERROU ici : cette fonction ne connaît ni FreeRTOS ni aucun mutex,
 * c'est précisément ce qui la rend testable sur PC. Sur cible, c'est
 * boucle_tache() (le "shell" non portable dans boucle.c) qui reste seul
 * responsable de protéger etat_store_valider() sous g_mutex_etat — jamais la
 * requête réseau que `desc->rafraichir()` peut effectuer ici, qui doit
 * pouvoir prendre plusieurs secondes sans jamais faire attendre un lecteur
 * concurrent (voir le commentaire de g_mutex_etat dans boucle.c). C'est
 * pourquoi cette fonction NE valide PAS elle-même le magasin d'état : elle
 * rend simplement si le rafraîchissement a réussi, à charge pour l'appelant
 * d'appeler etat_store_valider() lui-même — sous verrou sur cible, sans
 * verrou dans les tests hôte.
 *
 * Rend true si `desc->rafraichir()` a rendu ESP_OK (que le contenu ait
 * effectivement changé ou non — c'est etat_store_valider() qui tranche
 * cela), false sinon. `*liaison` est mis à jour dans les deux cas. */
bool boucle_cycle(etat_store_t *store, liaison_t *liaison, const backend_desc_t *desc);
