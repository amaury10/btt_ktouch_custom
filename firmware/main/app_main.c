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
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "pandatouch_display.h"
#include "pandatouch_msc.h"

#include "accueil_choix.h"
#include "backend.h"
#include "backend_factice.h"
#include "backend_moonraker.h"
#include "boucle.h"
#include "ecran_accueil.h"
#include "ecran_accueil_hub.h"
#include "ecran_configuration.h"
#include "ecran_macros.h"
#include "habillage.h"
#include "journal.h"
#include "klipper_temp_historique.h"
#include "navigation.h"
#include "netlog.h"
#include "rail_actions.h"
#include "reglages.h"
#include "rescue.h"
#include "usb_fichiers.h"
#include "usb_upload.h" /* usb_est_gcode() -- tache A de "Impression depuis USB" */
#include "web.h"
#include "wifi.h"

static const char *TAG = "preuve_de_vie";

/* État affiché en continu, seul canal de diagnostic qui survive à une panne
 * WiFi sans câble série (voir le commentaire en tête de wifi.c : c'est
 * exactement ce qui manquait pour diagnostiquer le premier vol matériel).
 * Renseignés une fois, tout au début d'app_main, avant que build_test_pattern()
 * ne crée le label qui les affiche. */
static char partition_label_globale[17] = "?";
static uint32_t compteur_demarrages_global;

/* NULL tant que l'écran n'a pas été construit (pt_display_init() en échec,
 * ou avant que build_test_pattern() ne s'exécute) : rafraichir_etat_ecran()
 * s'abstient dans ce cas plutôt que de déréférencer un pointeur nul. */
static lv_obj_t *label_etat;

static void on_touch(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    ESP_LOGI(TAG, "appui a x=%d y=%d", (int)point.x, (int)point.y);
}

/* ------------------------------------------------------------------------
 * Feature "Impression depuis USB", tache B (integration ESP) : montage MSC
 * (BSP pandatouch_msc.h) + scan recursif au montage, qui remplit le store
 * dedie usb_fichiers.h (voir son commentaire de tete pour le POURQUOI hors
 * de etat_klipper_t -- meme raison que klipper_fichiers.h). Le reste de la
 * chaine (upload HTTP streame vers Moonraker, ecran) vit dans
 * usb_upload_http.{h,c}/ecran_usb.{h,c}, pas ici.
 * ------------------------------------------------------------------------ */

/* Pile large (8 Kio, meme ordre de grandeur que les taches dediees d'ota.c) :
 * cette tache porte la recursion de usb_scan_recursif() (un cadre ~USB_FICHIER_CHEMIN_MAX
 * octets par niveau de sous-dossier) -- s_usb_scan_tampon, lui, est STATIQUE
 * (BSS, jamais sur cette pile). Priorite identique aux autres taches dediees
 * de ce firmware (ota.c) : nettement au-dessus d'IDLE, sans rivaliser avec
 * les taches internes du pilote WiFi/USB. */
#define USB_SCAN_TASK_STACK 8192
#define USB_SCAN_TASK_PRIO  (tskIDLE_PRIORITY + 5)

/* Tampon de scan STATIQUE (jamais sur une pile, voir usb_scan_tache()) --
 * rempli par usb_scan_recursif() puis recopie EN UNE FOIS dans le store
 * verrouille (usb_fichiers_definir()) une fois le scan complet termine.
 * Partage entre scans successifs (un seul a la fois, voir s_usb_scan_en_cours
 * ci-dessous) : pas de risque de lecture partielle par le store, qui ne voit
 * jamais ce tampon directement. */
static usb_fichier_t s_usb_scan_tampon[USB_FICHIERS_MAX];

/* Garde contre un second scan concurrent (remontage rapide, ou callback de
 * montage rappelee alors qu'un scan precedent tourne encore) -- lu/ecrit
 * uniquement depuis usb_on_mount_cb() (tache "msc_inst_w" du BSP) et
 * usb_scan_tache() (sa propre tache dediee) : un seul ecrivain a la fois de
 * chaque cote, une simple variable volatile suffit (pas de portMUX -- meme
 * esprit que s_mounted dans pandatouch_msc.c, un flag booleen a ecriture
 * quasi-atomique sur cette architecture). */
static volatile bool s_usb_scan_en_cours = false;

/* Vrai si `nom` contient un octet de controle 0x00-0x1F -- prudence
 * demandee par la tache B, au-dela du seul filtre d'extension deja fait par
 * usb_est_gcode() (usb_upload.h, tache A) : usb_upload_preambule() nettoie
 * deja '"'/CR/LF d'un nom de fichier avant de l'inserer dans un en-tete HTTP,
 * mais un octet de controle "exotique" (tabulation, etc.) resterait possible
 * sur une clé FAT mal formee -- ce filtre-ci l'ecarte plus tot, avant meme
 * d'entrer dans le store. */
static bool usb_nom_a_octet_controle(const char *nom)
{
    if (nom == NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)nom; *p != '\0'; p++) {
        if (*p <= 0x1F) {
            return true;
        }
    }
    return false;
}

