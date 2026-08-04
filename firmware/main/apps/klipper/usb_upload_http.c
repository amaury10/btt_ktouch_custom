/* Implémentation de usb_upload_http.h -- voir ce header pour le contrat.
 *
 * Patron repris de ota.c (tâche dédiée créée à la demande, jamais le travail
 * long sur la pile de l'appelant) et de backend_moonraker.c (esp_http_client :
 * construction d'URL avec crochets IPv6, open/write/fetch_headers/status,
 * close+cleanup sur TOUS les chemins) -- voir leurs commentaires de tête pour
 * le détail de ces deux choix, repris ici sans les reexpliquer.
 *
 * ÉCART DÉLIBÉRÉ par rapport à la lecture du fichier suggérée par le plan de
 * cette tâche ("boucle pt_usb_read(tampon borné) -> esp_http_client_write") :
 * `pt_usb_read()` (pandatouch_msc.h) fopen()+fread(buf_size)+fclose() LE
 * FICHIER ENTIER À CHAQUE APPEL (voir pandatouch_msc.c) -- il n'a PAS de
 * paramètre d'offset. L'appeler en boucle ne ferait donc que relire les
 * `buf_size` premiers octets du fichier N fois, jamais avancer dedans : cette
 * API-là est taillée pour un fichier COURT lu d'un coup (voir son usage réel
 * dans ce dépôt, `pt_usb_write`/`pt_usb_read` complets), pas pour un flux.
 * `/usb` est cela dit un point de montage VFS FAT standard (msc_host_vfs_register(),
 * voir pandatouch_msc.c) -- exactement comme `display_slideshow.c` ouvre déjà
 * directement un chemin sous `/usb` via `fopen()` (voir son
 * `start_slideshow_task()`), ce fichier utilise donc fopen()/fread()/fclose()
 * standard sur `chemin_usb` pour le VRAI flux borné (USB_UPLOAD_HTTP_TAMPON_OCTETS
 * à la fois, jamais le fichier entier en RAM) -- la seule façon d'obtenir un
 * flux réel avec l'API telle qu'elle existe aujourd'hui dans ce BSP. */
#include "usb_upload_http.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "backend.h"   /* backend_hote_t */
#include "journal.h"
#include "reglages.h"  /* reglages_hote() -- même hôte/port que le backend Moonraker */
#include "usb_fichiers.h" /* USB_FICHIER_CHEMIN_MAX */
#include "usb_upload.h"   /* usb_upload_preambule/trailer/content_length -- tâche A */

static const char *TAG = "usb_upload_http";

/* Taille de bloc de lecture/écriture : bornée, jamais le fichier entier en
 * RAM (contrainte de la spec) -- même ordre de grandeur que OTA_TAILLE_BLOC
 * (ota.c), suffisant pour amortir le coût par appel fread()/esp_http_client_write()
 * sans jamais engager plus de 4 Kio à la fois. Statique (BSS), jamais sur la
 * pile de la tâche dédiée -- même discipline que le tampon d'ota.c. */
#define USB_UPLOAD_HTTP_TAMPON_OCTETS 4096u
static uint8_t s_tampon_lecture[USB_UPLOAD_HTTP_TAMPON_OCTETS];

/* Boundary FIXE : ce dépôt ne génère jamais de contenu utilisateur AVANT le
 * multipart (le fichier vient de la clé USB, jamais composé par ce firmware),
 * une valeur constante suffit à éviter toute collision avec les DEUX autres
 * parts (root/print, texte fixe) -- seul le contenu du .gcode lui-même
 * pourrait en théorie contenir cette séquence, un risque déjà accepté par la
 * spec (streaming imposé, aucune inspection du contenu n'est possible sans
 * charger le fichier). */
#define USB_UPLOAD_HTTP_BOUNDARY "----KTouchUSBUpload7f3a9c1d"

