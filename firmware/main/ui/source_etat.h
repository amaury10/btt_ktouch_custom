/* La seule porte par laquelle un écran (ou l'habillage) voit le monde.
 *
 * core/boucle.c inclut FreeRTOS : un écran qui appellerait directement
 * boucle_instantane()/boucle_commander() cesserait de compiler sur PC, et le
 * simulateur perdrait tout moyen de le faire tourner. Cette façade porte
 * donc exactement le même contrat que boucle_instantane()/boucle_commander()
 * (voir firmware/main/core/boucle.h), sous deux implémentations :
 * - firmware/main/ui/source_etat_esp.c, sur cible, transmet en trois lignes
 *   à boucle_instantane()/boucle_commander() ;
 * - simulateur/source_etat_sim.c, sur PC, lit le magasin d'état que la
 *   boucle du simulateur fait elle-même tourner avec boucle_cycle()
 *   (core/boucle_cycle.c), sans FreeRTOS puisque le simulateur est
 *   mono-thread.
 *
 * Chaque build PC (harnais de tests hôte, simulateur) ne lie que
 * source_etat_sim.c ; seule la cible ESP-IDF lie source_etat_esp.c — les deux
 * ne sont jamais compilées ensemble dans un même exécutable. */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "liaison.h"

/* Copie sous verrou (côté ESP) le dernier état validé dans `dest`, qui doit
 * faire exactement `taille` octets — la taille réelle de l'état du backend
 * en cours (typiquement sizeof(etat_klipper_t), voir web.c pour l'usage déjà
 * établi de ce même triplet). Lit aussi, dans la MÊME prise de verrou,
 * `generation` et `liaison` : les trois valeurs sont donc toujours
 * cohérentes entre elles, jamais une génération qui ne correspond pas au
 * contenu qu'on vient de copier. `dest` n'est jamais un pointeur vers un
 * tampon interne : il reste valable et exact quel que soit le temps que
 * l'appelant met à s'en servir ensuite, y compris depuis un rappel LVGL qui
 * cède la main entre deux instructions.
 *
 * Rend false SANS RIEN TOUCHER (ni `dest`, ni `*generation`, ni `*liaison`)
 * si la boucle n'a pas démarré, si `dest` est NULL, ou si `taille` ne
 * correspond pas à la taille réelle de l'état du backend en cours — reprise
 * exacte du contrat de boucle_instantane() (core/boucle.h). `generation` et
 * `liaison` peuvent valoir NULL si l'appelant ne s'y intéresse pas. */
bool ui_etat_instantane(void *dest, size_t taille, uint32_t *generation, liaison_etat_t *liaison);

/* Empile `action` pour être exécutée par la boucle d'interrogation et rend
 * la main IMMÉDIATEMENT — ne bloque jamais l'appelant, ne l'attend jamais
 * même brièvement. `arguments_json` peut être NULL. Le code de retour dit
 * seulement si la commande a été acceptée dans la file, jamais si elle a
 * réussi : un écran qui veut connaître le résultat doit relire l'état via
 * ui_etat_instantane() au cycle suivant, pas attendre celui-ci.
 *
 * Rend ESP_ERR_INVALID_STATE si la boucle n'a pas démarré, ESP_ERR_INVALID_ARG
 * si `action` est NULL ou trop long, ESP_ERR_NO_MEM si la file est pleine —
 * reprise exacte du contrat de boucle_commander() (core/boucle.h). */
esp_err_t ui_commander(const char *action, const char *arguments_json);

/* Taille de tampon suffisante pour toute action rendue par ui_commande_echec()
 * ci-dessous : reprend la marge de BOUCLE_ACTION_MAX (core/boucle.c) et
 * ACTION_MAX (simulateur/source_etat_sim.c), jamais incluses ici pour ne pas
 * coupler ce fichier-façade à leurs détails internes -- les trois valeurs
 * doivent rester en accord. Corrigé (revue tâche 9, fix round 1, LOW) : la
 * version précédente de ce commentaire prétendait que
 * host-test/tests/test_commandes.c « vérifie » les trois en accord via une
 * action de 14 octets ("arret_urgence") -- FAUX pour BOUCLE_ACTION_MAX,
 * propre à core/boucle.c, jamais compilé sur PC (ESP-only, voir le
 * commentaire de tête de ui/source_etat_esp.c) : aucun test hôte ne peut
 * l'atteindre. Le test hôte exerce réellement ACTION_MAX
 * (simulateur/source_etat_sim.c, lié par host-test) et cette macro-ci ; l'
 * accord avec BOUCLE_ACTION_MAX reste une invariante à maintenir À LA MAIN
 * (revue de code), jamais vérifiée mécaniquement avant la cible ESP-IDF
 * (tâche 10). */
#define UI_COMMANDE_ACTION_MAX 32

/* Tâche 9 : remonte l'échec d'une commande exécutée de façon ASYNCHRONE par
 * la boucle d'interrogation -- après que ui_commander() a déjà rendu ESP_OK
 * (acceptée en file), donc après que l'appelant d'origine (un rappel de
 * bouton LVGL) a déjà rendu la main sans connaître le résultat réel (voir le
 * commentaire de ui_commander() ci-dessus : son code de retour ne dit jamais
 * si la commande a réussi). Sans cette fonction, un tel échec ne serait
 * journalisé que côté boucle (JOURNAL_ALERTE), invisible à l'écran -- exactement
 * le trou que la revue de la tâche 9 a nommé.
 *
 * Rend true et copie dans `action` (au moins `taille` octets, voir
 * UI_COMMANDE_ACTION_MAX) le nom de la commande dont le DERNIER résultat connu
 * est un échec, si cet échec n'a pas déjà été rendu par un appel précédent --
 * consommation unique, comme une file à un seul élément : un second appel
 * immédiat rend false tant qu'aucun nouvel échec n'est survenu depuis. Conçue
 * pour être appelée à intervalle régulier par habillage_pomper() (la seule
 * tâche qui touche LVGL à cadence fixe, voir ui/habillage.c), qui traduit un
 * true en habillage_notifier(..., true) -- c'est le SEUL site qui décide de
 * remonter ceci à l'écran, jamais boucle.c/source_etat_sim.c eux-mêmes
 * (core/ reste LVGL-free, voir leur propre commentaire de tête).
 *
 * Rend false sans toucher `action` si la boucle n'a pas démarré, si `action`
 * est NULL, si `taille` vaut 0, ou si aucun échec n'est en attente. */
bool ui_commande_echec(char *action, size_t taille);