/* Scanne recursivement `chemin` (sous /usb) et accumule dans
 * s_usb_scan_tampon les .gcode trouves -- filtre extension (usb_est_gcode(),
 * tache A) + noms sans octet de controle (ci-dessus). Copie de
 * scan_dir_recursive() (PandaTouch_IDF/examples/display_slideshow.c, meme
 * usage de pt_usb_list_dir()/pt_usb_dir_list_free()), adaptee au filtre
 * gcode et a la taille des fichiers (pt_usb_dir_entry_t.size, propagee dans
 * le store -- voir usb_fichiers.h). `nb`/`tronques` sont des accumulateurs
 * partages entre tous les niveaux de recursion (passes par pointeur). */
static void usb_scan_recursif(const char *chemin, uint8_t *nb, bool *tronques)
{
    int erreur = 0;
    pt_usb_dir_list_t *liste = pt_usb_list_dir(chemin, &erreur);
    if (liste == NULL) {
        if (erreur != 0) {
            JOURNAL_ALERTE(TAG, "pt_usb_list_dir(%s) a echoue (%d)", chemin ? chemin : "?", erreur);
        }
        return;
    }

    for (size_t i = 0; i < liste->count; i++) {
        pt_usb_dir_entry_t *e = &liste->entries[i];
        if (e->is_hidden) {
            continue;
        }

        const char *chemin_entree = (e->path != NULL && e->path[0] != '\0') ? e->path : NULL;
        char tampon_local[USB_FICHIER_CHEMIN_MAX];
        if (chemin_entree == NULL) {
            if (chemin != NULL && chemin[0] != '\0' && chemin[strlen(chemin) - 1] == '/') {
                snprintf(tampon_local, sizeof(tampon_local), "%s%s", chemin, e->name ? e->name : "");
            } else {
                snprintf(tampon_local, sizeof(tampon_local), "%s/%s", chemin ? chemin : "", e->name ? e->name : "");
            }
            chemin_entree = tampon_local;
        }

        if (e->is_dir) {
            usb_scan_recursif(chemin_entree, nb, tronques);
            continue;
        }

        if (usb_nom_a_octet_controle(e->name) || !usb_est_gcode(e->name)) {
            continue;
        }

        if (*nb >= USB_FICHIERS_MAX) {
            *tronques = true;
            continue;
        }

        usb_fichier_t *dest = &s_usb_scan_tampon[*nb];
        size_t longueur = strlen(chemin_entree);
        if (longueur >= sizeof(dest->chemin)) {
            longueur = sizeof(dest->chemin) - 1;
        }
        memcpy(dest->chemin, chemin_entree, longueur);
        dest->chemin[longueur] = '\0';
        dest->taille = e->size;
        (*nb)++;
    }

    pt_usb_dir_list_free(liste);
}

static void usb_scan_tache(void *arg)
{
    (void)arg;
    uint8_t nb = 0;
    bool tronques = false;
    usb_scan_recursif(PT_USB_MOUNT_PATH, &nb, &tronques);
    /* La cle a pu etre ejectee PENDANT le scan (cette tache dediee tourne des
       centaines de ms) : usb_on_unmount_cb() a alors deja publie monte=false.
       Ne pas ecraser cet etat par un "true" en dur -- relire l'etat reel du
       montage. Si demontee, publier monte=false + liste vide plutot qu'une
       liste perimee etiquetee "cle presente" (scenario ejection a chaud). */
    bool encore_monte = pt_usb_is_mounted();
    usb_fichiers_definir(encore_monte, encore_monte ? s_usb_scan_tampon : NULL,
                         encore_monte ? nb : 0, encore_monte ? tronques : false);
    JOURNAL_INFO(TAG, "scan USB : %u fichier(s) .gcode trouve(s)%s", (unsigned)nb,
                 tronques ? " (liste tronquee)" : "");
    s_usb_scan_en_cours = false;
    vTaskDelete(NULL);
}

/* Callback pt_usb_on_mount() -- tourne sur "msc_inst_w", une tache du BSP a
 * SEULEMENT 4096 o de pile (voir pandatouch_msc.c, pt_usb_install_device_task()) :
 * jamais de scan direct ici (la recursion + les tampons locaux de
 * usb_scan_recursif() y depasseraient vite ce budget -- la spec de cette
 * tache demande explicitement de ne pas gonfler les piles USB du BSP). Une
 * tache DEDIEE (8 Kio, voir USB_SCAN_TASK_STACK) fait le travail reel ; ce
 * callback ne fait que la creer et revenir immediatement. */
static void usb_on_mount_cb(void)
{
    if (s_usb_scan_en_cours) {
        JOURNAL_ALERTE(TAG, "scan USB deja en cours, montage ignore");
        return;
    }
    s_usb_scan_en_cours = true;
    BaseType_t cree = xTaskCreate(usb_scan_tache, "usb_scan", USB_SCAN_TASK_STACK, NULL, USB_SCAN_TASK_PRIO, NULL);
    if (cree != pdPASS) {
        s_usb_scan_en_cours = false;
        JOURNAL_ERREUR(TAG, "creation de la tache de scan USB echouee (memoire epuisee)");
    }
}

static void usb_on_unmount_cb(void)
{
    usb_fichiers_definir(false, NULL, 0, false);
    JOURNAL_ALERTE(TAG, "cle USB retiree, store vide");
}

