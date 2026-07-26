/* Preuve de vie du jalon 1 : allumer le panneau, afficher une mire lisible et
 * confirmer que le tactile remonte des coordonnées cohérentes.
 *
 * Le pinout utilisé est celui du Panda Touch 7 pouces, fourni par le BSP. Toute
 * l'expérience consiste à savoir s'il convient tel quel à la K-Touch 5 pouces.
 *
 * L'appareil n'est atteignable qu'en WiFi (le port USB-C ne sert qu'à
 * l'alimentation ici) : ce firmware doit donc porter son propre chemin de
 * retour. L'ordre de démarrage ci-dessous n'est pas arbitraire — voir le
 * commentaire au-dessus de app_main(). */

#include <inttypes.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "pandatouch_display.h"

#include "netlog.h"
#include "rescue.h"
#include "web.h"
#include "wifi.h"

static const char *TAG = "preuve_de_vie";

static void on_touch(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    ESP_LOGI(TAG, "appui a x=%d y=%d", (int)point.x, (int)point.y);
}

static void build_test_pattern(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);

    /* Bandes primaires : un canal de couleur inversé ou une broche de données
     * flottante se voit immédiatement. */
    static const uint32_t colours[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(screen);
        lv_obj_set_size(bar, 200, 80);
        lv_obj_set_pos(bar, i * 200, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colours[i]), LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    }

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "K-Touch custom\nslot app1 — preuve de vie");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    /* Repères de coin : valident que les 800x480 sont bien balayés en entier. */
    static const lv_align_t corners[] = {
        LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
        LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT,
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *marker = lv_obj_create(screen);
        lv_obj_set_size(marker, 24, 24);
        lv_obj_align(marker, corners[i], 0, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        lv_obj_set_style_border_width(marker, 0, LV_PART_MAIN);
    }
}

