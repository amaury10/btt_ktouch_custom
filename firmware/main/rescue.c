/* Sauvetage automatique : sans accès série, c'est le seul moyen de revenir au
 * firmware d'origine si ce firmware-ci ne parvient pas à joindre le réseau.
 *
 * Le principe est volontairement pauvre : un minuteur armé au démarrage, que
 * seule une connexion WiFi réussie désarme. Il ne dépend ni de l'écran, ni du
 * tactile, ni d'aucune bibliothèque tierce — donc il survit à leur défaillance. */

#include "rescue.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "rescue";
static esp_timer_handle_t minuteur;

esp_err_t rescue_switch_to_other_slot(void)
{
    const esp_partition_t *cible = esp_ota_get_next_update_partition(NULL);
    if (cible == NULL) {
        ESP_LOGE(TAG, "aucun autre slot OTA disponible");
        return ESP_ERR_NOT_FOUND;
    }
    /* Le slot voisin contient le firmware d'origine, que nous n'écrasons
     * jamais : la bascule suffit, il n'y a rien à téléverser. */
    esp_err_t erreur = esp_ota_set_boot_partition(cible);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "bascule impossible : %s", esp_err_to_name(erreur));
        return erreur;
    }
    ESP_LOGW(TAG, "bascule vers %s, redemarrage", cible->label);
    return ESP_OK;
}

static void sur_echeance(void *arg)
{
    ESP_LOGE(TAG, "reseau injoignable dans le delai imparti");
    if (rescue_switch_to_other_slot() == ESP_OK) {
        esp_restart();
    }
    /* Si même la bascule échoue, redémarrer quand même : le bootloader
     * retombera sur le slot que otadata désigne encore. */
    esp_restart();
}

esp_err_t rescue_arm(uint32_t delai_ms)
{
    const esp_timer_create_args_t args = {
        .callback = sur_echeance,
        .name = "rescue",
    };
    esp_err_t erreur = esp_timer_create(&args, &minuteur);
    if (erreur != ESP_OK) {
        return erreur;
    }
    ESP_LOGW(TAG, "sauvetage arme : %lu ms pour joindre le reseau", (unsigned long)delai_ms);
    return esp_timer_start_once(minuteur, (uint64_t)delai_ms * 1000);
}

void rescue_disarm(void)
{
    if (minuteur != NULL) {
        esp_timer_stop(minuteur);
        esp_timer_delete(minuteur);
        minuteur = NULL;
        ESP_LOGI(TAG, "sauvetage desarme : le reseau repond");
    }
}
