/* Implémentation : voir usb_scan.h pour le contrat et le POURQUOI (fix RAM
 * interne, lot Power/Console/Miniatures/USB -- voir la mémoire du projet).
 * Code déplacé QUASI TEL QUEL depuis app_main.c (feature "Impression depuis
 * USB", tâche B) -- seuls changent : le déclenchement (paresseux, plus au
 * boot) et l'emplacement mémoire du tampon de scan (PSRAM plutôt que .bss
 * RAM interne, voir usb_scan_demarrage_paresseux() plus bas). */
#include "usb_scan.h"

#ifdef ESP_PLATFORM
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h" /* xTaskCreateWithCaps -- pile de scan en PSRAM */
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pandatouch_msc.h"

#include "journal.h"
#include "usb_fichiers.h"
#include "usb_upload.h" /* usb_est_gcode() -- tache A de "Impression depuis USB" */

static const char *TAG = "usb_scan";

/* Pile large (8 Kio, meme ordre de grandeur que les taches dediees d'ota.c) :
 * cette tache porte la recursion de usb_scan_recursif() (un cadre ~USB_FICHIER_CHEMIN_MAX
 * octets par niveau de sous-dossier) -- s_usb_scan_tampon, lui, n'est jamais
 * sur cette pile (voir plus bas, desormais PSRAM). Priorite identique aux
 * autres taches dediees de ce firmware (ota.c) : nettement au-dessus d'IDLE,
 * sans rivaliser avec les taches internes du pilote WiFi/USB. */
#define USB_SCAN_TASK_STACK 8192
#define USB_SCAN_TASK_PRIO  (tskIDLE_PRIORITY + 5)

/* Tampon de scan EN PSRAM (fix RAM interne, voir le commentaire de tete de
 * usb_scan.h) -- alloue UNE SEULE FOIS, au tout premier
 * usb_scan_demarrage_paresseux() reussi (voir plus bas), jamais en .bss RAM
 * interne comme avant ce fix. Rempli par usb_scan_recursif() puis recopie EN
 * UNE FOIS dans le store verrouille (usb_fichiers_definir()) une fois le
 * scan complet termine. Partage entre scans successifs (un seul a la fois :
 * l'unique tache perenne de scan, voir s_scan_reveil ci-dessous, est la
 * seule a y ecrire) : pas de risque de lecture partielle par le store, qui
 * ne voit jamais ce tampon directement. */
static usb_fichier_t *s_usb_scan_tampon;

/* Tâche de scan PÉRENNE + sémaphore de réveil (fix "memoire epuisee" du
 * 2026-08-14) : la version précédente créait la tâche À CHAQUE montage,
 * c'est-à-dire au pire moment possible -- l'hôte USB venait de consommer
 * ~40 Kio de RAM interne (54 Kio libres au repos -> 13 Kio clé montée,
 * mesuré via /status.heap_interne) et xTaskCreate(8 Kio de pile d'un seul
 * tenant) échouait : "creation de la tache de scan USB echouee (memoire
 * epuisee)", scan jamais lancé, écran figé sur "Insert a USB key" clé
 * pourtant montée. Créés UNE SEULE FOIS ici, dans
 * usb_scan_demarrage_paresseux(), AVANT pt_usb_start() -- donc avant que
 * l'hôte USB ne mange la marge -- puis la tâche dort sur le sémaphore et
 * chaque montage ne fait que la réveiller (aucune allocation au montage).
 *
 * Le sémaphore BINAIRE sert aussi de coalescence (revue du 2026-08-14, L1) :
 * un montage qui survient PENDANT un scan (éjection + réinsertion rapide,
 * l'ancienne garde "scan deja en cours" jetait ce montage et la nouvelle clé
 * n'était JAMAIS scannée -- pire, le scan en vol finissait sa récursion sur
 * la nouvelle clé et publiait une liste MÉLANGÉE étiquetée valide) laisse
 * simplement le sémaphore levé : le tour de boucle suivant rescanne la clé
 * réellement présente et écrase toute publication bâtarde. Plusieurs
 * montages pendant un même scan coalescent en UN rescan -- exactement ce
 * qu'il faut, la liste finale reflète l'état final. */