/* Rappel du minuteur récurrent (tâche 10) : par construction, un lv_timer
 * s'exécute déjà sur le fil LVGL — pt_lvgl_task (pandatouch_display.c)
 * est le seul appelant de lv_timer_handler(), qui dispatche tous les
 * rappels de minuteurs, et il l'exécute DÉJÀ sous PT_LVGL_SCOPE_LOCK().
 * C'est la vraie raison de ne pas reprendre le verrou ici : ce fil le
 * détient déjà pendant tout le rappel. (Le reprendre serait d'ailleurs
 * inoffensif, pas un interblocage : pt_lvgl_mutex est un
 * xSemaphoreCreateRecursiveMutex, voir pandatouch_display.c:57 — précision
 * de la revue tâche 10, la première version de ce commentaire supposait
 * l'inverse sans avoir lu le .c.) habillage_pomper() est un no-op si
 * habillage_construire() n'a pas encore tourné (voir habillage.c), donc rien
 * à garder ici en plus de l'appel. Ni réseau ni NVS : uniquement le
 * rafraîchissement visuel (bandeau de notification qui expire, barre d'état)
 * que la boucle de sondage à 1 Hz ne couvre pas assez vite. */
static void interface_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    habillage_pomper();
}

/* Echantillonneur d'historique de temperature : toutes les 5 s, copie l'etat
 * courant (meme accesseur verrouille que habillage_pomper) et pousse un point.
 * Tourne en continu sur le fil LVGL, independamment de l'ecran affiche, pour
 * que la courbe se remplisse meme hors accueil. */
static void echantillon_temp_cb(lv_timer_t *t)
{
    (void)t;
    etat_klipper_t e;                 /* ~1840 o sur la pile du fil LVGL, meme
                                         budget que la copie de habillage_pomper */
    if (boucle_etat_copier(&e, sizeof(e))) {
        klipper_temp_historique_pousser(&e);
    }
}

/* Reconstruit le texte (deux lignes) de la ligne d'état à partir de l'état
 * WiFi courant. Appelée sous PT_LVGL_SCOPE_LOCK() par l'appelant —
 * lv_label_set_text() n'est pas thread-safe vis-à-vis de la tâche LVGL.
 *
 * ASCII pur exigé de bout en bout (SSID compris, via strncpy sur des
 * identifiants qui ne devraient déjà contenir que de l'ASCII) : la police
 * Montserrat compilée par défaut dans LVGL ne couvre que 0x20-0x7F, un
 * caractère hors de cette plage s'affiche en carré vide (voir
 * build_test_pattern()). */
static void rafraichir_etat_ecran(void)
{
    if (label_etat == NULL) {
        return;
    }

    /* Première ligne : partition, compteur de démarrages, source des
     * identifiants (cfg/nvs/aucun) et SSID employé — la lecture qui indique
     * d'un coup d'œil laquelle des deux configurations a été essayée. */
    char ssid[33];
    const char *source = wifi_credential_source(ssid, sizeof(ssid));
    char ligne1[96];
    if (ssid[0] != '\0') {
        snprintf(ligne1, sizeof(ligne1), "%s | boot %" PRIu32 " | %s:%s",
                 partition_label_globale, compteur_demarrages_global, source, ssid);
    } else {
        snprintf(ligne1, sizeof(ligne1), "%s | boot %" PRIu32 " | sans ssid",
                 partition_label_globale, compteur_demarrages_global);
    }

    /* Seconde ligne : IP une fois connecté ; sinon la raison de la dernière
     * déconnexion (le diagnostic qui compte le plus, voir wifi.c) ou, à
     * défaut, l'erreur synchrone d'esp_wifi_connect() ; toujours accompagnée
     * du nombre d'essais pour distinguer une tentative isolée d'un blocage
     * persistant. */
    char ip[16];
    bool connectee = wifi_ip_string(ip, sizeof(ip));
    char raison[40];
    char erreur_wifi[32];
    char ligne2[96];

    if (connectee) {
        snprintf(ligne2, sizeof(ligne2), "wifi: %s", ip);
    } else if (wifi_last_disconnect_reason(raison, sizeof(raison))) {
        snprintf(ligne2, sizeof(ligne2), "wifi: %s | essais %" PRIu32, raison, wifi_connect_attempts());
    } else if (wifi_last_connect_error(erreur_wifi, sizeof(erreur_wifi))) {
        snprintf(ligne2, sizeof(ligne2), "wifi: %s | essais %" PRIu32, erreur_wifi, wifi_connect_attempts());
    } else {
        snprintf(ligne2, sizeof(ligne2), "wifi: connexion... | essais %" PRIu32, wifi_connect_attempts());
    }

    char texte[192];
    snprintf(texte, sizeof(texte), "%s\n%s", ligne1, ligne2);
    lv_label_set_text(label_etat, texte);
}

/* Chooser de l'écran de fond injecté dans l'habillage (bascule vivante
 * repos<->impression, sous-projet 5 tâche 2) : hub au repos, accueil
 * impression pendant une impression -- accueil_impression_actif() est le MÊME
 * helper pur (accueil_choix.h) qu'utilise le simulateur, appliqué ici à
 * l'état réel de la boucle applicative. L'état arrive opaque (`const void *`)
 * de l'habillage générique : ce fichier (couche application) le recaste vers
 * etat_klipper_t, le seul site qui connaît ce type dans la chaîne de bascule.
 * `ctx` inutilisé (le choix ne dépend que de l'état). */
