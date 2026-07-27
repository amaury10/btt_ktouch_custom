#include "boucle_cycle.h"

bool boucle_cycle(etat_store_t *store, liaison_t *liaison, const backend_desc_t *desc)
{
    /* Remis à zéro ici, comme le faisait boucle_tache() : un appel précédent
     * de boucle_traiter_commandes() (resté côté shell, voir boucle.c) aurait
     * pu, contre le contrat, écrire dans ce même tampon arrière — cette
     * remise à zéro garantit qu'un tel débris ne contamine jamais le
     * rafraîchissement qui suit. */
    void *arriere = etat_store_tampon_arriere(store);
    esp_err_t erreur = desc->rafraichir(arriere);

    if (erreur == ESP_OK) {
        liaison_succes(liaison);
        return true;
    }

    liaison_echec(liaison);
    return false;
}
