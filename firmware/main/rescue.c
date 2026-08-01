/* Sauvetage automatique : sans accès série, c'est le seul moyen de revenir au
 * firmware d'origine si ce firmware-ci ne parvient pas à joindre le réseau.
 *
 * Le principe est volontairement pauvre : un minuteur armé au démarrage, que
 * seule une connexion WiFi réussie désarme. Il ne dépend ni de l'écran, ni du
 * tactile, ni d'aucune bibliothèque tierce — donc il survit à leur défaillance.
 *
 * Le rappel d'esp_timer tourne sur une pile étroite qu'ESP-IDF documente comme
 * ne devant jamais bloquer. Or basculer de slot implique une vérification
 * SHA-256 de l'image cible (dans esp_ota_set_boot_partition), puis en dernier
 * recours une écriture flash, avant un esp_restart() qui appelle des
 * gestionnaires d'arrêt eux-mêmes bloquants (dont esp_wifi_stop). Tout ce
 * travail est donc délégué à une tâche dédiée, réveillée par
 * rescue_switch_now() — jamais exécuté sur la pile de l'appelant, qu'il
 * s'agisse du rappel du minuteur, de la tâche httpd (web.c) ou d'app_main.
 *
 * Un minuteur seul ne couvre que les pannes plus lentes que son échéance. Le
 * compteur de démarrages en mémoire RTC, plus bas, ferme presque tout le
 * reste : panique, chien de garde, débordement de pile — tout ce qui
 * redémarre l'appareil en moins de 90 secondes, À CONDITION que ce soit après
 * l'entrée dans app_main. Ce qu'il NE couvre PAS : un échec d'initialisation
 * de la PSRAM. esp_psram_chip_init() est appelée depuis cpu_start.c, avant
 * l'ordonnanceur et avant app_main — un échec y déclenche un abort() alors
 * qu'aucune ligne de ce fichier n'a encore tourné, et comme otadata désigne
 * toujours ce même slot, une coupure de courant ne change rien : la seule
 * parade est en amont, dans sdkconfig.defaults (CONFIG_SPIRAM_IGNORE_NOTFOUND
 * notamment). */

#include "rescue.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rescue";
static esp_timer_handle_t minuteur;
static TaskHandle_t tache_execution;

/* RTC_NOINIT_ATTR survit à un redémarrage logiciel comme à une panique, mais
 * pas à une coupure d'alimentation — exactement le comportement voulu : une
 * boucle de redémarrage est détectée, un appareil rallumé repart à zéro. */
RTC_NOINIT_ATTR static uint32_t compteur_demarrages;
RTC_NOINIT_ATTR static uint32_t temoin_validite;

#define TEMOIN_ATTENDU 0x4B544348u /* "KTCH" */

uint32_t rescue_count_boot(void)
{
    if (temoin_validite != TEMOIN_ATTENDU) {
        /* Premier démarrage après mise sous tension : la mémoire RTC contient
         * n'importe quoi, il faut l'initialiser avant de s'y fier. */
        temoin_validite = TEMOIN_ATTENDU;
        compteur_demarrages = 0;
    }
    compteur_demarrages++;
    return compteur_demarrages;
}

void rescue_reset_boot_count(void)
{
    compteur_demarrages = 0;
}

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

/* Dernier recours : effacer otadata.
 *
 * Si esp_ota_set_boot_partition() refuse la cible — image absente, à moitié
 * écrite, ou qui échoue la vérification — insister ne sert à rien. Mais une
 * otadata invalide n'est pas un blocage : le bootloader se rabat alors sur
 * `factory`, et faute de partition `factory` il démarre le premier slot OTA,
 * c'est-à-dire `app0`. Ce chemin est tolérant là où esp_ota_set_boot_partition
 * est intransigeant, puisque le bootloader essaie chaque slot à son tour. */
static esp_err_t effacer_otadata(void)
{
    const esp_partition_t *ota = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (ota == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "dernier recours : effacement d'otadata");
    return esp_partition_erase_range(ota, 0, ota->size);
}

/* Fait le travail réel, hors du contexte étroit de tout appelant. Ne rend
 * jamais la main : soit la bascule réussit et esp_restart() coupe tout, soit
 * elle échoue et l'effacement d'otadata en dernier recours en fait autant. */
