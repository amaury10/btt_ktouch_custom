/* Implémentation cible de la façade réglages (voir source_reglages.h) :
 * transmet telle quelle à reglages_hote()/reglages_definir_hote()
 * (core/reglages.h). Même schéma que ui/source_etat_esp.c pour la même
 * raison : reglages.c touche la NVS (nvs.h, ESP-IDF), donc n'est jamais lié
 * hors cible ; ce fichier-ci, qui ne fait que transmettre, pourrait à la
 * lettre compiler sur PC, mais n'est lié que sur cible — chaque build PC lie
 * simulateur/source_reglages_sim.c à la place. */
#include "source_reglages.h"

#include "reglages.h"

bool ui_reglages_hote(backend_hote_t *sortie)
{
    return reglages_hote(sortie);
}

esp_err_t ui_reglages_definir_hote(const backend_hote_t *hote)
{
    return reglages_definir_hote(hote);
}
