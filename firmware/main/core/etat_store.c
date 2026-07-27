#include "etat_store.h"

#include <stdlib.h>
#include <string.h>

bool etat_store_init(etat_store_t *store, size_t taille)
{
    if (store == NULL) {
        return false;
    }
    /* Toute sortie en echec doit laisser une structure sure a liberer : un
     * appelant defensif appellera etat_store_liberer() sans savoir que
     * l'initialisation a echoue, et liberer des pointeurs indetermines
     * (cas d'un store alloue sur la pile, jamais initialise) serait un
     * comportement indefini. */
    store->avant = NULL;
    store->arriere = NULL;
    store->taille = 0;
    store->generation = 0;

    if (taille == 0) {
        return false;
    }
    store->avant = calloc(1, taille);
    store->arriere = calloc(1, taille);
    if (store->avant == NULL || store->arriere == NULL) {
        free(store->avant);
        free(store->arriere);
        store->avant = store->arriere = NULL;
        return false;
    }
    store->taille = taille;
    return true;
}

void etat_store_liberer(etat_store_t *store)
{
    if (store == NULL) {
        return;
    }
    free(store->avant);
    free(store->arriere);
    store->avant = store->arriere = NULL;
    store->taille = 0;
}

void *etat_store_tampon_arriere(etat_store_t *store)
{
    /* La remise à zéro n'est pas une précaution de confort : sans elle, le
     * remplissage d'alignement de la structure garderait des valeurs aléatoires
     * et la comparaison de etat_store_valider() échouerait au hasard, faisant
     * clignoter l'interface sans raison. */
    memset(store->arriere, 0, store->taille);
    return store->arriere;
}

bool etat_store_valider(etat_store_t *store)
{
    if (memcmp(store->avant, store->arriere, store->taille) == 0) {
        return false;
    }
    void *echange = store->avant;
    store->avant = store->arriere;
    store->arriere = echange;
    store->generation++;
    return true;
}

const void *etat_store_lire(const etat_store_t *store)
{
    return store->avant;
}

uint32_t etat_store_generation(const etat_store_t *store)
{
    return store->generation;
}
