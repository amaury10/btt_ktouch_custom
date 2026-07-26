/* Preuve de vie du jalon 1 : allumer le panneau, afficher une mire lisible et
 * confirmer que le tactile remonte des coordonnées cohérentes.
 *
 * Le pinout utilisé est celui du Panda Touch 7 pouces, fourni par le BSP. Toute
 * l'expérience consiste à savoir s'il convient tel quel à la K-Touch 5 pouces. */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pandatouch_display.h"

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

    lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSED, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "demarrage du firmware de preuve de vie");

    /* pt_display_init() enregistre déjà le périphérique d'entrée tactile en
     * interne : il appelle pt_lvgl_touch_init(pt_disp, 800, 480) lui-même
     * (voir pandatouch_display.c). Il n'y a donc pas d'appel explicite à
     * pt_lvgl_touch_init() ici — le brief d'origine en supposait un avant
     * l'ajout du sous-module, mais la vraie signature
     * (lv_indev_t *pt_lvgl_touch_init(lv_display_t*, int, int)) ne
     * correspond de toute façon pas à l'appel sans argument imaginé, et
     * l'appeler nous-mêmes créerait un second périphérique d'entrée. */
    ESP_ERROR_CHECK(pt_display_init());
    pt_backlight_set(80);

    PT_LVGL_SCOPE_LOCK() {
        build_test_pattern();
    }

    ESP_LOGI(TAG, "interface construite, le panneau doit etre allume");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "toujours vivant");
    }
}