void app_main(void)
{
    /* Tout au début, avant même le sauvetage : le compteur de démarrages
     * couvre les pannes trop rapides pour que le minuteur les voie (panique,
     * chien de garde, débordement de pile), à condition qu'elles surviennent
     * après cette ligne. Il NE couvre PAS un échec d'initialisation de la
     * PSRAM : esp_psram_chip_init() tourne depuis cpu_start.c, avant
     * l'ordonnanceur et avant app_main — la seule parade à cette classe de
     * pannes est dans sdkconfig.defaults (CONFIG_SPIRAM_IGNORE_NOTFOUND), pas
     * ici. Le compteur survit aux redémarrages en mémoire RTC. Au-delà de
     * RESCUE_DEMARRAGES_MAX tentatives consécutives, on ne tente plus rien :
     * bascule immédiate, sans même armer le minuteur. */
    uint32_t compteur_demarrages = rescue_count_boot();
    if (compteur_demarrages > RESCUE_DEMARRAGES_MAX) {
        /* La remise à zéro n'est pas une coquetterie : esp_restart() est une
         * réinitialisation logicielle, la mémoire RTC survit donc jusque
         * dans le firmware d'origine, qui n'y touche jamais. Sans elle, le
         * compteur resterait à compteur_demarrages ; au prochain flash de ce
         * firmware par-dessus, il repartirait déjà au-delà du seuil et
         * basculerait sans jamais avoir tenté de démarrer, cassant la boucle
         * d'itération décrite dans flashing.md jusqu'à une coupure secteur. */
        rescue_reset_boot_count();
        rescue_switch_now();
        /* rescue_switch_now() délègue à une tâche dédiée et ne bloque pas :
         * on attend ici, sans rien tenter d'autre, qu'elle redémarre
         * l'appareil. */
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* Le compte à rebours est armé ensuite, avant l'écran, avant le WiFi,
     * avant quoi que ce soit d'autre. Une panne à n'importe quel étage
     * ultérieur (NVS, WiFi, écran, tactile, serveur HTTP) doit rester
     * rattrapable ; un sauvetage armé après coup ne protégerait pas contre
     * l'étage qui a justement échoué. rescue_disarm() n'est appelé que
     * depuis le gestionnaire de IP_EVENT_STA_GOT_IP, dans wifi.c. */
    ESP_ERROR_CHECK(rescue_arm(CONFIG_KTOUCH_RESCUE_TIMEOUT_MS));

    /* Le journal réseau doit être en place avant même les deux lignes de log
     * qui suivent : sans port série, /log est le seul endroit où les relire
     * après coup, et ce sont les toutes premières choses à y vérifier. */
    esp_err_t erreur = netlog_init();
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "netlog_init a echoue : %s", esp_err_to_name(erreur));
    }

    ESP_LOGW(TAG, "demarrage numero %" PRIu32 " (limite avant bascule forcee : %d)",
             compteur_demarrages, RESCUE_DEMARRAGES_MAX);

    /* Première chose à vérifier dans le journal : ce firmware doit tourner
     * depuis app1, jamais depuis app0 qui n'est jamais réécrit. */
    const esp_partition_t *partition_courante = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "partition d'execution : %s (offset 0x%06" PRIx32 ")",
             partition_courante != NULL ? partition_courante->label : "?",
             partition_courante != NULL ? (uint32_t)partition_courante->address : 0);

    /* La NVS est requise par le WiFi (stockage des paramètres PHY/calibration
     * et, selon la configuration, des informations de connexion). Une NVS
     * corrompue au point que l'effacement ne la répare pas signale un
     * problème matériel plus large : ESP_ERROR_CHECK est justifié ici,
     * contrairement aux étages suivants qui doivent tous rester dégradables
     * sans faire échouer le démarrage. */
    esp_err_t erreur_nvs = nvs_flash_init();
    if (erreur_nvs == ESP_ERR_NVS_NO_FREE_PAGES || erreur_nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        erreur_nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(erreur_nvs);

    /* Un échec ici n'est volontairement pas fatal : c'est justement le cas
     * que le sauvetage automatique couvre. Si le WiFi ne se connecte
     * jamais, rescue_disarm() n'est jamais appelé et le minuteur armé plus
     * haut rebasculera sur l'autre slot à l'échéance. */
    erreur = wifi_start();
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start a echoue : %s ; le sauvetage automatique reste actif", esp_err_to_name(erreur));
    }

    web_set_boot_count(compteur_demarrages);

    /* Le serveur HTTP démarre ici, juste après le WiFi et AVANT l'écran :
     * c'est ce qui garantit que /revert et /log restent joignables même si
     * l'étage suivant (écran) échoue ou abat le processus. Sans cet ordre,
     * une panne d'affichage emporterait à la fois le sauvetage automatique
     * (déjà désarmé par une connexion WiFi réussie) et la route manuelle
     * /revert — les deux voies de secours disparaîtraient d'un coup, sur la
     * panne la plus probable de ce jalon. */
    erreur = web_start();
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "web_start a echoue : %s", esp_err_to_name(erreur));
    }

    ESP_LOGI(TAG, "demarrage du firmware de preuve de vie");

    /* Aucune défaillance locale n'est fatale à partir d'ici : ni l'écran, ni
     * le rétroéclairage, ni le tactile ne sont vérifiés par ESP_ERROR_CHECK
     * — on journalise et on continue. Un écran mort doit laisser tourner le
     * WiFi et le serveur, précisément ce qui permet de diagnostiquer à
     * distance au lieu de constater un appareil muet. pt_backlight_set()
     * mérite une attention particulière : le BSP de BTT contient ses propres
     * ESP_ERROR_CHECK sur les appels LEDC, donc un échec à l'intérieur peut
     * abattre le processus sans que notre code y soit pour quoi que ce soit
     * — raison de plus pour que WiFi et le serveur HTTP tournent déjà. */
    esp_err_t erreur_affichage = pt_display_init();
    if (erreur_affichage != ESP_OK) {
        ESP_LOGE(TAG, "pt_display_init a echoue : %s", esp_err_to_name(erreur_affichage));
        ESP_LOGE(TAG, "ecran indisponible, mais WiFi/serveur HTTP restent actifs pour /revert et /log");
    } else {
        if (!pt_backlight_set(80)) {
            ESP_LOGW(TAG, "pt_backlight_set a echoue, l'ecran peut rester sombre");
        }

        PT_LVGL_SCOPE_LOCK() {
            build_test_pattern();

            /* Le rappel est enregistré sur le périphérique d'entrée tactile
             * lui-même, et non sur un widget précis. lv_obj_create() donne par
             * défaut LV_OBJ_FLAG_CLICKABLE à chaque objet créé, y compris les
             * huit objets décoratifs de la mire (barres de couleur, repères de
             * coin) : lv_indev_search_obj() les désigne donc comme cible de
             * l'appui à la place de l'écran, et l'événement ne remonterait au
             * parent que si l'enfant portait LV_OBJ_FLAG_EVENT_BUBBLE — ce
             * qu'aucun d'eux ne fait. Un rappel posé sur l'écran (comme avant)
             * ne recevait donc rien pour un appui sur une barre ou un repère de
             * coin. lv_indev_send_event() envoie LV_EVENT_PRESSED aux rappels du
             * périphérique AVANT de le distribuer à l'objet ciblé (voir
             * send_event() dans lv_indev.c des sources LVGL vendues ici), quel
             * que soit l'objet touché ou ses drapeaux : s'abonner ici capture
             * donc chaque appui, y compris sur les repères de coin qui
             * garantissent que les 800x480 sont bien balayés jusqu'aux bords. */
            /* pt_display_init() avale une éventuelle défaillance tactile : si
             * pt_lvgl_touch_init() échoue en interne (GT911 muet), la fonction
             * ignore la valeur de retour (voir pandatouch_display.c) et rend
             * quand même ESP_OK — ESP_ERROR_CHECK ne se déclenche donc jamais
             * dans ce cas. lv_indev_get_next(NULL) renvoie alors NULL, et un
             * NULL ici signale une puce tactile silencieuse, pas une erreur de
             * programmation : il ne faut pas retirer ce test. Sans lui,
             * lv_indev_add_event_cb() ferait échouer LV_ASSERT_NULL, qui boucle
             * indéfiniment (LV_USE_ASSERT_NULL=y par défaut) — en tenant le
             * verrou LVGL, donc sans jamais afficher la mire déjà construite.
             * Un pinout tactile faux ou une puce GT911 non répondante étant
             * justement l'hypothèse la plus probable de ce jalon, on dégrade
             * ici plutôt que de tout bloquer : la mire reste visible et
             * seul le retour tactile est absent, ce qui distingue clairement
             * « l'écran marche, pas le tactile » de « rien ne marche ». */
            lv_indev_t *touch_indev = lv_indev_get_next(NULL);
            if (touch_indev != NULL) {
                lv_indev_add_event_cb(touch_indev, on_touch, LV_EVENT_PRESSED, NULL);
            } else {
                ESP_LOGW(TAG, "aucun peripherique tactile enregistre : le GT911 n'a pas repondu");
                ESP_LOGW(TAG, "la mire reste affichee, seul le retour tactile est indisponible");
            }
            web_set_touch_available(touch_indev != NULL);
        }

        ESP_LOGI(TAG, "interface construite, le panneau doit etre allume");
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "toujours vivant");
    }
}
