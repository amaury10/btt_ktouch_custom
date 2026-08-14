/* Implémentation : voir usb_scan.h pour le contrat et le POURQUOI (fix RAM
 * interne, lot Power/Console/Miniatures/USB -- voir la mémoire du projet).
 * Code déplacé QUASI TEL QUEL depuis app_main.c (feature "Impression depuis
 * USB", tâche B) -- seuls changent : le déclenchement (paresseux, plus au
 * boot) et l'emplacement mémoire du tampon de scan (PSRAM plutôt que .bss
 * RAM interne, voir usb_scan_demarrage_paresseux() plus bas). */
#include "usb_scan.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <strings.h> /* strcasecmp -- meme usage que usb_upload.c */
#include <sys/stat.h>

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

/* Repertoire demande par l'ecran (ou "/usb" au montage) -- ecrit par
 * usb_scan_demander() (tache LVGL, ou msc_inst_w via le callback de
 * montage), lu par la tache de listage : section critique COURTE, meme
 * patron que le store. Deux demandes rapprochees : la derniere ecrase la
 * premiere et le semaphore binaire coalesce -- exactement le comportement
 * voulu pour la navigation (dernier tap gagne). */
static portMUX_TYPE s_verrou_demande = portMUX_INITIALIZER_UNLOCKED;
static char s_chemin_demande[USB_FICHIER_CHEMIN_MAX] = PT_USB_MOUNT_PATH;

/* Liste UN repertoire (explorateur, spec 2026-08-14-usb-explorateur-
 * design.md -- plus jamais de scan recursif complet : 34-55 s mesures sur
 * une cle de 29 Go, la lecture brute de TOUS les repertoires etait le poste
 * dominant ; un seul niveau est quasi instantane). Retient sous-dossiers +
 * .gcode (filtre usb_est_gcode() + octets de controle), en opendir/readdir
 * DIRECT : d_type fourni gratuitement par le VFS FAT, stat() reserve aux
 * seuls .gcode retenus (taille requise par l'upload), aucune allocation par
 * entree. Chemin plus long que le store : entree IGNOREE plutot que
 * tronquee (un chemin tronque serait introuvable a l'upload/navigation).
 *
 * `retenir_dossiers` (revue du 2026-08-15, L3) : l'appelant enumere le MEME
 * repertoire en DEUX passes, dossiers d'abord -- sans cela, la troncature a
 * USB_FICHIERS_MAX en ordre FAT brut pouvait ejecter des sous-dossiers
 * enumeres tard, rendant leurs sous-arbres INACCESSIBLES depuis l'ecran
 * (un fichier de trop est paginable ailleurs, un dossier perdu est un
 * cul-de-sac). Deux enumerations d'UN repertoire restent bon marche --
 * c'est le scan recursif complet qui etait le poste dominant, pas ca. */
