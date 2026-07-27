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

/* En cas d'échec (store NULL ou taille nulle), la structure est laissée
 * entièrement à zéro : `etat_store_liberer()` reste sûr à appeler même sans
 * savoir que l'initialisation a échoué. */
bool        etat_store_init(etat_store_t *store, size_t taille);
void        etat_store_liberer(etat_store_t *store);

/* Tampon à remplir, remis à zéro à chaque appel.
 *
 * Contrat : `store` doit être un magasin initialisé avec succès par
 * `etat_store_init()`. Aucune de ces fonctions ne vérifie ses arguments —
 * ajouter des gardes ici masquerait un bug de l'appelant plutôt que de le
 * révéler. */
void       *etat_store_tampon_arriere(etat_store_t *store);

/* Compare arrière et avant ; permute et incrémente la génération si différents.
 * Rend true en cas de changement. */
bool        etat_store_valider(etat_store_t *store);

/* Rend le tampon avant, celui que l'interface doit lire.
 *
 * Le pointeur rendu n'est valable que jusqu'au prochain appel à
 * etat_store_valider() sur ce même store : si celui-ci permute (retour
 * true), le tampon pointé devient le tampon arrière, qui sera remis à zéro
 * puis réécrit par le prochain etat_store_tampon_arriere(). Ne jamais
 * conserver ce pointeur au travers d'un appel à etat_store_valider() — le
 * relire à chaque fois via etat_store_lire() à la place. */
const void *etat_store_lire(const etat_store_t *store);
uint32_t    etat_store_generation(const etat_store_t *store);