static void tache_sur_echeance(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGE(TAG, "bascule demandee");
        if (rescue_switch_to_other_slot() != ESP_OK) {
            effacer_otadata();
        }
        esp_restart();
    }
}

/* Crée la tâche dédiée si elle n'existe pas encore. Appelée à la fois par
 * rescue_arm() (armement normal) et par rescue_switch_now() (chemin du
 * compteur de démarrages, appelé avant même rescue_arm()) : idempotente,
 * peu importe laquelle des deux s'exécute en premier. */
static void assurer_tache_dediee(void)
{
    if (tache_execution == NULL) {
        BaseType_t cree = xTaskCreate(tache_sur_echeance, "rescue_exec", 8192, NULL,
                                       tskIDLE_PRIORITY + 5, &tache_execution);
        if (cree != pdPASS) {
            tache_execution = NULL;
        }
    }
}

void rescue_switch_now(void)
{
    assurer_tache_dediee();
    if (tache_execution != NULL) {
        xTaskNotifyGive(tache_execution);
        return;
    }
    /* Dernier recours si même la tâche dédiée n'a pas pu être créée (mémoire
     * épuisée) : tenter la bascule ici, sur la pile de l'appelant. Pire que
     * la tâche dédiée si cette pile est étroite, mais mieux que ne rien
     * tenter — et un dépassement de pile ici resterait rattrapable par le
     * chien de garde (CONFIG_ESP_TASK_WDT_PANIC) ou par la détection de
     * dépassement de pile de FreeRTOS, tous deux configurés pour redémarrer
     * plutôt que geler. */
    ESP_LOGE(TAG, "tache de sauvetage indisponible, bascule en direct");
    if (rescue_switch_to_other_slot() != ESP_OK) {
        effacer_otadata();
    }
    esp_restart();
}

static void sur_echeance(void *arg)
{
    ESP_LOGE(TAG, "reseau injoignable dans le delai imparti");
    rescue_switch_now();
}

esp_err_t rescue_arm(uint32_t delai_ms)
{
    assurer_tache_dediee();

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
    /* Deux appelants CONCURRENTS existent depuis le sous-projet 7 : la tâche
     * `main` (app_main.c, une fois l'initialisation terminée) et la tâche
     * `sys_evt` de l'event loop par défaut (wifi.c, sur IP_EVENT_STA_GOT_IP).
     * Sur cet ESP32-S3 SMP elles peuvent s'exécuter réellement en parallèle
     * (deux cœurs) ou se préempter. Le check-then-act sur `minuteur` DOIT donc
     * être atomique : sans verrou, les deux pourraient lire `minuteur != NULL`
     * avant que l'une ne le remette à NULL, et appeler esp_timer_delete() DEUX
     * FOIS sur le même handle -- esp_timer diffère la libération en réinsérant
     * le timer dans sa liste interne, un second appel corromprait cette liste
     * ou déréférencerait une structure déjà libérée (use-after-free), juste
     * après les grosses allocations LVGL de la construction de l'interface :
     * un plantage tas ici recréerait précisément le multi-reboot que ce fix
     * élimine. On prend possession du handle sous section critique, puis on le
     * manipule HORS verrou (esp_timer_stop/_delete ne doivent pas tourner sous
     * portENTER_CRITICAL). */
    static portMUX_TYPE verrou = portMUX_INITIALIZER_UNLOCKED;
    esp_timer_handle_t local;
    portENTER_CRITICAL(&verrou);
    local = minuteur;
    minuteur = NULL;
    portEXIT_CRITICAL(&verrou);
    if (local != NULL) {
        esp_timer_stop(local);
        esp_timer_delete(local);
        /* Message volontairement neutre sur la CAUSE du désarmement : les deux
         * appelants (wifi.c « le réseau répond » ; app_main.c fin d'init, où le
         * réseau peut ne pas encore avoir répondu) journalisent déjà leur
         * propre contexte juste avant -- un doublon ici confirmerait une cause
         * pas toujours exacte. */
        ESP_LOGI(TAG, "sauvetage desarme");
    }
}