/* Marge large pour usb_upload_preambule() : gabarit fixe (~180 octets) +
 * 3 occurrences du boundary (28 caractères) + le nom de fichier nettoyé
 * (borné à 256 par usb_upload.c, USB_UPLOAD_FILENAME_MAX) -- 512 couvre
 * confortablement le pire cas (180 + 3*28 + 255 = 519... voir la marge
 * ci-dessous, portée à 768 pour rester loin de la limite plutôt qu'au plus
 * juste). */
#define USB_UPLOAD_HTTP_PREAMBULE_MAX 768u
#define USB_UPLOAD_HTTP_TRAILER_MAX   64u
#define USB_UPLOAD_HTTP_URL_MAX       (BACKEND_HOTE_LONGUEUR_MAX + 64u)

/* Délai par opération socket individuelle -- même ordre de grandeur que
 * MOONRAKER_DELAI_MS (backend_moonraker.c) : un LAN local, pas une borne
 * pensée pour un très gros fichier (chaque écriture de
 * USB_UPLOAD_HTTP_TAMPON_OCTETS reste courte, ce délai ne borne QUE cette
 * écriture-là, jamais le transfert entier). */
#define USB_UPLOAD_HTTP_TIMEOUT_MS 5000

/* Pile large (comme ota.c : 8192) -- cette tâche porte esp_http_client
 * (plusieurs structures internes) + les tampons de pile locaux (préambule/
 * trailer/URL, quelques centaines d'octets), le tampon de lecture lui-même
 * étant statique (BSS, voir plus haut). Sans lien avec les tâches USB du
 * BSP (4096 o chacune, non touchées ici) : celle-ci est une tâche DÉDIÉE,
 * propre à ce fichier. */
#define USB_UPLOAD_HTTP_TASK_STACK 8192
#define USB_UPLOAD_HTTP_TASK_PRIO  (tskIDLE_PRIORITY + 5)

/* Verrou paresseux (créé au premier usb_upload_http_demarrer(), jamais avant)
 * -- même idiome que g_verrou dans moonraker_ws.c (voir son commentaire) :
 * usb_upload_http_lire()/_en_cours() tolèrent un verrou encore NULL (aucune
 * tâche d'upload n'a jamais tourné, rien à protéger, la copie directe de
 * l'état INACTIF initial est déjà sûre en mono-thread à ce stade). */
static SemaphoreHandle_t g_verrou = NULL;

static usb_upload_http_progression_t g_progression = {
    .etat = USB_UPLOAD_HTTP_INACTIF,
    .envoyes = 0,
    .total = 0,
    .message = "",
};

/* Chemin/taille de la demande en cours -- copiés sous verrou par
 * usb_upload_http_demarrer() AVANT de créer la tâche, relus par la tâche
 * elle-même une seule fois à son tout début (voir usb_upload_http_tache()) :
 * jamais modifiés une fois la tâche lancée, aucune course possible au-delà de
 * cette lecture initiale. */
static char   g_chemin_usb[USB_FICHIER_CHEMIN_MAX];
static size_t g_taille_fichier;

static void verrou_prendre(void)
{
    if (g_verrou != NULL) {
        xSemaphoreTake(g_verrou, portMAX_DELAY);
    }
}

static void verrou_rendre(void)
{
    if (g_verrou != NULL) {
        xSemaphoreGive(g_verrou);
    }
}

/* Publie un changement d'état complet (transition EN_COURS -> SUCCES/ECHEC,
 * ou le tout premier passage à EN_COURS) -- `message` NULL vide le champ. */
static void publier_etat(usb_upload_http_etat_t etat, size_t envoyes, size_t total, const char *message)
{
    verrou_prendre();
    g_progression.etat = etat;
    g_progression.envoyes = envoyes;
    g_progression.total = total;
    if (message != NULL) {
        snprintf(g_progression.message, sizeof(g_progression.message), "%s", message);
    } else {
        g_progression.message[0] = '\0';
    }
    verrou_rendre();
}

/* Publie une avancée d'octets SANS toucher etat/message -- appelée à chaque
 * bloc écrit pendant la boucle de streaming, plus légère que publier_etat(). */