static SemaphoreHandle_t s_scan_reveil;
static TaskHandle_t      s_scan_tache;

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
    /* Tâche pérenne (voir s_scan_reveil plus haut) : dort sur le sémaphore,
       un tour de boucle par montage -- plus jamais de vTaskDelete(NULL) ni
       de re-création. */
    for (;;) {
        if (xSemaphoreTake(s_scan_reveil, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        uint8_t nb = 0;
        bool tronques = false;
        usb_scan_recursif(PT_USB_MOUNT_PATH, &nb, &tronques);
        /* La cle a pu etre ejectee PENDANT le scan (ce tour de boucle dure des
           centaines de ms) : usb_on_unmount_cb() a alors deja publie monte=false.
           Ne pas ecraser cet etat par un "true" en dur -- relire l'etat reel du
           montage. Si demontee, publier monte=false + liste vide plutot qu'une
           liste perimee etiquetee "cle presente" (scenario ejection a chaud). */
        bool encore_monte = pt_usb_is_mounted();
        usb_fichiers_definir(encore_monte, encore_monte ? s_usb_scan_tampon : NULL,
                             encore_monte ? nb : 0, encore_monte ? tronques : false);
        /* Fenetre lecture->publication (revue du 2026-08-14, L6) : une
           ejection qui tombe entre le pt_usb_is_mounted() ci-dessus et le
           definir() peut faire atterrir NOTRE publication "monte=true" APRES
           celle du callback d'unmount -- liste fantome persistante, rien ne
           republierait jamais. Relire une seconde fois et corriger ferme la
           fenetre a l'epaisseur d'une lecture (le cas résiduel est rattrapé
           par le rescan du prochain montage, sémaphore coalescent). */
        if (encore_monte && !pt_usb_is_mounted()) {
            usb_fichiers_definir(false, NULL, 0, false);
        }
        JOURNAL_INFO(TAG, "scan USB : %u fichier(s) .gcode trouve(s)%s", (unsigned)nb,
                     tronques ? " (liste tronquee)" : "");
    }
}

/* Callback pt_usb_on_mount() -- tourne sur "msc_inst_w", une tache du BSP a
 * SEULEMENT 4096 o de pile (voir pandatouch_msc.c, pt_usb_install_device_task()) :
 * jamais de scan direct ici (la recursion + les tampons locaux de
 * usb_scan_recursif() y depasseraient vite ce budget -- la spec de cette
 * tache demande explicitement de ne pas gonfler les piles USB du BSP). La
 * tache PERENNE (8 Kio en PSRAM, voir USB_SCAN_TASK_STACK et s_scan_reveil)
 * fait le travail reel ; ce callback ne fait que la reveiller et revenir
 * immediatement. */
static void usb_on_mount_cb(void)
{
    if (s_scan_reveil == NULL || s_scan_tache == NULL) {
        /* Ne devrait jamais arriver : usb_scan_demarrage_paresseux() refuse
           de demarrer l'USB tant que la tache/le semaphore n'existent pas. */
        JOURNAL_ERREUR(TAG, "tache de scan absente, montage ignore");
        return;
    }
    /* JAMAIS de garde "scan deja en cours" ici (revue du 2026-08-14, L1) :
       un montage pendant un scan en vol DOIT redéclencher un scan, sinon la
       nouvelle clé reste invisible -- voir le commentaire de s_scan_reveil
       sur la coalescence du sémaphore binaire. L'ecran doit voir "lecture de
       la cle en cours" pendant toute la duree du scan (45 s observees sur
       une cle bien remplie), voir usb_fichiers.h. */
    usb_fichiers_scan_demarre();
    xSemaphoreGive(s_scan_reveil);
}

static void usb_on_unmount_cb(void)
{
    usb_fichiers_definir(false, NULL, 0, false);
    JOURNAL_ALERTE(TAG, "cle USB retiree, store vide");
}

static bool s_demarre = false;

void usb_scan_demarrage_paresseux(void)
{
    if (s_demarre) {
        return;
    }

    /* Alloue le tampon de scan EN PSRAM avant tout demarrage reel -- jamais
       de callback enregistre ni de pt_usb_start() tant qu'il n'est pas la
       (le scan en dependrait). Echec (PSRAM absente/epuisee) : on renonce a
       demarrer PLUTOT que de repartir sur un tampon RAM interne -- ce serait
       reintroduire exactement le defaut que cette tache corrige. `s_demarre`
       reste FAUX : une reouverture ulterieure de l'ecran USB retentera cette
       allocation (elle a une chance de reussir plus tard, ex. apres qu'un
       autre consommateur PSRAM ait libere de la place). */
    if (s_usb_scan_tampon == NULL) {
        s_usb_scan_tampon =
            (usb_fichier_t *)heap_caps_malloc((size_t)USB_FICHIERS_MAX * sizeof(usb_fichier_t), MALLOC_CAP_SPIRAM);
        if (s_usb_scan_tampon == NULL) {
            JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour le tampon de scan USB (%u octets) ; "
                           "USB non demarre", (unsigned)((size_t)USB_FICHIERS_MAX * sizeof(usb_fichier_t)));
            return;
        }
    }

    /* Semaphore + tache de scan crees ICI, une seule fois, AVANT
       pt_usb_start() : c'est le moment ou la RAM interne est la plus saine
       (~54 Kio libres mesures) -- l'hote USB en mange ~40 des qu'il demarre,
       et la creation au montage echouait (voir le commentaire de
       s_scan_reveil). Echec ici : on renonce a demarrer l'USB (s_demarre
       reste faux, retente a la prochaine ouverture de l'ecran), meme
       politique que le tampon PSRAM ci-dessus -- jamais un USB "demarre"
       dont les montages ne seraient jamais scannes. */
    if (s_scan_reveil == NULL) {
        s_scan_reveil = xSemaphoreCreateBinary();
        if (s_scan_reveil == NULL) {
            JOURNAL_ERREUR(TAG, "creation du semaphore de scan USB echouee ; USB non demarre");
            return;
        }
    }
    if (s_scan_tache == NULL) {
        /* Pile EN PSRAM (revue du 2026-08-14, L7) : 8 Kio pérennes en RAM
           interne seraient ~60 % de la marge mesurée clé montée (13 Kio).
           CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY et
           CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM sont déjà actifs, et
           cette tâche ne touche jamais un chemin cache-flash-désactivé
           (parcours VFS USB + store PSRAM + journal uniquement). */
        BaseType_t cree = xTaskCreateWithCaps(usb_scan_tache, "usb_scan", USB_SCAN_TASK_STACK, NULL,
                                              USB_SCAN_TASK_PRIO, &s_scan_tache,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (cree != pdPASS) {
            s_scan_tache = NULL;
            JOURNAL_ERREUR(TAG, "creation de la tache de scan USB echouee (memoire epuisee) ; USB non demarre");
            return;
        }
    }

    s_demarre = true;

    /* ATTENTION (constat, non corrige ici -- hors perimetre de cette tache,
       toucherait le BSP vendore) : pt_usb_start() (pandatouch_msc.c) appelle
       en interne ESP_ERROR_CHECK(usb_host_install(...)) -- un echec
       d'installation du pilote USB host (materiel absent, conflit de
       ressources) ferait donc PANIQUER/redemarrer tout le firmware ici.
       Repousse au premier acces a l'ecran USB (au lieu du tout premier
       instant du boot) : WiFi/serveur HTTP/ecran sont deja debout a ce
       stade, donc /revert et /log restent joignables meme si ce panic
       survient. Voir le rapport de la tache d'origine ("Impression depuis
       USB", tache B) pour le detail complet du compromis. */
    pt_usb_on_mount(usb_on_mount_cb);
    pt_usb_on_unmount(usb_on_unmount_cb);
    pt_usb_start();

    /* Cle deja presente au moment de ce demarrage tardif (branchee avant que
       l'utilisateur n'ouvre l'ecran USB, contrairement au demarrage au boot
       ou ce cas n'existait pas encore) : le callback de montage ne se
       declenche que sur un evenement de montage MATERIEL a venir, jamais
       retroactivement pour un peripherique deja monte au moment de son
       enregistrement -- sans ce declenchement explicite, la cle resterait
       invisible du store tant qu'elle ne serait pas rebranchee. */
    if (pt_usb_is_mounted()) {
        usb_on_mount_cb();
    }
}
#else
void usb_scan_demarrage_paresseux(void)
{
    /* no-op host-test : pandatouch_msc.h (pt_usb_*) et FreeRTOS n'existent
       pas sur PC, voir le commentaire de tete de usb_scan.h. */
}
#endif
