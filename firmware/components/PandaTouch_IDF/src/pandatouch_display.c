#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "sdkconfig.h"
#include "pandatouch_display.h"
#include "pandatouch_lvgl_touch.h"
#include "pandatouch_board.h"

#ifdef CONFIG_LV_USE_CUSTOM_MALLOC
#ifdef CONFIG_PT_LVGL_USE_PT_INTERNAL_MALLOC
void lv_mem_init(void)
{
}
/* Contrepartie de lv_mem_init : appelee par lv_deinit(), que esp_lvgl_port
 * reference (changelog 2.7.1). Notre allocateur delegue a heap_caps, sans etat
 * global a demonter -- donc vide. Son absence cassait le link (undefined
 * reference to lv_mem_deinit) des que lv_deinit() etait tire dans le binaire. */
void lv_mem_deinit(void)
{
}
void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}
#endif
#endif

/* ====================== Logging / Globals ====================== */
static const char *TAG = "PandaTouch::Display";
static esp_lcd_panel_handle_t pt_lcd_panel_handle = NULL;
static volatile uint32_t pt_backlight_setting = PT_BL_MAX;
static lv_display_t *pt_disp = NULL;

/* Rendu tear-free delegue a esp_lvgl_port (composant maintenu Espressif).
 *
 * Historique : le driver d'affichage etait fait main -- flush + attente VSYNC +
 * page-flip a la main. Aucune combinaison (PARTIAL/bounce, FULL_2/DIRECT,
 * RESTART_IN_VSYNC on/off, purge du semaphore) n'a supprime le tearing : le
 * basculement de framebuffer ne se latchait pas proprement au vblank sur ce
 * panneau. La cause etait architecturale, pas un reglage -- reimplementation
 * fragile de ce que fait esp_lvgl_port. On confie donc desormais flush, tick,
 * tache LVGL, verrou et synchro VSYNC au composant, en mode anti-tearing :
 * deux framebuffers pleins du panneau (num_fbs=2, sans bounce) utilises
 * directement par LVGL, page-flip au VSYNC gere par le composant. full_refresh
 * (RENDER_MODE_FULL) : chaque frame redessine tout l'ecran, donc les deux FB
 * restent toujours coherents -- ce qui evite le defaut de coherence du mode
 * DIRECT (le bas de l'image qui "sautait" au meme endroit). */

/* Vrai une fois lvgl_port_init() reussi. Protege pt_lvgl_lock() : si
 * pt_display_init() echoue avant l'init du port (ecran indisponible mais WiFi
 * et /revert vivants -- mode degrade voulu), lvgl_port_lock() assert()erait sur
 * un mutex NULL. Ce drapeau rend alors le verrou inoffensif. */
static bool pt_lvgl_ready = false;

/* ====================== LVGL mutex (delegue au port) ====================== */
void pt_lvgl_lock(void)
{
    if (pt_lvgl_ready)
        lvgl_port_lock(0); /* 0 = bloque indefiniment */
}

void pt_lvgl_unlock(void)
{
    if (pt_lvgl_ready)
        lvgl_port_unlock();
}

/* ====================== Backlight helpers ====================== */

static uint32_t pt_backlight_percent_to_duty(uint32_t percent)
{
    if (percent > PT_BL_MAX)
        percent = PT_BL_MAX;
    uint32_t maxd = (1u << PT_BL_LEDC_RESOLUTION) - 1u;
    return (uint32_t)((percent / 100.0f) * maxd);
}