static void publier_progression(size_t envoyes, size_t total)
{
    verrou_prendre();
    g_progression.envoyes = envoyes;
    g_progression.total = total;
    verrou_rendre();
}

/* Écrit intégralement `longueur` octets de `donnees` sur `client`, en
 * bouclant sur d'éventuelles écritures partielles (esp_http_client_write()
 * n'est pas documenté comme garantissant toujours une écriture complète en un
 * seul appel) -- jamais un octet perdu silencieusement. Rend faux au premier
 * retour <= 0 (erreur ou connexion fermée), avec `msg_echec` rempli. */
static bool ecrire_tout(esp_http_client_handle_t client, const char *donnees, size_t longueur,
                         char *msg_echec, size_t msg_echec_taille)
{
    size_t ecrit_total = 0;
    while (ecrit_total < longueur) {
        int ecrit = esp_http_client_write(client, donnees + ecrit_total, (int)(longueur - ecrit_total));
        if (ecrit <= 0) {
            snprintf(msg_echec, msg_echec_taille, "envoi interrompu a %u/%u octets",
                     (unsigned)ecrit_total, (unsigned)longueur);
            return false;
        }
        ecrit_total += (size_t)ecrit;
    }
    return true;
}

/* Construit "http://<adresse>:<port>/server/files/upload" dans `tampon` --
 * même logique de crochets IPv6 que moonraker_construire_url()
 * (backend_moonraker.c, voir son commentaire complet) : `hote->adresse` est
 * stockée SANS crochets même pour un littéral IPv6 (hote_parse.c), les
 * remettre est le travail de CE site. Copiée plutôt que partagée (la
 * fonction d'origine est `static` à backend_moonraker.c) -- même choix que le
 * reste de ce dépôt vis-à-vis du partage de code entre fichiers. */
static void construire_url(const backend_hote_t *hote, char *tampon, size_t taille)
{
    if (strchr(hote->adresse, ':') != NULL) {
        snprintf(tampon, taille, "http://[%s]:%u/server/files/upload", hote->adresse, (unsigned)hote->port);
    } else {
        snprintf(tampon, taille, "http://%s:%u/server/files/upload", hote->adresse, (unsigned)hote->port);
    }
}