static void usb_lister_repertoire(const char *chemin, bool retenir_dossiers,
                                  uint8_t *nb, bool *tronques)
{
    DIR *dossier = opendir(chemin);
    if (dossier == NULL) {
        JOURNAL_ALERTE(TAG, "opendir(%s) a echoue (errno %d)", chemin ? chemin : "?", errno);
        return;
    }

    struct dirent *e;
    while ((e = readdir(dossier)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (e->d_name[0] == '.') {
            continue; /* caches -- meme regle que l'is_hidden du BSP */
        }

        bool est_dossier = (e->d_type == DT_DIR);
        if (est_dossier != retenir_dossiers) {
            continue; /* pas la passe de ce type d'entree, voir l'en-tete */
        }
        if (est_dossier && strcasecmp(e->d_name, "System Volume Information") == 0) {
            continue; /* dossier d'indexation Windows : jamais de .gcode utilisateur */
        }
        if (!est_dossier && (usb_nom_a_octet_controle(e->d_name) || !usb_est_gcode(e->d_name))) {
            continue;
        }

        char chemin_entree[USB_FICHIER_CHEMIN_MAX];
        int ecrit = snprintf(chemin_entree, sizeof(chemin_entree), "%s/%s", chemin, e->d_name);
        if (ecrit < 0 || (size_t)ecrit >= sizeof(chemin_entree)) {
            continue;
        }

        if (*nb >= USB_FICHIERS_MAX) {
            *tronques = true;
            break; /* plus rien a retenir, inutile de finir l'enumeration */
        }

        /* Seul stat() du listage : la taille d'un .gcode retenu (requise
           pour le Content-Length de l'upload). Echec -> taille 0, l'entree
           reste listable. */
        size_t taille = 0;
        if (!est_dossier) {
            struct stat infos;
            taille = (stat(chemin_entree, &infos) == 0) ? (size_t)infos.st_size : 0;
        }

        usb_fichier_t *dest = &s_usb_scan_tampon[*nb];
        memcpy(dest->chemin, chemin_entree, (size_t)ecrit + 1);
        dest->taille = taille;
        dest->est_dossier = est_dossier;
        (*nb)++;
    }

    closedir(dossier);
}

static void usb_scan_tache(void *arg)
{
    (void)arg;
    /* Tâche pérenne (voir s_scan_reveil plus haut) : dort sur le sémaphore,
       un tour de boucle par demande de listage -- plus jamais de
       vTaskDelete(NULL) ni de re-création. */
    for (;;) {
        if (xSemaphoreTake(s_scan_reveil, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* Instantané de la demande courante -- une demande plus récente
           arrivée PENDANT ce listage relèvera le sémaphore, le tour suivant
           la servira (dernier tap gagne). */
        char chemin[USB_FICHIER_CHEMIN_MAX];
        portENTER_CRITICAL(&s_verrou_demande);
        memcpy(chemin, s_chemin_demande, sizeof(chemin));
        portEXIT_CRITICAL(&s_verrou_demande);

        uint8_t nb = 0;
        bool tronques = false;
        /* Dossiers d'abord (deux passes, voir usb_lister_repertoire()) : la
           troncature ne peut sacrifier que des fichiers, jamais l'acces a un
           sous-arbre. */
        usb_lister_repertoire(chemin, /*retenir_dossiers=*/true, &nb, &tronques);
        usb_lister_repertoire(chemin, /*retenir_dossiers=*/false, &nb, &tronques);
        usb_listing_trier(s_usb_scan_tampon, nb);
        /* La cle a pu etre ejectee PENDANT le listage : usb_on_unmount_cb()
           a alors deja publie monte=false. Ne pas ecraser cet etat par un
           "true" en dur -- relire l'etat reel du montage (scenario ejection
           a chaud). */
        bool encore_monte = pt_usb_is_mounted();
        usb_fichiers_definir(encore_monte, encore_monte ? chemin : "",
                             encore_monte ? s_usb_scan_tampon : NULL,
                             encore_monte ? nb : 0, encore_monte ? tronques : false);
        /* Fenetre lecture->publication (revue du 2026-08-14, L6) : une
           ejection qui tombe entre le pt_usb_is_mounted() ci-dessus et le
           definir() peut faire atterrir NOTRE publication "monte=true" APRES
           celle du callback d'unmount -- liste fantome persistante, rien ne
           republierait jamais. Relire une seconde fois et corriger ferme la
           fenetre a l'epaisseur d'une lecture (le cas résiduel est rattrapé
           par le listage du prochain montage, sémaphore coalescent). */
        if (encore_monte && !pt_usb_is_mounted()) {
            usb_fichiers_definir(false, "", NULL, 0, false);
        }
        JOURNAL_INFO(TAG, "listage %s : %u entree(s)%s", chemin, (unsigned)nb,
                     tronques ? " (tronque)" : "");

        /* Une demande PLUS RECENTE est-elle arrivee PENDANT ce listage ?
           usb_fichiers_definir() vient de rabattre scan_en_cours sans savoir
           qu'un autre repertoire attend (revue du 2026-08-15, L4 : l'ecran
           montrait le repertoire A comme acquis alors que B se listait) :
           relever le drapeau, l'ecran garde "listage en cours" jusqu'a la
           publication du BON repertoire. Le semaphore, deja leve par cette
           demande, fait reboucler immediatement. strcmp sous verrou : borne
           a USB_FICHIER_CHEMIN_MAX octets, section courte. */
        bool demande_en_attente;
        portENTER_CRITICAL(&s_verrou_demande);
        demande_en_attente = (strncmp(chemin, s_chemin_demande, sizeof(chemin)) != 0);
        portEXIT_CRITICAL(&s_verrou_demande);
        if (demande_en_attente) {
            usb_fichiers_scan_demarre();
        }
    }
}

void usb_scan_demander(const char *chemin)
{
    if (s_scan_reveil == NULL || s_scan_tache == NULL) {
        /* Ne devrait jamais arriver : usb_scan_demarrage_paresseux() refuse
           de demarrer l'USB tant que la tache/le semaphore n'existent pas,
           et l'ecran ne peut demander qu'apres son propre demarrage
           paresseux. */
        JOURNAL_ERREUR(TAG, "tache de listage absente, demande ignoree");
        return;
    }
    if (chemin == NULL || chemin[0] == '\0') {
        chemin = PT_USB_MOUNT_PATH;
    }
    /* strlen HORS de la section critique (discipline "verrou court" du
       depot) ; seule la copie est verrouillée. */
    size_t longueur = strlen(chemin);
    if (longueur >= sizeof(s_chemin_demande)) {
        longueur = sizeof(s_chemin_demande) - 1;
    }
    portENTER_CRITICAL(&s_verrou_demande);
    memcpy(s_chemin_demande, chemin, longueur);
    s_chemin_demande[longueur] = '\0';
    portEXIT_CRITICAL(&s_verrou_demande);
    /* L'ecran doit voir "listage en cours" pendant la lecture du repertoire
       (jusqu'a 1-2 s sur un gros dossier), voir usb_fichiers.h. */
    usb_fichiers_scan_demarre();
    xSemaphoreGive(s_scan_reveil);
}

/* Callback pt_usb_on_mount() -- tourne sur "msc_inst_w", une tache du BSP a
 * SEULEMENT 4096 o de pile (voir pandatouch_msc.c, pt_usb_install_device_task()) :
 * jamais de listage direct ici, la tache PERENNE (8 Kio en PSRAM, voir
 * USB_SCAN_TASK_STACK et s_scan_reveil) fait le travail reel. Un montage =
 * une demande de listage de la RACINE (l'utilisateur repart toujours de
 * "/usb" quand une cle arrive). Un montage pendant un listage en vol
 * redéclenche naturellement (revue du 2026-08-14, L1 : jamais de garde
 * "deja en cours", la coalescence du semaphore fait le bon arbitrage). */
static void usb_on_mount_cb(void)
{
    usb_scan_demander(PT_USB_MOUNT_PATH);
}

static void usb_on_unmount_cb(void)
{
    usb_fichiers_definir(false, "", NULL, 0, false);
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

void usb_scan_demander(const char *chemin)
{
    (void)chemin; /* meme no-op host : l'ecran compile, rien ne se liste */
}
#endif
