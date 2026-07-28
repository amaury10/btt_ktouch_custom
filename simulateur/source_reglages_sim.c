/* Implémentation PC de la façade réglages (voir ui/source_reglages.h) : ni
 * NVS ni fichier, un backend_hote_t statique de fichier — même schéma que
 * simulateur/source_etat_sim.c pour la même raison (aucune des deux ne peut
 * lier core/reglages.c, qui inclut nvs.h). Réutilisé tel quel par
 * host-test/CMakeLists.txt, comme source_etat_sim.c et plateforme_sim.c le
 * sont déjà : une seule implémentation PC, jamais une copie qui pourrait
 * diverger entre le simulateur et le harnais de tests.
 *
 * État initial identique à celui de reglages.c avant tout reglages_charger()
 * réussi (voir core/reglages.h) : adresse vide, port par défaut, « non
 * configuré ». Rien ici ne persiste au-delà du process : chaque lancement du
 * simulateur ou de la suite de tests hôte repart de cet état, exactement
 * comme un appareil qui n'a jamais rien enregistré en NVS. */
#include "source_reglages.h"

static backend_hote_t g_hote = {
    .adresse = "",
    .port = 7125,
};
static bool g_configure = false;

bool ui_reglages_hote(backend_hote_t *sortie)
{
    if (sortie != NULL) {
        *sortie = g_hote;
    }
    return g_configure;
}

esp_err_t ui_reglages_definir_hote(const backend_hote_t *hote)
{
    if (hote == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    g_hote = *hote;
    g_configure = true;
    return ESP_OK;
}