static void usb_upload_http_tache(void *arg)
{
    (void)arg;

    /* memcpy, PAS snprintf(dst, N, "%s", src) -- `chemin` et `g_chemin_usb`
     * font la MEME taille fixe (USB_FICHIER_CHEMIN_MAX), ce que gcc ne peut
     * pas prouver borne a la compilation ; meme piege, meme correctif que
     * ecran_fichiers.c/ecran_usb.c (voir leur commentaire sur
     * -Werror=format-truncation). `g_chemin_usb` est deja garanti
     * NUL-terminé par usb_upload_http_demarrer() (snprintf depuis un
     * `const char *` de taille inconnue à gcc, donc sans ce piège) --
     * le forçage explicite ci-dessous reste une garde défensive, jamais
     * supposée. */
    char   chemin[USB_FICHIER_CHEMIN_MAX];
    size_t taille_fichier;
    verrou_prendre();
    memcpy(chemin, g_chemin_usb, sizeof(chemin));
    taille_fichier = g_taille_fichier;
    verrou_rendre();
    chemin[sizeof(chemin) - 1] = '\0';

    const char *nom_fichier = strrchr(chemin, '/');
    nom_fichier = (nom_fichier != NULL) ? nom_fichier + 1 : chemin;

    char preambule[USB_UPLOAD_HTTP_PREAMBULE_MAX];
    size_t preambule_len =
        usb_upload_preambule(preambule, sizeof(preambule), USB_UPLOAD_HTTP_BOUNDARY, nom_fichier);
    if (preambule_len >= sizeof(preambule)) {
        JOURNAL_ERREUR(TAG, "preambule tronque (%u octets requis, tampon %u)",
                       (unsigned)preambule_len, (unsigned)sizeof(preambule));
        publier_etat(USB_UPLOAD_HTTP_ECHEC, 0, taille_fichier, "erreur interne : nom de fichier trop long");
        vTaskDelete(NULL);
        return;
    }

    char trailer[USB_UPLOAD_HTTP_TRAILER_MAX];
    size_t trailer_len = usb_upload_trailer(trailer, sizeof(trailer), USB_UPLOAD_HTTP_BOUNDARY);
    if (trailer_len >= sizeof(trailer)) {
        /* Ne peut arriver en pratique : USB_UPLOAD_HTTP_BOUNDARY est un
           littéral fixe court, largement sous USB_UPLOAD_HTTP_TRAILER_MAX --
           garde défensive seulement, même discipline que le reste de ce
           dépôt (jamais une écriture non gardée, même "certaine"). */
        publier_etat(USB_UPLOAD_HTTP_ECHEC, 0, taille_fichier, "erreur interne : trailer trop long");
        vTaskDelete(NULL);
        return;
    }

    size_t content_length = usb_upload_content_length(preambule_len, taille_fichier, trailer_len);
    publier_progression(0, content_length);

    backend_hote_t hote;
    if (!reglages_hote(&hote) || hote.adresse[0] == '\0') {
        publier_etat(USB_UPLOAD_HTTP_ECHEC, 0, content_length, "aucun hote Moonraker configure");
        vTaskDelete(NULL);
        return;
    }

    char url[USB_UPLOAD_HTTP_URL_MAX];
    construire_url(&hote, url, sizeof(url));

    FILE *fichier = fopen(chemin, "rb");
    if (fichier == NULL) {
        JOURNAL_ERREUR(TAG, "fopen(%s) a echoue", chemin);
        publier_etat(USB_UPLOAD_HTTP_ECHEC, 0, content_length, "fichier introuvable sur la cle (ejectee ?)");
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = USB_UPLOAD_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        fclose(fichier);
        publier_etat(USB_UPLOAD_HTTP_ECHEC, 0, content_length, "erreur interne : client HTTP indisponible");
        vTaskDelete(NULL);
        return;
    }

    char entete_content_type[64];
    snprintf(entete_content_type, sizeof(entete_content_type),
             "multipart/form-data; boundary=%s", USB_UPLOAD_HTTP_BOUNDARY);
    esp_http_client_set_header(client, "Content-Type", entete_content_type);

    bool ok = true;
    char msg_echec[USB_UPLOAD_HTTP_MESSAGE_MAX] = "";
    size_t envoyes = 0;
    int statut = -1;

    /* `content_length` connu AVANT le premier octet écrit (préambule + taille
       du fichier + trailer déjà sommés ci-dessus) : esp_http_client_open()
       pose l'en-tête Content-Length lui-même à partir de ce second paramètre
       -- jamais posé à la main ici, même convention que le reste de ce
       fichier vis-à-vis de l'API esp_http_client. */
    esp_err_t erreur = esp_http_client_open(client, (int)content_length);
    if (erreur != ESP_OK) {
        ok = false;
        snprintf(msg_echec, sizeof(msg_echec), "connexion a Moonraker impossible (%s)", esp_err_to_name(erreur));
    }

    if (ok && !ecrire_tout(client, preambule, preambule_len, msg_echec, sizeof(msg_echec))) {
        ok = false;
    }
    if (ok) {
        envoyes += preambule_len;
        publier_progression(envoyes, content_length);
    }

    /* Boucle de streaming : fread() borné (USB_UPLOAD_HTTP_TAMPON_OCTETS à la
       fois, tampon STATIQUE, voir le commentaire de tête pour pourquoi
       fopen()/fread() plutôt que pt_usb_read()) -> esp_http_client_write().
       Jamais le fichier entier en RAM -- exactement la contrainte de la
       spec. */
    while (ok) {
        size_t lu = fread(s_tampon_lecture, 1, sizeof(s_tampon_lecture), fichier);
        if (lu == 0) {
            if (ferror(fichier)) {
                ok = false;
                snprintf(msg_echec, sizeof(msg_echec), "lecture du fichier USB interrompue a %u octets",
                         (unsigned)envoyes);
            }
            break; /* fin de fichier (feof) : sortie normale de la boucle */
        }
        if (!ecrire_tout(client, (const char *)s_tampon_lecture, lu, msg_echec, sizeof(msg_echec))) {
            ok = false;
            break;
        }
        envoyes += lu;
        publier_progression(envoyes, content_length);
    }

    if (ok && !ecrire_tout(client, trailer, trailer_len, msg_echec, sizeof(msg_echec))) {
        ok = false;
    }
    if (ok) {
        envoyes += trailer_len;
        publier_progression(envoyes, content_length);
    }

    if (ok) {
        (void)esp_http_client_fetch_headers(client);
        statut = esp_http_client_get_status_code(client);
        if (statut < 200 || statut >= 300) {
            ok = false;
            snprintf(msg_echec, sizeof(msg_echec), "Moonraker a refuse l'envoi (statut HTTP %d)", statut);
        }
    }

    /* Fermeture/libération sur TOUS les chemins -- succès comme échec, même
       discipline que moonraker_fermer()/moonraker_requete() (backend_moonraker.c). */
    fclose(fichier);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (ok) {
        JOURNAL_INFO(TAG, "upload de %s reussi (%u octets, statut HTTP %d)", chemin, (unsigned)envoyes, statut);
        publier_etat(USB_UPLOAD_HTTP_SUCCES, envoyes, content_length, NULL);
    } else {
        JOURNAL_ERREUR(TAG, "upload de %s echoue : %s", chemin, msg_echec);
        publier_etat(USB_UPLOAD_HTTP_ECHEC, envoyes, content_length, msg_echec);
    }

    vTaskDelete(NULL);
}

