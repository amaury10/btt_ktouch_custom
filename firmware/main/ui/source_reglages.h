/* Façade réglages hôte : même raison d'être que ui/source_etat.h.
 *
 * firmware/main/core/reglages.c inclut nvs.h (ESP-IDF) : il ne compile ni ne
 * se lie jamais hors cible. reglages.h, lui, ne dépend que de esp_err.h/
 * backend.h/stdbool.h (tous compilables sur PC) et pourrait donc, à la
 * lettre, être inclus depuis un écran PC-compilable — mais SEULE reglages.c
 * porte la définition réelle de reglages_hote()/reglages_definir_hote(), et
 * elle n'est jamais liée dans un build PC (host-test, simulateur). Un écran
 * qui les appellerait directement cesserait donc de LIER sur PC, même s'il
 * continuait à compiler.
 *
 * Cette façade porte le même contrat que reglages_hote()/reglages_definir_hote()
 * (voir firmware/main/core/reglages.h), sous deux implémentations :
 * - firmware/main/ui/source_reglages_esp.c, sur cible, transmet directement ;
 * - simulateur/source_reglages_sim.c, sur PC (harnais de tests hôte compris,
 *   qui réutilise ce fichier comme il réutilise déjà source_etat_sim.c —
 *   voir host-test/CMakeLists.txt), stocke dans une variable statique de
 *   fichier, jamais dans la NVS, relisable par les tests.
 *
 * Seul l'écran de première configuration (tâche 8) consomme cette façade
 * aujourd'hui : c'est lui qui doit préremplir son champ avec l'hôte déjà
 * enregistré et enregistrer une saisie, sans jamais cesser de compiler ET de
 * se lier sur PC. */
#pragma once

#include <stdbool.h>

#include "backend.h"
#include "esp_err.h"

/* Copie l'hôte actuellement connu dans `sortie` (si non NULL) et rend vrai
 * s'il est exploitable — reprise exacte du contrat de reglages_hote() :
 * `sortie` reste toujours entièrement écrit, y compris quand la fonction rend
 * faux (adresse vide, port par défaut, jamais une valeur indéterminée). */
bool ui_reglages_hote(backend_hote_t *sortie);

/* Enregistre `hote` — reprise exacte du contrat de reglages_definir_hote() :
 * rend ESP_ERR_INVALID_ARG si `hote` est NULL, propage toute autre erreur de
 * l'implémentation sous-jacente. Un appel réussi doit se relire identique via
 * ui_reglages_hote() ci-dessus, immédiatement, sans redémarrage. */
esp_err_t ui_reglages_definir_hote(const backend_hote_t *hote);
