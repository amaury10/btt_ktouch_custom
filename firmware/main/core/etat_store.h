/* Magasin d'état à double tampon.
 *
 * La tâche réseau remplit le tampon arrière ; l'interface lit le tampon avant.
 * Ils ne sont jamais les mêmes, donc un écran ne peut pas lire une structure à
 * moitié réécrite. La permutation n'a lieu que si le contenu a réellement
 * changé, ce qui évite de redessiner l'écran à chaque interrogation.
 *
 * Générique sur `void *` et une taille : le magasin ne connaît aucun modèle
 * d'état, ce qui le rend réutilisable tel quel par une autre application. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void    *avant;
    void    *arriere;
    size_t   taille;
    uint32_t generation;
} etat_store_t;

bool        etat_store_init(etat_store_t *store, size_t taille);
void        etat_store_liberer(etat_store_t *store);

/* Tampon à remplir, remis à zéro à chaque appel. */
void       *etat_store_tampon_arriere(etat_store_t *store);

/* Compare arrière et avant ; permute et incrémente la génération si différents.
 * Rend true en cas de changement. */
bool        etat_store_valider(etat_store_t *store);

const void *etat_store_lire(const etat_store_t *store);
uint32_t    etat_store_generation(const etat_store_t *store);
