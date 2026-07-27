/* Implémentation cible de la façade d'état (voir source_etat.h) : transmet
 * telle quelle à boucle_instantane()/boucle_commander() (core/boucle.h).
 *
 * boucle.h ne dépend lui-même d'aucun en-tête FreeRTOS/ESP-IDF au-delà
 * d'esp_err.h (voir son propre en-tête) : ce fichier-ci pourrait donc, à la
 * lettre, compiler sur PC. Il n'est cependant JAMAIS lié dans un build PC
 * (host-test, simulateur) : c'est boucle.c, pas boucle.h, qui touche
 * FreeRTOS pour de vrai (la tâche, le mutex, la file de commandes), et
 * boucle_instantane()/boucle_commander() n'existent qu'une fois boucle.c
 * compilé et lié — ce que ni host-test/CMakeLists.txt ni
 * simulateur/CMakeLists.txt ne font. Chaque build PC lie
 * simulateur/source_etat_sim.c à la place (voir ce fichier). */
#include "source_etat.h"

#include "boucle.h"

bool ui_etat_instantane(void *dest, size_t taille, uint32_t *generation, liaison_etat_t *liaison)
{
    return boucle_instantane(dest, taille, generation, liaison);
}

esp_err_t ui_commander(const char *action, const char *arguments_json)
{
    return boucle_commander(action, arguments_json);
}