static void pt_backlight_init(int duty_percent)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PT_BL_PIN) | (1ULL << PT_LCD_RESET_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io);
    gpio_set_level(PT_LCD_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PT_LCD_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ledc_timer_config_t tcfg = {
        .speed_mode = PT_BL_LEDC_SPEED_MODE,
        .duty_resolution = PT_BL_LEDC_RESOLUTION,
        .timer_num = PT_BL_LEDC_TIMER,
        .freq_hz = PT_BL_FREQUENCY_HZ,
        .clk_cfg = LEDC_USE_APB_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    ledc_channel_config_t ccfg = {
        .gpio_num = PT_BL_PIN,
        .speed_mode = PT_BL_LEDC_SPEED_MODE,
        .channel = PT_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PT_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
    ledc_fade_func_install(0);

    if (duty_percent < PT_BL_MIN)
        duty_percent = PT_BL_MIN;
    if (duty_percent > PT_BL_MAX)
        duty_percent = PT_BL_MAX;

    ESP_ERROR_CHECK(ledc_set_duty(PT_BL_LEDC_SPEED_MODE, PT_BL_LEDC_CHANNEL, pt_backlight_percent_to_duty(duty_percent)));
    ESP_ERROR_CHECK(ledc_update_duty(PT_BL_LEDC_SPEED_MODE, PT_BL_LEDC_CHANNEL));
}

bool pt_backlight_set(uint32_t percent)
{
    if (percent > PT_BL_MAX)
    {
        percent = PT_BL_MAX;
    }

    /* update in-memory setting; persistence is not handled here */
    pt_backlight_setting = percent;

    esp_err_t e = ledc_set_duty(PT_BL_LEDC_SPEED_MODE, PT_BL_LEDC_CHANNEL, pt_backlight_percent_to_duty(percent));
    if (e == ESP_OK)
    {
        e = ledc_update_duty(PT_BL_LEDC_SPEED_MODE, PT_BL_LEDC_CHANNEL);
    }

    return (e == ESP_OK) ? true : false;
}

uint32_t pt_backlight_get(void)
{
    return pt_backlight_setting;
}

/* ====================== Panel init ====================== */
static esp_err_t pt_lcd_panel_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = PT_LCD_PCLK_HZ,
            .h_res = PT_LCD_H_RES,
            .v_res = PT_LCD_V_RES,
            .hsync_pulse_width = PT_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = PT_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = PT_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = PT_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = PT_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = PT_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = true,
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = false},
        },
        .data_width = 16,
        /* Anti-tearing (esp_lvgl_port) : le panneau alloue DEUX framebuffers
         * pleins ; LVGL dessine directement dedans et le port fait le page-flip.
         * Bounce buffer (10 lignes, RAM interne) : la parade anti-underrun
         * d'ESP-IDF (celle qu'utilise BTT). Un 1er essai avait donne des lignes
         * a gauche -- mais c'etait avec les porches divergents (blanking trop
         * court, le bounce n'avait pas le temps de se remplir). Avec les timings
         * officiels BTT (blanking double) il doit tenir. Combine a bb_mode=true
         * cote esp_lvgl_port (synchro sur on_bounce_frame_finish). But : tuer le
         * drift horizontal periodique (underrun sur pic reseau ~5s). */
        .num_fbs = 2,
        .bounce_buffer_size_px = 10 * PT_LCD_H_RES,
        .psram_trans_align = 64,
        .hsync_gpio_num = PT_LCD_HSYNC_PIN,
        .vsync_gpio_num = PT_LCD_VSYNC_PIN,
        .de_gpio_num = PT_LCD_DE_PIN,
        .pclk_gpio_num = PT_LCD_PCLK_PIN,
        .disp_gpio_num = -1,
        .data_gpio_nums = {PT_LCD_DATA0_PIN, PT_LCD_DATA1_PIN, PT_LCD_DATA2_PIN, PT_LCD_DATA3_PIN, PT_LCD_DATA4_PIN, PT_LCD_DATA5_PIN, PT_LCD_DATA6_PIN, PT_LCD_DATA7_PIN, PT_LCD_DATA8_PIN, PT_LCD_DATA9_PIN, PT_LCD_DATA10_PIN, PT_LCD_DATA11_PIN, PT_LCD_DATA12_PIN, PT_LCD_DATA13_PIN, PT_LCD_DATA14_PIN, PT_LCD_DATA15_PIN},
        .flags = {.fb_in_psram = true},
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &pt_lcd_panel_handle), TAG, "esp_lcd_new_rgb_panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(pt_lcd_panel_handle), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(pt_lcd_panel_handle), TAG, "panel_init");
    return ESP_OK;
}

/* ====================== Public API ====================== */

esp_err_t pt_display_init(void)
{
    /* Backlight early for smooth fade-in */
    pt_backlight_init(5);
    pt_backlight_set(100);

    /* Step 1: LCD panel (num_fbs=2, sans bounce) */
    ESP_RETURN_ON_ERROR(pt_lcd_panel_init(), TAG, "panel_init");

    /* Step 2: esp_lvgl_port (lv_init + tick + tache + verrou).
     * task_stack_caps en RAM INTERNE : la tache LVGL execute le bouton "Save"
     * qui ecrit en NVS (nvs_commit -> flash). Une ecriture flash coupe le cache
     * de PSRAM ; une pile en PSRAM deviendrait inaccessible en plein commit ->
     * crash. MALLOC_CAP_INTERNAL garantit une pile toujours accessible (parade
     * historique, preservee ici via le champ dedie du port). */
    const lvgl_port_cfg_t port_cfg = {
        .task_priority = 5,
        .task_stack = CONFIG_PT_LVGL_TASK_STACK_SIZE * 1024,
        .task_affinity = 1,
        .task_max_sleep_ms = 500,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        .timer_period_ms = 2,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init");
    pt_lvgl_ready = true;

    /* Step 3: display RGB anti-tearing.
     * avoid_tearing : le port recupere lui-meme les 2 framebuffers du panneau
     * et les donne a LVGL (il ignore double_buffer et force buffer_size).
     * direct_mode : RENDER_MODE_DIRECT -> LVGL ne redessine que les ZONES
     * modifiees (quelques Ko/frame) au lieu de tout l'ecran a chaque frame
     * (full_refresh, ~46 Mo/s d'ecriture PSRAM). full_refresh saturait la bande
     * passante PSRAM partagee avec le balayage RGB -> le pixel clock etait
     * affame de donnees -> cisaillement horizontal ondule. C'est aussi le mode
     * de l'exemple RGB officiel esp-idf ; esp_lvgl_port maintient la coherence
     * des deux framebuffers (le defaut du DIRECT fait main est ici gere). */
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = pt_lcd_panel_handle,
        .buffer_size = (uint32_t)PT_LCD_H_RES * PT_LCD_V_RES,
        .double_buffer = true,
        .hres = PT_LCD_H_RES,
        .vres = PT_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = false,
            .swap_bytes = false,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    /* Le port prend le verrou lui-meme ; pas de PT_LVGL_SCOPE_LOCK ici. */
    pt_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!pt_disp)
    {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb a echoue");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Display: esp_lvgl_port RGB, num_fbs=2, avoid_tearing + full_refresh");

    /* Step 4: tactile (indev LVGL standard, pollé par la tache du port) */
    lv_indev_t *indev = pt_lvgl_touch_init(pt_disp, 800, 480);
    (void)indev;

    return ESP_OK;
}

void pt_display_schedule_ui(pt_ui_fn_t fn, void *arg)
{
    if (!fn)
        return;
    /* Run on LVGL thread */
    lv_async_call(fn, arg);
}

lv_display_t *pt_get_display(void) { return pt_disp; }
esp_lcd_panel_handle_t pt_get_panel(void) { return pt_lcd_panel_handle; }