bool usb_upload_http_demarrer(const char *chemin_usb, size_t taille_fichier)
{
    if (chemin_usb == NULL || chemin_usb[0] == '\0') {
        return false;
    }

    if (g_verrou == NULL) {
        g_verrou = xSemaphoreCreateMutex();
        if (g_verrou == NULL) {
            JOURNAL_ERREUR(TAG, "xSemaphoreCreateMutex a echoue (memoire epuisee)");
            return false;
        }
    }

    verrou_prendre();
    bool deja_en_cours = (g_progression.etat == USB_UPLOAD_HTTP_EN_COURS);
    if (!deja_en_cours) {
        snprintf(g_chemin_usb, sizeof(g_chemin_usb), "%s", chemin_usb);
        g_taille_fichier = taille_fichier;
        g_progression.etat = USB_UPLOAD_HTTP_EN_COURS;
        g_progression.envoyes = 0;
        g_progression.total = taille_fichier; /* affiné par la tâche une fois préambule/trailer connus */
        g_progression.message[0] = '\0';
    }
    verrou_rendre();

    if (deja_en_cours) {
        JOURNAL_ALERTE(TAG, "upload deja en cours, second demarrage refuse (%s)", chemin_usb);
        return false;
    }

    BaseType_t cree = xTaskCreate(usb_upload_http_tache, "usb_upload", USB_UPLOAD_HTTP_TASK_STACK, NULL,
                                   USB_UPLOAD_HTTP_TASK_PRIO, NULL);
    if (cree != pdPASS) {
        JOURNAL_ERREUR(TAG, "creation de la tache d'upload echouee (memoire epuisee)");
        publier_etat(USB_UPLOAD_HTTP_ECHEC, 0, taille_fichier, "creation de la tache d'upload impossible");
        return false;
    }
    return true;
}

void usb_upload_http_lire(usb_upload_http_progression_t *dest)
{
    if (dest == NULL) {
        return;
    }
    verrou_prendre();
    *dest = g_progression;
    verrou_rendre();
}

bool usb_upload_http_en_cours(void)
{
    bool resultat;
    verrou_prendre();
    resultat = (g_progression.etat == USB_UPLOAD_HTTP_EN_COURS);
    verrou_rendre();
    return resultat;
}
