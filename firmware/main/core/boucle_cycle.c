#include "boucle_cycle.h"

uint32_t boucle_cycle_periode_ms(const backend_desc_t *desc, void *etat)
{
    if (desc == NULL || desc->periode_ms == NULL) {
        return BOUCLE_PERIODE_MS_DEFAUT;
    }
    uint32_t periode = desc->periode_ms(etat);
    return periode == 0 ? BOUCLE_PERIODE_MS_DEFAUT : periode;
}

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