static const ecran_desc_t *choix_accueil_klipper(const void *etat, void *ctx)
{
    (void)ctx;
    return accueil_impression_actif((const etat_klipper_t *)etat) ? &ECRAN_ACCUEIL
                                                                  : &ECRAN_ACCUEIL_HUB;
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
    /* Texte volontairement en ASCII pur : la police Montserrat compilee par
     * defaut dans LVGL ne couvre que 0x20-0x7F, donc un tiret cadratin ou une
     * lettre accentuee s'affiche en carre vide. L'ecran etant, faute de port
     * serie, le seul canal de diagnostic qui survive a une panne de WiFi, sa
     * lisibilite prime sur la typographie. */
    lv_label_set_text(label, "K-Touch custom\nslot app1 - preuve de vie");
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

    /* Ligne d'état, ajoutée après le premier vol matériel : la mire (bandes,
     * repères de coin) reste inchangée ci-dessus, ce sont désormais des
     * points de repère confirmés bons, à comparer d'un vol à l'autre. Placée
     * en bas de l'écran, loin des repères de coin (24x24 dans chaque angle)
     * et des bandes (haut de l'écran) : aucun chevauchement. Sans câble
     * série, c'est le seul canal de diagnostic qui survive à une panne
     * WiFi — d'où son importance : voir le commentaire en tête de wifi.c. */
    label_etat = lv_label_create(screen);
    lv_obj_set_style_text_color(label_etat, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label_etat, LV_ALIGN_BOTTOM_MID, 0, -8);
    rafraichir_etat_ecran();
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

    /* Le journal réseau doit être en place avant même le sauvetage : sans
     * port série, /log est le seul endroit où relire après coup la ligne
     * "sauvetage arme : ... ms" — sans doute la plus utile de tout le
     * démarrage — et les deux lignes de log qui suivent. Le compteur de
     * démarrages ci-dessus reste la seule protection si netlog_init()
     * échouait lui-même avant que le sauvetage ne soit armé. */
    esp_err_t erreur = netlog_init();
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "netlog_init a echoue : %s", esp_err_to_name(erreur));
    }

    /* Le compte à rebours est armé ensuite, avant l'écran, avant le WiFi,
     * avant quoi que ce soit d'autre. Une panne à n'importe quel étage
     * ultérieur (NVS, WiFi, écran, tactile, serveur HTTP) doit rester
     * rattrapable ; un sauvetage armé après coup ne protégerait pas contre
     * l'étage qui a justement échoué.
     *
     * Contrat (revu ici même, voir le désarmement plus bas juste avant la
     * boucle principale) : le sauvetage couvre les étages d'INITIALISATION,
     * pas l'attente normale d'un réseau qui n'est pas encore monté. Il reste
     * armé jusqu'à ce que ce firmware ait prouvé qu'il est sain -- soit
     * parce que tous les étages risqués ci-dessous (NVS, WiFi, serveur HTTP,
     * réglages/backend/boucle, écran/habillage) ont réussi et que la boucle
     * principale s'apprête à tourner (voir rescue_disarm() plus bas), soit,
     * plus tôt, parce que IP_EVENT_STA_GOT_IP a été reçu (wifi.c). Le premier
     * des deux qui survient désarme ; les deux appellent aussi
     * rescue_reset_boot_count(). */
    ESP_ERROR_CHECK(rescue_arm(CONFIG_KTOUCH_RESCUE_TIMEOUT_MS));

    ESP_LOGW(TAG, "demarrage numero %" PRIu32 " (limite avant bascule forcee : %d)",
             compteur_demarrages, RESCUE_DEMARRAGES_MAX);

    /* Première chose à vérifier dans le journal : ce firmware doit tourner
     * depuis app1, jamais depuis app0 qui n'est jamais réécrit. */
    const esp_partition_t *partition_courante = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "partition d'execution : %s (offset 0x%06" PRIx32 ")",
             partition_courante != NULL ? partition_courante->label : "?",
             partition_courante != NULL ? (uint32_t)partition_courante->address : 0);

    /* Renseignés une fois pour la ligne d'état affichée à l'écran (voir
     * build_test_pattern()/rafraichir_etat_ecran()) : c'est le seul canal de
     * diagnostic qui survive à une panne WiFi sans câble série. */
    strlcpy(partition_label_globale, partition_courante != NULL ? partition_courante->label : "?",
            sizeof(partition_label_globale));
    compteur_demarrages_global = compteur_demarrages;

    /* La NVS est requise par le WiFi (stockage des paramètres PHY/calibration
     * et, selon la configuration, des informations de connexion). Le
     * réflexe habituel d'ESP-IDF sur ESP_ERR_NVS_NO_FREE_PAGES/
     * ESP_ERR_NVS_NEW_VERSION_FOUND est d'appeler nvs_flash_erase() puis de
     * réessayer — un réflexe qui NE DOIT JAMAIS s'appliquer ici :
     * nvs_flash_erase() efface la partition nvs (0x9000) ENTIÈRE, or c'est
     * justement celle que wifi.c passe onze lignes à expliquer comme
     * partagée avec le firmware d'origine. ESP_ERR_NVS_NO_FREE_PAGES n'a
     * rien d'exotique : c'est ce que rend une partition que le firmware
     * d'origine a remplie. L'effacer détruirait ses identifiants WiFi — le
     * seul accès de l'appareil — sans qu'aucun secours Kconfig ne soit
     * garanti configuré (défaut "" dans Kconfig.projbuild). Un échec ici
     * fait au pire échouer wifi_start() plus bas, non fatal comme les
     * étages suivants : le sauvetage ramène alors au firmware d'origine
     * avec sa NVS intacte. Détruire l'état avant d'échouer, ou aborter dans
     * une boucle de redémarrage via ESP_ERROR_CHECK, sont les deux seules
     * façons de rendre cet échec irrécupérable — d'où l'absence des deux
     * ici. */
    esp_err_t erreur_nvs = nvs_flash_init();
    if (erreur_nvs != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init a echoue : %s ; NVS laissee intacte", esp_err_to_name(erreur_nvs));
    }

    /* Un échec ici n'est volontairement pas fatal : c'est justement le cas
     * que le sauvetage automatique couvre. wifi_start() en échec (par
     * opposition à une simple absence de réseau) laisse le sauvetage armé
     * jusqu'à l'échéance, faute d'atteindre le point de désarmement plus bas
     * (voir rescue_disarm() en fin de fonction) : c'est voulu, un WiFi qui ne
     * démarre même pas est le signe d'un mauvais flash, pas d'un réseau qui
     * tarde. */
    erreur = wifi_start();
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start a echoue : %s ; le sauvetage automatique reste actif", esp_err_to_name(erreur));
    }

    web_set_boot_count(compteur_demarrages);

    /* Feature "Impression depuis USB", tache B : montage MSC + scan --
       independant du backend Klipper/du serveur HTTP ci-dessous (aucun lien),
       demarre ICI (juste apres web_set_boot_count()) pour que le scan tourne
       des que possible si une cle est deja inseree au boot, sans attendre
       reglages_charger()/boucle_demarrer() plus bas.
       ATTENTION (constat, non corrige ici -- hors perimetre de cette tache,
       toucherait le BSP vendore) : pt_usb_start() (pandatouch_msc.c) appelle
       en interne ESP_ERROR_CHECK(usb_host_install(...)) -- contrairement a
       TOUT le reste de cette fonction (NVS, WiFi, serveur HTTP, ecran, qui
       journalisent et continuent sur erreur), un echec d'installation du
       pilote USB host (materiel absent, conflit de ressources) ferait donc
       PANIQUER/redemarrer tout le firmware ici. Voir le rapport de cette
       tache. */
    pt_usb_on_mount(usb_on_mount_cb);
    pt_usb_on_unmount(usb_on_unmount_cb);
    pt_usb_start();

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

    /* Réglages, sélection du backend puis démarrage de la boucle
     * d'interrogation — placés ICI, délibérément après web_start() : le
     * serveur HTTP (donc /log et /revert, et maintenant /state) est déjà
     * debout avant qu'on touche à la NVS des réglages ou qu'on lance la
     * tâche réseau de boucle.c. Un réglage illisible ou un backend qui
     * échoue à démarrer reste ainsi diagnosticable à distance au lieu de
     * priver l'appareil de son seul canal de secours si l'étage suivant
     * tombait avec lui. Comme pour NVS et WiFi plus haut : aucun
     * ESP_ERROR_CHECK ici, on journalise et on continue — un hôte non
     * configuré est l'état normal d'un premier démarrage (voir
     * reglages.h), pas une panne, et rien ci-dessous ne doit pouvoir
     * redémarrer l'appareil. */
    esp_err_t erreur_reglages = reglages_charger();
    if (erreur_reglages != ESP_OK) {
        JOURNAL_ALERTE(TAG, "reglages_charger a echoue (%s) ; reglages par defaut conserves",
                       esp_err_to_name(erreur_reglages));
    }

    if (!reglages_configures()) {
        /* Premier démarrage, ou réglages jamais saisis : normal, pas une
         * erreur. L'écran de première configuration (ECRAN_CONFIGURATION,
         * empilé plus bas sous PT_LVGL_SCOPE_LOCK()) renseignera l'hôte ;
         * en attendant qu'il le fasse, ne pas démarrer la boucle plutôt que
         * de la lancer contre une adresse vide, qui échouerait à chaque
         * cycle pour rien. Texte corrigé (revue tâche 8, round 1, Q6) :
         * l'ancienne version annonçait cet écran "à venir au sous-jalon 2b"
         * -- c'était vrai avant la tâche 8, plus depuis. */
        JOURNAL_ALERTE(TAG, "aucun hote configure : la boucle d'interrogation ne demarre pas "
                       "(en attente de l'ecran de configuration)");
    } else {
        /* Sélection du backend par le nom que porte CHAQUE descripteur
         * lui-même (desc->nom), jamais par une chaîne recopiée ici : ainsi
         * un futur renommage d'un backend ne peut pas faire diverger cette
         * comparaison de la valeur réellement stockée dans les réglages. */
        const backend_desc_t *desc_moonraker = backend_moonraker_desc();
        const backend_desc_t *desc_factice = backend_factice_desc();
        const char *nom_backend = reglages_backend();

        const backend_desc_t *desc_choisi = NULL;
        if (desc_moonraker != NULL && strcmp(nom_backend, desc_moonraker->nom) == 0) {
            desc_choisi = desc_moonraker;
        } else if (desc_factice != NULL && strcmp(nom_backend, desc_factice->nom) == 0) {
            desc_choisi = desc_factice;
        } else {
            /* Nom inconnu : réglage corrompu, ou nom d'un backend retiré
             * depuis. Repli documenté sur moonraker — le backend par défaut
             * de reglages.h — plutôt qu'un choix silencieux ; le
             * avertissement ci-dessous est ce qui rend ce repli visible
             * dans /log au lieu de laisser deviner pourquoi la machine
             * attendue ne répond jamais. */
            JOURNAL_ALERTE(TAG, "backend '%s' inconnu ; repli sur '%s'",
                           nom_backend, desc_moonraker != NULL ? desc_moonraker->nom : "?");
            desc_choisi = desc_moonraker;
        }

        backend_hote_t hote;
        reglages_hote(&hote);

        if (desc_choisi != NULL) {
            esp_err_t erreur_boucle = boucle_demarrer(desc_choisi, &hote);
            if (erreur_boucle != ESP_OK) {
                JOURNAL_ERREUR(TAG, "boucle_demarrer a echoue (%s) : /state restera a son etat initial",
                               esp_err_to_name(erreur_boucle));
            }
        }
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
            /* Feature "Miniatures gcode", tache B : enregistre le decodeur
             * PNG une fois pour toute la duree de vie du firmware, AVANT
             * toute construction d'ecran (pt_display_init() ci-dessus a deja
             * appele lv_init() en interne -- voir esp_lvgl_port/le BSP
             * vendorise -- donc la liste globale de decodeurs d'images
             * existe deja ici). CONFIG_LV_USE_LODEPNG=y (firmware/sdkconfig[.
             * defaults]) : sans lui, lv_lodepng_init() n'est meme pas
             * declaree (voir lv_lodepng.h, entierement sous #if
             * LV_USE_LODEPNG) -- le controleur (gate idf) verifie cette
             * activation, voir le rapport de la tache B. Sous le meme verrou
             * LVGL que build_test_pattern() juste en dessous : l'enregistrement
             * d'un decodeur touche l'etat global LVGL (liste chainee des
             * decodeurs), meme discipline que tout le reste de ce bloc. */
            lv_lodepng_init();

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

            /* Tâche 8 (sous-jalon 2b) : choix de l'écran de départ, câblé ici
             * plutôt que laissé à la tâche 10 parce que reglages_configures()
             * est déjà connu à ce point (reglages_charger() a tourné plus
             * haut) et que ce site est déjà sous PT_LVGL_SCOPE_LOCK() pour
             * build_test_pattern() ci-dessus — le même verrou protège donc
             * cette construction-ci sans rien inventer.
             *
             * Un écran d'accueil est TOUJOURS empilé en premier, jamais
             * seulement ECRAN_CONFIGURATION seul (corrigé revue tâche 8,
             * round 1, Q1 IMPORTANT) : navigation_accueil() (bouton Save de
             * l'écran de configuration) est un `while (profondeur > 1)`, un
             * no-op à profondeur 1 -- avec ECRAN_CONFIGURATION seul en fond
             * de pile, Save n'aurait donc LITTÉRALEMENT aucun endroit où
             * revenir : la bannière "Settings saved" s'affiche, l'écran ne
             * bouge pas, et le bouton retour reste caché (profondeur == 1).
             * Reproduit et confirmé par la revue avec la vraie topologie de
             * ce fichier. Empiler l'accueil D'ABORD puis, si l'appareil
             * n'est pas encore configuré, ECRAN_CONFIGURATION PAR-DESSUS lui
             * donne un sens réel à Save (retour à l'accueil, déjà construit
             * et grisé puisque la boucle ne démarre pas tant que l'hôte
             * n'est pas configuré) et fait apparaître le bouton retour
             * (profondeur > 1) pour qui veut jeter un œil à l'accueil en
             * cours de configuration sans valider quoi que ce soit.
             *
             * Tâche 3 (jalon 3b), mis à jour tâche 7 (accueil-hub remplace
             * l'idle) : ECRAN_ACCUEIL_HUB plutôt que ECRAN_ACCUEIL
             * (impression) ici -- au tout premier démarrage la boucle
             * applicative vient tout juste de naître (boucle_init()
             * ci-dessous n'a pas encore tourné un seul cycle), l'état réel
             * de la machine (impression en cours ou non) n'est donc pas
             * encore connu et accueil_choix.h ne peut pas encore trancher.
             * Le repos est l'état de démarrage le plus courant ET le plus
             * sûr (aucun contrôle dangereux -- Cancel, E-STOP -- proposé
             * hors ligne, contrairement à l'accueil impression) : il sert
             * donc de défaut fixe, jamais recalculé ici. La bascule VIVANTE
             * idle<->impression une fois la première donnée reçue est
             * différée à la fin du plan 3b (voir task-3-brief.md) --
             * accueil_choix.h existe déjà et est exercé par le simulateur
             * (simulateur/main.c), mais rien ne l'appelle encore depuis
             * cette boucle applicative. */
            habillage_construire(lv_screen_active());
            /* Bouton engrenage de la barre d'etat -> ecran de configuration
             * (adresse imprimante), accessible depuis l'accueil a tout moment,
             * pas seulement au premier demarrage. */
            habillage_definir_ecran_reglages(&ECRAN_CONFIGURATION);
            /* Comportement Klipper du rail persistant (Accueil/Home/Macros/STOP)
             * + ids d'ecran servant au surlignage. L'habillage reste generique :
             * il ne connait ni klipper_gcode.h ni ces ecrans, l'application les
             * injecte ici (meme motif que habillage_definir_ecran_reglages
             * ci-dessus). */
            habillage_definir_action_rail(rail_action_klipper, NULL, ECRAN_ACCUEIL_HUB.id,
                                          ECRAN_MACROS.id);
            /* Bascule vivante repos<->impression du fond : l'habillage
             * consulte ce chooser à chaque pompage (voir
             * habillage_definir_choix_accueil()). Le choix AU BOOT ci-dessous
             * reste ECRAN_ACCUEIL_HUB (état réel pas encore connu, défaut sûr
             * -- voir le commentaire de l'empilement) ; la bascule prend le
             * relais dès le premier état reçu de la boucle applicative. */
            habillage_definir_choix_accueil(choix_accueil_klipper, NULL);
            esp_err_t erreur_accueil = navigation_empiler(&ECRAN_ACCUEIL_HUB);
            if (erreur_accueil != ESP_OK) {
                JOURNAL_ERREUR(TAG, "navigation_empiler(accueil) a echoue (%s) : ecran de depart absent",
                               esp_err_to_name(erreur_accueil));
            }
            /* Second empilement CONDITIONNÉ à la réussite du premier (revue
             * tâche 8, round 2, minor) : les deux appels ne sont pas
             * indépendants. Sans cette garde, un accueil refusé (à cause
             * d'un NO_MEM au tout premier démarrage, par exemple — son
             * contexte est le plus gros des deux écrans) suivi d'un
             * ECRAN_CONFIGURATION plus petit qui, lui, réussirait, recrée
             * EXACTEMENT le cul-de-sac Q1 corrigé plus haut : configuration
             * seule à profondeur 1, sous un profil de panne différent (échec
             * d'allocation partiel plutôt qu'un oubli de code). */
            if (erreur_accueil == ESP_OK && !reglages_configures()) {
                esp_err_t erreur_config = navigation_empiler(&ECRAN_CONFIGURATION);
                if (erreur_config != ESP_OK) {
                    JOURNAL_ERREUR(TAG, "navigation_empiler(configuration) a echoue (%s)",
                                   esp_err_to_name(erreur_config));
                }
            } else if (erreur_accueil != ESP_OK) {
                JOURNAL_ALERTE(TAG, "ecran de configuration non empile : l'accueil a deja echoue");
            }

            /* Tâche 10 : un premier habillage_pomper(), synchrone, peuple la
             * barre d'état une fois à la construction (juste en dessous) ;
             * un lv_timer récurrent (interface_timer_cb() plus haut) prend
             * ensuite le relais pour la faire vivre (bandeau qui expire,
             * heure, wifi). Créé ICI, sous le même PT_LVGL_SCOPE_LOCK() que
             * le reste de cette construction, parce que lv_timer_create()
             * n'est lui-même pas thread-safe vis-à-vis du fil LVGL — le
             * créer hors du verrou risquerait de le faire courir pendant que
             * build_test_pattern()/habillage_construire() ci-dessus modifient
             * encore l'arbre d'objets. Une fois créé, son rappel s'exécute
             * par construction sur le fil LVGL (voir interface_timer_cb()) :
             * aucun verrou n'y est repris. Période de 200 ms : largement
             * assez pour une réactivité sous la seconde sur le bandeau de
             * notification, sans rivaliser avec la boucle de sondage à 1 Hz
             * de boucle.c. Échec de création journalisé, jamais fatal (pas
             * d'ESP_ERROR_CHECK) : lv_timer_create() rendant NULL sur un
             * épuisement mémoire LVGL, l'interface reste alors figée sur son
             * premier habillage_pomper() plutôt que de faire tomber le
             * firmware pour un bandeau qui ne s'auto-masquera pas.
             *
             * La mire de build_test_pattern() ci-dessus n'est PAS retirée :
             * comportement inchangé au niveau code (corrigé revue tâche 8,
             * round 1, Q5 : ce paragraphe affirmait à tort que "l'habillage
             * la couvre entièrement, fond opaque plein cadre" -- FAUX :
             * g_contenu, la zone que habillage_construire() crée pour
             * accueillir la navigation, est intégralement TRANSPARENTE
             * (lv_obj_remove_style_all(), voir habillage.c). Ce qui couvre
             * réellement la mire est la SOMME de la bande d'état opaque
             * (44 px) et du fond opaque que CHAQUE écran empilé pose
             * lui-même sur la totalité de sa propre zone (voir COULEUR_FOND
             * dans ecran_accueil.c/ecran_configuration.c) -- une convention
             * suivie par les deux écrans actuels, pas une garantie que
             * habillage.c impose. Si navigation_empiler() échouait ici
             * (pile pleine, allocation refusée -- voir les JOURNAL_ERREUR
             * ci-dessus), rien ne serait empilé et la mire referait surface
             * au travers de g_contenu transparent : un repli dégradé mais
             * honnête (une mire lisible plutôt qu'un écran gris uniforme),
             * pas un défaut à corriger. rafraichir_etat_ecran() continue de
             * tourner sans changement dans la boucle 5 s plus bas quel que
             * soit ce résultat — rien du diagnostic WiFi du jalon 1 n'est
             * supprimé, il devient seulement invisible tant qu'un écran
             * réel couvre effectivement l'affichage. */
            habillage_pomper();

            lv_timer_t *minuteur_interface = lv_timer_create(interface_timer_cb, 200, NULL);
            if (minuteur_interface == NULL) {
                JOURNAL_ALERTE(TAG, "lv_timer_create(interface) a echoue : rafraichissement "
                               "periodique indisponible, l'interface reste figee sur son premier etat");
            }

            lv_timer_t *minuteur_temp = lv_timer_create(echantillon_temp_cb, 5000, NULL);
            if (minuteur_temp == NULL) {
                JOURNAL_ALERTE(TAG, "lv_timer_create(echantillon temp) a echoue : historique de "
                               "temperature indisponible");
            }
        }

        ESP_LOGI(TAG, "interface construite, le panneau doit etre allume");

        /* Fix (jalon 3b, sous-projet 7 tâche 1) : désarmer le sauvetage ICI,
         * une fois le firmware prouvé sain, plutôt que de compter
         * uniquement sur IP_EVENT_STA_GOT_IP (wifi.c). Tous les étages
         * risqués qui précèdent dans cette fonction -- NVS, wifi_start(),
         * web_start(), réglages/choix du backend/boucle_demarrer(), et enfin
         * pt_display_init() plus haut ainsi que la construction de l'écran/
         * habillage ci-dessus (habillage_construire(), le premier
         * habillage_pomper(), la création du minuteur d'interface) -- ont
         * réussi ; la boucle principale ci-dessous s'apprête à prendre le
         * relais. Ce point n'est atteint QUE dans la branche où
         * pt_display_init() a réussi (ce bloc else) : si l'écran avait
         * échoué (voir le if (erreur_affichage != ESP_OK) ci-dessus), on
         * n'arriverait jamais ici et le sauvetage resterait armé -- c'est ce
         * qui préserve sa protection contre un vrai mauvais flash (panique,
         * PSRAM, init écran/WiFi qui échoue).
         *
         * Avant ce correctif, un firmware par ailleurs totalement sain mais
         * démarré à froid sans réseau immédiatement disponible (routeur ou
         * imprimante pas encore monté) restait épinglé sur le seul
         * désarmement de wifi.c : le minuteur de rescue.c (rescue_arm() plus
         * haut, ~CONFIG_KTOUCH_RESCUE_TIMEOUT_MS) rebasculait alors de slot
         * OTA et redémarrait avant d'avoir laissé sa chance au réseau, et le
         * firmware d'origine récupéré par la bascule se heurtait au même
         * problème de réseau -- un ping-pong entre les deux slots jusqu'à ce
         * que le réseau réponde enfin. rescue_reset_boot_count() ci-dessous
         * remet aussi à zéro le compteur RTC (RESCUE_DEMARRAGES_MAX, en tête
         * de cette fonction) : un firmware sain ne l'accumule plus non plus.
         *
         * Compromis assumé : un firmware sain qui ne rejoindra JAMAIS le
         * WiFi (mauvais identifiants, par exemple) ne se rebascule plus tout
         * seul une fois ce point atteint -- il retente indéfiniment sans
         * jamais redémarrer. C'est corrigé par la saisie WiFi à l'écran
         * (sous-projet en cours), pas par le sauvetage. Le désarmement sur
         * IP_EVENT_STA_GOT_IP (wifi.c) reste en place, inchangé : il devient
         * simplement, dans le cas courant, le premier des deux à survenir --
         * redondant avec celui-ci mais inoffensif (rescue_disarm() et
         * rescue_reset_boot_count() sont idempotents). */
        rescue_disarm();
        rescue_reset_boot_count();
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "toujours vivant");

        /* Rafraîchit la ligne d'état (état WiFi, IP une fois obtenue) sur ce
         * même battement de 5 s. rafraichir_etat_ecran() s'abstient toute
         * seule si label_etat est NULL (écran indisponible) ; on prend tout
         * de même le verrou LVGL seulement quand il y a un écran à
         * rafraîchir, pour ne pas dépendre d'un verrou jamais initialisé si
         * pt_display_init() a échoué. */
        if (label_etat != NULL) {
            PT_LVGL_SCOPE_LOCK() {
                rafraichir_etat_ecran();
            }
        }
    }
}
