/* ota.c — sauvegarde RAW de l'image app0 (BTT, le firmware d'origine) vers
 * la partition spiffs, avec verification SHA-256 apres relecture depuis la
 * flash. Voir ota.h pour le contrat et la garantie de surete (app0 lu seul,
 * jamais ecrit ; spiffs seule cible d'ecriture de ce fichier -- jamais un
 * slot app).
 *
 * Choix de taille de sauvegarde : l'image BTT reelle est plus petite que les
 * 4,5 Mio de la partition app0 (les segments de l'image ESP donneraient une
 * taille exacte plus petite), mais ce fichier sauvegarde la PARTITION
 * ENTIERE (app0->size) plutot que de parser les segments pour en deduire la
 * taille reelle : plus simple, plus robuste (aucune hypothese sur le format
 * de l'image au-dela du magic 0xE9, deja verifie ailleurs -- voir
 * ota_image.h), et la partition spiffs (6,8 Mio) est assez grande pour la
 * recevoir avec de la marge (en-tete 40 octets + 4,5 Mio, contre 6,8 Mio
 * disponibles). Le seul cout est quelques dizaines/centaines de Kio de fin
 * de partition non utilises par l'image reelle, sauvegardes tels quels --
 * sans consequence : une restauration ulterieure ecrira ces memes octets de
 * "remplissage" a la meme position dans le slot cible, exactement comme
 * app0 les contient deja aujourd'hui.
 *
 * Choix d'execution : le travail flash (lecture app0 + effacement/ecriture
 * spiffs + relecture de verification, quelques secondes pour ~4,5 Mio)
 * tourne sur une tache dediee creee pour l'occasion (meme motif que
 * rescue_switch_now() dans rescue.c), jamais sur la pile de l'appelant --
 * potentiellement la tache httpd, dont web.c documente qu'elle ne doit
 * porter aucune ecriture flash directement. ota_backup_btt() reste
 * neanmoins une fonction SYNCHRONE de bout en bout, cote appelant (c'est la
 * signature demandee, voir ota.h) : elle bloque sur un semaphore jusqu'a la
 * fin de la tache dediee. Bloquer sur un semaphore laisse l'ordonnanceur
 * executer les taches IDLE pendant l'attente -- contrairement a une boucle
 * d'E/S flash tournant directement sur la pile de l'appelant, qui
 * monopoliserait un coeur assez longtemps pour risquer d'affamer les taches
 * IDLE (donc le chien de garde des taches, qui les surveille par defaut). La
 * boucle de blocs elle-meme cede aussi la main periodiquement (voir
 * OTA_CEDER_TOUS_LES_N_BLOCS plus bas) : defense en profondeur peu couteuse,
 * plutot que de compter uniquement sur le fait que cette tache-la soit
 * dediee. */

#include "ota.h"
#include "ota_image.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "rescue.h"

static const char *TAG = "ota";

/* Taille de bloc de lecture/ecriture/hachage : 4096 = a la fois la taille de
   secteur d'effacement de la flash SPI de cette cible (esp_partition_erase_range
   exige un effacement aligne sur ce secteur -- voir esp_flash_erase_chip/
   SPI_FLASH_SEC_SIZE dans ESP-IDF) ET une taille de bloc de streaming
   raisonnable : jamais plus de 4 Kio de l'image en RAM a la fois, une image
   de 4,5 Mio n'est donc jamais tenue en entier en memoire. */
#define OTA_TAILLE_BLOC 4096u

/* Cede la main a l'ordonnanceur tous les N blocs (N * 4 Kio = 128 Kio) : une
   image de 4,5 Mio traverse ~35 cessions par passe, quelques dizaines de
   millisecondes ajoutees au total sur les quelques secondes deja attendues --
   voir le commentaire de tete de ce fichier pour la justification complete. */
#define OTA_CEDER_TOUS_LES_N_BLOCS 32u

/* Taille plancher plausible d'une image applicative ESP (tache 4, dry-run
   /ota) : purement defensive -- rejette tot une requete manifestement pas une
   image (fichier vide, quelques octets de test envoyes par erreur) avant
   meme de regarder le SHA-256, sans pretendre connaitre la taille exacte
   d'une vraie image K-Touch (voir ota_image.h : le controle de borne haute,
   lui, precis, vient de la taille reelle de la partition cible). 64 Kio :
   nettement en-dessous de toute image ESP-IDF reelle (quelques centaines de
   Kio a plusieurs Mio pour ce projet), nettement au-dessus d'un envoi
   accidentel de quelques octets/Kio. */
#define OTA_TAILLE_MIN_IMAGE (64u * 1024u)

/* Nombre de timeouts consecutifs de httpd_req_recv() tolerees avant
   d'abandonner la reception (tache 4, dry-run /ota) : un client qui cesse
   d'envoyer sans jamais fermer la connexion ne doit pas bloquer cette tache
   httpd indefiniment -- meme esprit defensif que OTA_CEDER_TOUS_LES_N_BLOCS
   ci-dessus, mais pour un client mal comporte plutot que pour l'ordonnanceur. */
#define OTA_TIMEOUTS_CONSECUTIFS_MAX 3u

/* Espace de noms NVS DEDIE au commit OTA (tache 5), distinct de
   REGLAGES_ESPACE_NOMS ("ktouch", voir core/reglages.h) : ce dernier est deja
   partage avec les identifiants WiFi saisis par l'utilisateur (nvs_open y est
   appele avec des cles that reglages.c connait et gere seul) -- un espace
   separe evite tout risque d'interference avec ce cache, et rend la portee
   de ce drapeau evidente a la lecture (rien a voir avec les reglages
   utilisateur). Ni l'un ni l'autre n'est la NVS INTERNE d'esp_wifi (partition
   "nvs" mais namespace gere par le pilote WiFi lui-meme, jamais touchee par
   ce firmware -- voir reglages.h) : ce fichier n'ouvre jamais cet espace-la. */
#define OTA_NVS_ESPACE_NOMS "ota"

/* Vrai des que le tout premier commit OTA reussi a ecrit dans app0 (donc a
   ecrase le firmware BTT d'origine qui s'y trouvait) -- voir la garde de
   ota_appliquer_flux() plus bas. Faux par defaut (cle absente = appareil
   jamais mis a jour depuis ce firmware, ou NVS pas encore ecrite) : c'est la
   valeur SURE, celle qui declenche l'exigence de sauvegarde. */
#define OTA_NVS_CLE_BTT_ECRASE "btt_ecrase"

static const esp_partition_t *trouver_app0(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
}

static const esp_partition_t *trouver_spiffs(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
}

/* Ecrit un message formate dans `msg` si non NULL/non vide, sans jamais
   deborder (vsnprintf borne toujours a msg_taille) -- meme convention que
   les autres routes de diagnostic de ce firmware (voir gestion_status() dans
   web.c) : un message tronque proprement vaut mieux qu'un debordement. */
static void ota_msg(char *msg, size_t msg_taille, const char *fmt, ...)
{
    if (msg == NULL || msg_taille == 0) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, msg_taille, fmt, args);
    va_end(args);
}

/* 32 octets -> 64 caracteres hexa minuscules + terminateur nul. `sortie_taille`
   doit etre >= 65 pour un SHA-256 complet ; sinon, tronque proprement (pas
   d'ecriture hors bornes). */
static void sha_vers_hex(const uint8_t sha[32], char *sortie, size_t sortie_taille)
{
    static const char chiffres[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < 32 && (i * 2 + 2) < sortie_taille; i++) {
        sortie[i * 2] = chiffres[(sha[i] >> 4) & 0x0Fu];
        sortie[i * 2 + 1] = chiffres[sha[i] & 0x0Fu];
    }
    if (sortie_taille > 0) {
        sortie[i * 2] = '\0';
    }
}

ota_backup_etat_t ota_backup_etat(void)
{
    const esp_partition_t *spiffs = trouver_spiffs();
    if (spiffs == NULL) {
        return OTA_BACKUP_ABSENT;
    }

    uint8_t entete_brut[OTA_BACKUP_ENTETE_TAILLE];
    if (esp_partition_read(spiffs, 0, entete_brut, sizeof(entete_brut)) != ESP_OK) {
        return OTA_BACKUP_ABSENT;
    }

    ota_backup_entete_t entete;
    if (!ota_backup_entete_parser(entete_brut, sizeof(entete_brut), &entete)) {
        /* magic absent ou errone : aucune sauvegarde exploitable -- spiffs
           jamais ecrite par ce module, ou encore a l'etat effacee d'usine
           (tout a 0xFF), pas une sauvegarde EXISTANTE corrompue. */
        return OTA_BACKUP_ABSENT;
    }

    /* `taille` vient d'une flash potentiellement corrompue au-dela du seul
       SHA-256 (secteur defaillant touchant l'en-tete lui-meme, par exemple) :
       borner AVANT de boucler dessus, pour ne jamais lire hors partition. */
    if ((uint64_t)OTA_BACKUP_ENTETE_TAILLE + entete.taille > spiffs->size) {
        return OTA_BACKUP_CORROMPU;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); /* 0 = SHA-256 (pas la variante SHA-224) */

    static uint8_t tampon[OTA_TAILLE_BLOC];
    size_t position = 0;
    uint32_t compteur_blocs = 0;
    bool erreur_lecture = false;
    while (position < entete.taille) {
        size_t bloc = entete.taille - position;
        if (bloc > sizeof(tampon)) {
            bloc = sizeof(tampon);
        }
        if (esp_partition_read(spiffs, OTA_BACKUP_ENTETE_TAILLE + position, tampon, bloc) != ESP_OK) {
            erreur_lecture = true;
            break;
        }
        mbedtls_sha256_update(&ctx, tampon, bloc);
        position += bloc;

        if (++compteur_blocs % OTA_CEDER_TOUS_LES_N_BLOCS == 0) {
            vTaskDelay(1);
        }
    }

    uint8_t sha[32];
    mbedtls_sha256_finish(&ctx, sha);
    mbedtls_sha256_free(&ctx);

    if (erreur_lecture) {
        return OTA_BACKUP_CORROMPU;
    }
    return ota_sha256_egal(sha, entete.sha256) ? OTA_BACKUP_VALIDE : OTA_BACKUP_CORROMPU;
}

/* Fait le travail reel : lecture app0 -> ecriture spiffs par blocs (SHA-256
   de l'image accumule au fil de l'eau), en-tete ecrit une fois le SHA-256
   final connu, puis relecture INDEPENDANTE (via ota_backup_etat(), qui relit
   depuis la flash et recalcule son propre SHA-256) pour prouver que la
   flash contient bien ce qui vient d'etre demande -- pas seulement que le
   hachage fait pendant l'ecriture s'accorde avec lui-meme. Tourne sur la
   tache dediee creee par ota_backup_btt() ci-dessous, jamais sur la pile de
   l'appelant. */
static esp_err_t ota_backup_btt_travail(char *msg, size_t msg_taille)
{
    const esp_partition_t *app0 = trouver_app0();
    const esp_partition_t *spiffs = trouver_spiffs();
    if (app0 == NULL || spiffs == NULL) {
        ota_msg(msg, msg_taille, "erreur : partition %s introuvable",
                app0 == NULL ? "app0" : "spiffs");
        ESP_LOGE(TAG, "partition %s introuvable", app0 == NULL ? "app0" : "spiffs");
        return ESP_ERR_NOT_FOUND;
    }

    /* Taille de sauvegarde = la partition app0 ENTIERE, pas la taille reelle
       de l'image BTT -- voir le commentaire de tete de ce fichier pour la
       justification. */
    size_t taille_image = app0->size;
    uint64_t taille_totale = (uint64_t)OTA_BACKUP_ENTETE_TAILLE + taille_image;
    if (taille_totale > spiffs->size) {
        ota_msg(msg, msg_taille, "erreur : spiffs trop petite pour la sauvegarde (%u < %llu octets)",
                (unsigned)spiffs->size, (unsigned long long)taille_totale);
        ESP_LOGE(TAG, "spiffs trop petite pour la sauvegarde");
        return ESP_ERR_INVALID_SIZE;
    }

    size_t taille_effacee = ota_taille_alignee((size_t)taille_totale, OTA_TAILLE_BLOC);
    ESP_LOGI(TAG, "sauvegarde BTT : app0 (%u octets) -> spiffs, effacement de %u octets",
             (unsigned)taille_image, (unsigned)taille_effacee);
    esp_err_t err = esp_partition_erase_range(spiffs, 0, taille_effacee);
    if (err != ESP_OK) {
        ota_msg(msg, msg_taille, "erreur : effacement de spiffs (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "effacement de spiffs echoue : %s", esp_err_to_name(err));
        return err;
    }

    /* Passe 1 : app0 -> spiffs (a partir de l'offset OTA_BACKUP_ENTETE_TAILLE,
       apres l'emplacement de l'en-tete), SHA-256 de l'image accumule au fil
       de l'eau. L'en-tete lui-meme est ecrit APRES cette boucle : son champ
       sha256 n'est connu qu'une fois l'image entiere hachee. */
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);

    static uint8_t tampon[OTA_TAILLE_BLOC];
    size_t position = 0;
    uint32_t compteur_blocs = 0;
    while (position < taille_image) {
        size_t bloc = taille_image - position;
        if (bloc > sizeof(tampon)) {
            bloc = sizeof(tampon);
        }
        err = esp_partition_read(app0, position, tampon, bloc);
        if (err != ESP_OK) {
            mbedtls_sha256_free(&ctx);
            ota_msg(msg, msg_taille, "erreur : lecture app0 a l'offset %u (%s)",
                    (unsigned)position, esp_err_to_name(err));
            ESP_LOGE(TAG, "lecture app0 echouee a l'offset %u : %s", (unsigned)position,
                      esp_err_to_name(err));
            return err;
        }
        err = esp_partition_write(spiffs, OTA_BACKUP_ENTETE_TAILLE + position, tampon, bloc);
        if (err != ESP_OK) {
            mbedtls_sha256_free(&ctx);
            ota_msg(msg, msg_taille, "erreur : ecriture spiffs a l'offset %u (%s)",
                    (unsigned)position, esp_err_to_name(err));
            ESP_LOGE(TAG, "ecriture spiffs echouee a l'offset %u : %s", (unsigned)position,
                      esp_err_to_name(err));
            return err;
        }
        mbedtls_sha256_update(&ctx, tampon, bloc);
        position += bloc;

        if (++compteur_blocs % OTA_CEDER_TOUS_LES_N_BLOCS == 0) {
            vTaskDelay(1);
        }
    }

    uint8_t sha[32];
    mbedtls_sha256_finish(&ctx, sha);
    mbedtls_sha256_free(&ctx);

    ota_backup_entete_t entete = {
        .magic = OTA_BACKUP_MAGIC,
        .taille = (uint32_t)taille_image,
    };
    memcpy(entete.sha256, sha, sizeof(entete.sha256));

    uint8_t entete_ser[OTA_BACKUP_ENTETE_TAILLE];
    if (!ota_backup_entete_serialiser(&entete, entete_ser, sizeof(entete_ser))) {
        /* Ne peut arriver en pratique : `entete_ser` fait exactement
           OTA_BACKUP_ENTETE_TAILLE et `entete` est une variable locale
           non-NULL -- garde defensive seulement (meme discipline que le
           reste du depot : jamais de dereferencement/ecriture non garde,
           meme quand l'appelant "sait" que les arguments sont corrects). */
        ota_msg(msg, msg_taille, "erreur interne : serialisation de l'en-tete de sauvegarde");
        ESP_LOGE(TAG, "serialisation de l'en-tete de sauvegarde echouee (ne devrait pas arriver)");
        return ESP_FAIL;
    }

    err = esp_partition_write(spiffs, 0, entete_ser, sizeof(entete_ser));
    if (err != ESP_OK) {
        ota_msg(msg, msg_taille, "erreur : ecriture de l'en-tete spiffs (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "ecriture de l'en-tete spiffs echouee : %s", esp_err_to_name(err));
        return err;
    }

    /* Passe 2 : RELECTURE INDEPENDANTE depuis spiffs (ota_backup_etat() relit
       l'en-tete puis les donnees et recalcule son propre SHA-256) -- prouve
       que la flash contient reellement ce qui vient d'etre ecrit, pas
       seulement que le hachage fait pendant l'ecriture de la passe 1
       s'accorde avec lui-meme. */
    ota_backup_etat_t etat = ota_backup_etat();
    if (etat != OTA_BACKUP_VALIDE) {
        ota_msg(msg, msg_taille,
                "erreur : verification apres ecriture echouee (etat=%d) -- sauvegarde NON fiable",
                (int)etat);
        ESP_LOGE(TAG, "verification post-ecriture de la sauvegarde echouee (etat=%d)", (int)etat);
        return ESP_ERR_INVALID_CRC;
    }

    char sha_hex[65];
    sha_vers_hex(sha, sha_hex, sizeof(sha_hex));
    ota_msg(msg, msg_taille, "OK : BTT sauvegarde en spiffs (%u octets), sha256=%s",
            (unsigned)taille_image, sha_hex);
    ESP_LOGI(TAG, "sauvegarde BTT verifiee, sha256=%s", sha_hex);
    return ESP_OK;
}

/* Contexte passe a la tache dediee : `msg`/`msg_taille` pointent vers le
   tampon de l'appelant, qui reste vivant tout le temps de l'appel puisque
   ota_backup_btt() bloque sur `termine` jusqu'a ce que la tache ait fini de
   s'en servir -- aucun risque d'ecrire dans une pile deja depilee. */
typedef struct {
    char *msg;
    size_t msg_taille;
    esp_err_t resultat;
    SemaphoreHandle_t termine;
} ota_backup_ctx_t;

static void ota_backup_tache(void *arg)
{
    ota_backup_ctx_t *ctx = (ota_backup_ctx_t *)arg;
    ctx->resultat = ota_backup_btt_travail(ctx->msg, ctx->msg_taille);
    xSemaphoreGive(ctx->termine);
    vTaskDelete(NULL);
}

esp_err_t ota_backup_btt(char *msg, size_t msg_taille)
{
    ota_backup_ctx_t ctx = {
        .msg = msg,
        .msg_taille = msg_taille,
        .resultat = ESP_FAIL,
        .termine = NULL,
    };
    ctx.termine = xSemaphoreCreateBinary();
    if (ctx.termine == NULL) {
        ota_msg(msg, msg_taille, "erreur : semaphore de sauvegarde indisponible (memoire epuisee)");
        ESP_LOGE(TAG, "creation du semaphore de sauvegarde echouee");
        return ESP_ERR_NO_MEM;
    }

    /* Pile 8192 (comme rescue.c/tache_sur_echeance) : plus large que le
       defaut, cette tache porte esp_partition_read/write + mbedtls_sha256
       (contexte ~200 octets) + le tampon statique OTA_TAILLE_BLOC (BSS, pas
       la pile). Priorite identique a rescue.c : nettement au-dessus d'IDLE,
       sans rivaliser avec les taches internes du pilote WiFi. */
    BaseType_t cree = xTaskCreate(ota_backup_tache, "ota_backup", 8192, &ctx,
                                   tskIDLE_PRIORITY + 5, NULL);
    if (cree != pdPASS) {
        vSemaphoreDelete(ctx.termine);
        ota_msg(msg, msg_taille, "erreur : tache de sauvegarde non creee (memoire epuisee)");
        ESP_LOGE(TAG, "creation de la tache de sauvegarde echouee");
        return ESP_ERR_NO_MEM;
    }

    /* Bloque jusqu'a la fin de la tache dediee : c'est la signature
       synchrone demandee par ota.h, mais l'attente elle-meme se fait sur un
       semaphore (donc l'ordonnanceur execute les taches IDLE pendant ce
       temps) -- jamais en boucle d'E/S flash directement sur cette pile-ci.
       Voir le commentaire de tete de ce fichier pour la justification
       complete de ce choix face a l'alternative synchrone directe. */
    xSemaphoreTake(ctx.termine, portMAX_DELAY);
    vSemaphoreDelete(ctx.termine);
    return ctx.resultat;
}

/* Tache 4 (jalon OTA firmware) : dry-run de reception d'image applicative --
 * voir le contrat complet dans ota.h. Recoit le corps de `req` par blocs de
 * OTA_TAILLE_BLOC (meme tampon statique et meme discipline de cession que le
 * reste de ce fichier), SANS jamais l'ecrire nulle part : ni sur une
 * partition applicative (aucun esp_ota_begin/write ici), ni meme sur spiffs
 * (contrairement a ota_backup_btt_travail() ci-dessus -- ce chemin-ci ne
 * garde RIEN de l'image recue au-dela du premier octet et du SHA-256 en
 * cours de calcul). */
esp_err_t ota_verifier_flux(httpd_req_t *req, const char *sha_attendu_hex, char *msg, size_t msg_taille)
{
    if (req == NULL) {
        ota_msg(msg, msg_taille, "erreur interne : requete nulle");
        ESP_LOGE(TAG, "ota_verifier_flux appelee avec req NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Taille annoncee par le client (en-tete Content-Length, seul mecanisme
       de transfert que gere esp_http_server pour un corps de requete -- pas
       de Transfer-Encoding: chunked). Sans elle, aucune borne fiable sur ce
       qui va etre recu : on refuse plutot que de lire un flux de taille
       inconnue. */
    size_t taille_annoncee = req->content_len;
    if (taille_annoncee == 0) {
        ota_msg(msg, msg_taille, "erreur : corps de requete vide ou Content-Length absent");
        ESP_LOGW(TAG, "dry-run OTA : corps vide ou Content-Length absent");
        return ESP_ERR_INVALID_SIZE;
    }

    /* Lecture SEULE de la taille de la partition ciblee par une future OTA :
       esp_ota_get_next_update_partition() ne fait ici que renseigner
       `cible->size`, borne haute de taille acceptee -- AUCUN esp_ota_begin/
       write sur cette partition, ni ici ni ailleurs dans cette fonction. */
    const esp_partition_t *cible = esp_ota_get_next_update_partition(NULL);
    if (cible == NULL) {
        ota_msg(msg, msg_taille, "erreur : partition cible (prochaine mise a jour) introuvable");
        ESP_LOGE(TAG, "dry-run OTA : esp_ota_get_next_update_partition() a rendu NULL");
        return ESP_ERR_NOT_FOUND;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); /* 0 = SHA-256 (pas la variante SHA-224) */

    static uint8_t tampon[OTA_TAILLE_BLOC];
    uint8_t premier_octet = 0;
    bool premier_octet_connu = false;
    size_t position = 0;
    uint32_t compteur_blocs = 0;
    uint32_t timeouts_consecutifs = 0;
    esp_err_t erreur_reception = ESP_OK;

    while (position < taille_annoncee) {
        size_t reste = taille_annoncee - position;
        size_t a_lire = reste > sizeof(tampon) ? sizeof(tampon) : reste;

        int recu = httpd_req_recv(req, (char *)tampon, a_lire);
        if (recu <= 0) {
            if (recu == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Client lent, pas forcement fautif : quelques nouvelles
                   tentatives avant d'abandonner -- voir
                   OTA_TIMEOUTS_CONSECUTIFS_MAX. */
                if (++timeouts_consecutifs >= OTA_TIMEOUTS_CONSECUTIFS_MAX) {
                    erreur_reception = ESP_ERR_TIMEOUT;
                    break;
                }
                continue;
            }
            /* recu == 0 (connexion fermee cote client) ou autre erreur
               negative : flux interrompu, rien a en tirer de plus. */
            erreur_reception = ESP_FAIL;
            break;
        }
        timeouts_consecutifs = 0;

        if (!premier_octet_connu) {
            premier_octet = tampon[0];
            premier_octet_connu = true;
        }

        mbedtls_sha256_update(&ctx, tampon, (size_t)recu);
        position += (size_t)recu;

        if (++compteur_blocs % OTA_CEDER_TOUS_LES_N_BLOCS == 0) {
            vTaskDelay(1);
        }
    }

    uint8_t sha[32];
    mbedtls_sha256_finish(&ctx, sha);
    mbedtls_sha256_free(&ctx);

    char sha_hex[65];
    sha_vers_hex(sha, sha_hex, sizeof(sha_hex));

    if (erreur_reception != ESP_OK) {
        ota_msg(msg, msg_taille,
                "sha256=%s -- erreur : reception interrompue a %u/%u octets (%s)",
                sha_hex, (unsigned)position, (unsigned)taille_annoncee,
                erreur_reception == ESP_ERR_TIMEOUT ? "timeout" : "connexion fermee");
        ESP_LOGW(TAG, "dry-run OTA : reception interrompue a %u/%u octets",
                 (unsigned)position, (unsigned)taille_annoncee);
        return erreur_reception;
    }

    /* `position` == `taille_annoncee` ici (la boucle ne sort sans erreur que
       lorsque tout ce qui a ete annonce a ete recu) -- utilise quand meme
       `position`, pas `taille_annoncee`, comme taille reelle de l'image pour
       ota_image_entete_valide() : c'est ce qui a effectivement traverse le
       hachage, la donnee dont ce dry-run peut vraiment repondre. */
    bool magic_ok = premier_octet_connu && premier_octet == 0xE9;
    bool entete_ok = ota_image_entete_valide(premier_octet_connu ? &premier_octet : NULL,
                                              premier_octet_connu ? 1u : 0u, position,
                                              OTA_TAILLE_MIN_IMAGE, cible->size);
    if (!entete_ok) {
        ota_msg(msg, msg_taille, "sha256=%s -- %s (recu %u octets, partition cible '%s' %u octets)",
                sha_hex, magic_ok ? "taille hors bornes" : "magic invalide",
                (unsigned)position, cible->label, (unsigned)cible->size);
        ESP_LOGW(TAG, "dry-run OTA : entete invalide (%s), %u octets recus",
                 magic_ok ? "taille hors bornes" : "magic invalide", (unsigned)position);
        return magic_ok ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_VERSION;
    }

    if (sha_attendu_hex != NULL && sha_attendu_hex[0] != '\0') {
        uint8_t sha_attendu[32];
        if (!ota_hex_vers_sha256(sha_attendu_hex, sha_attendu)) {
            ota_msg(msg, msg_taille,
                    "sha256=%s -- erreur : sha attendu mal forme (64 caracteres hexadecimaux attendus)",
                    sha_hex);
            ESP_LOGW(TAG, "dry-run OTA : sha attendu mal forme");
            return ESP_ERR_INVALID_ARG;
        }
        if (!ota_sha256_egal(sha, sha_attendu)) {
            ota_msg(msg, msg_taille, "sha256=%s -- SHA ne correspond pas", sha_hex);
            ESP_LOGW(TAG, "dry-run OTA : sha256=%s ne correspond pas au sha attendu", sha_hex);
            return ESP_ERR_INVALID_CRC;
        }
    }

    ota_msg(msg, msg_taille,
            "sha256=%s -- image valide (%u octets, magic OK, tient dans '%s') -- "
            "DRY-RUN : rien ecrit en flash",
            sha_hex, (unsigned)position, cible->label);
    ESP_LOGI(TAG, "dry-run OTA : image valide, %u octets, sha256=%s", (unsigned)position, sha_hex);
    return ESP_OK;
}

/* Lit le drapeau OTA_NVS_CLE_BTT_ECRASE dans l'espace OTA_NVS_ESPACE_NOMS.
   Rend FAUX (valeur sure -- exige la garde) si l'espace de noms n'a encore
   jamais ete ouvert en ecriture (aucun commit reussi n'a encore eu lieu, donc
   nvs_open en lecture seule rend ESP_ERR_NVS_NOT_FOUND -- pas une panne, un
   etat initial normal) ou si la cle elle-meme est absente. */
static bool ota_nvs_btt_ecrase_lire(void)
{
    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(OTA_NVS_ESPACE_NOMS, NVS_READONLY, &handle);
    if (erreur != ESP_OK) {
        return false;
    }
    uint8_t valeur = 0;
    erreur = nvs_get_u8(handle, OTA_NVS_CLE_BTT_ECRASE, &valeur);
    nvs_close(handle);
    return erreur == ESP_OK && valeur != 0;
}

/* Pose OTA_NVS_CLE_BTT_ECRASE=1, appelee UNIQUEMENT apres qu'un commit ait
   deja ecrit avec succes dans app0 (voir ota_appliquer_flux() plus bas) --
   n'ecrit jamais dans REGLAGES_ESPACE_NOMS ("ktouch", partage avec les
   identifiants WiFi) ni dans la NVS interne d'esp_wifi, seulement dans
   l'espace dedie OTA_NVS_ESPACE_NOMS ("ota") defini plus haut. Un echec ici
   n'annule pas le commit deja effectue (l'image est deja active, le
   sauvetage deja arme) : au pire, un futur commit visant a nouveau app0
   re-exigera une sauvegarde BTT pourtant deja obsolete -- direction sure
   (trop prudente), jamais dangereuse (voir l'appelant). */
static esp_err_t ota_nvs_btt_ecrase_marquer(void)
{
    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(OTA_NVS_ESPACE_NOMS, NVS_READWRITE, &handle);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (espace '%s') a echoue lors du marquage %s : %s",
                 OTA_NVS_ESPACE_NOMS, OTA_NVS_CLE_BTT_ECRASE, esp_err_to_name(erreur));
        return erreur;
    }
    erreur = nvs_set_u8(handle, OTA_NVS_CLE_BTT_ECRASE, 1);
    if (erreur == ESP_OK) {
        erreur = nvs_commit(handle);
    }
    nvs_close(handle);
    if (erreur != ESP_OK) {
        ESP_LOGE(TAG, "ecriture NVS %s=1 echouee : %s", OTA_NVS_CLE_BTT_ECRASE, esp_err_to_name(erreur));
    }
    return erreur;
}

/* Tache 5 (jalon OTA firmware) : commit OTA reel -- voir le contrat complet
 * dans ota.h. Contrairement a ota_backup_btt() plus haut, cette fonction NE
 * DELEGUE PAS a une tache dediee : elle recoit `req` via httpd_req_recv(),
 * valide seulement sur la tache proprietaire de la requete (la tache httpd),
 * donc elle tourne directement sur cette pile-la, exactement comme
 * ota_verifier_flux() ci-dessus -- meme tampon statique, meme discipline de
 * cession reguliere a l'ordonnanceur (OTA_CEDER_TOUS_LES_N_BLOCS), qui est
 * ici le SEUL mecanisme laissant tourner les taches IDLE (donc le chien de
 * garde des taches) pendant les quelques secondes de ce flux. N'appelle
 * jamais esp_restart() : c'est a l'appelant (web.c) de repondre au client
 * HTTP avant de redemarrer, comme /revert. */
esp_err_t ota_appliquer_flux(httpd_req_t *req, char *msg, size_t msg_taille)
{
    if (req == NULL) {
        ota_msg(msg, msg_taille, "erreur interne : requete nulle");
        ESP_LOGE(TAG, "ota_appliquer_flux appelee avec req NULL");
        return ESP_ERR_INVALID_ARG;
    }

    size_t taille_annoncee = req->content_len;
    if (taille_annoncee == 0) {
        ota_msg(msg, msg_taille, "erreur : corps de requete vide ou Content-Length absent");
        ESP_LOGW(TAG, "commit OTA : corps vide ou Content-Length absent");
        return ESP_ERR_INVALID_SIZE;
    }

    /* Cible d'ecriture : TOUJOURS le slot OTA inactif -- jamais le slot en
       cours d'execution. Meme appel que rescue_switch_to_other_slot()
       (rescue.c) et que ota_verifier_flux() ci-dessus. */
    const esp_partition_t *cible = esp_ota_get_next_update_partition(NULL);
    if (cible == NULL) {
        ota_msg(msg, msg_taille, "erreur : partition cible (prochaine mise a jour) introuvable");
        ESP_LOGE(TAG, "commit OTA : esp_ota_get_next_update_partition() a rendu NULL");
        return ESP_ERR_NOT_FOUND;
    }

    /* GARDE (invariant de surete n 1, le plus important de tout ce fichier) :
       tant que la cible est app0 (le slot qui porte encore le firmware BTT
       d'origine, ce qui reste vrai jusqu'au tout premier commit reussi) ET
       qu'aucun commit precedent n'a deja pose le drapeau NVS btt_ecrase, un
       backup BTT valide est EXIGE avant d'appeler esp_ota_begin(). Sans
       cette garde, la toute premiere mise a jour OTA effacerait le seul
       exemplaire du firmware d'origine avant meme qu'il n'ait ete
       sauvegarde -- sans port serie sur cet appareil, ce serait
       irrecuperable. */
    if (cible->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 && !ota_nvs_btt_ecrase_lire()
        && ota_backup_etat() != OTA_BACKUP_VALIDE) {
        ota_msg(msg, msg_taille, "refuse : sauvegarde BTT absente/invalide, POST /backup-btt d'abord");
        ESP_LOGE(TAG, "commit OTA refuse : cible '%s' porte encore BTT et aucune sauvegarde valide",
                 cible->label);
        return ESP_ERR_INVALID_STATE;
    }

    /* Pre-verification de taille (defense en profondeur, revue T5) : si le
       client annonce une image plus grande que le slot cible, la refuser AVANT
       esp_ota_begin() -- qui, lui, efface deja la cible (donc BTT au tout
       premier commit). Inutile d'effacer BTT pour une image qui ne rentre de
       toute facon pas. content_len == 0 (transfert chunked, taille inconnue) :
       on laisse passer, esp_ota_write() detectera le debordement plus bas et on
       abandonnera proprement (image non bootable, la sauvegarde couvre BTT). */
    if (req->content_len > 0 && (size_t)req->content_len > cible->size) {
        ota_msg(msg, msg_taille, "refuse : image (%u o) plus grande que le slot '%s' (%u o)",
                (unsigned)req->content_len, cible->label, (unsigned)cible->size);
        ESP_LOGE(TAG, "commit OTA refuse : image %u o > slot '%s' %u o",
                 (unsigned)req->content_len, cible->label, (unsigned)cible->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(cible, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        ota_msg(msg, msg_taille, "erreur : esp_ota_begin sur '%s' (%s)", cible->label, esp_err_to_name(err));
        ESP_LOGE(TAG, "esp_ota_begin echoue sur '%s' : %s", cible->label, esp_err_to_name(err));
        return err;
    }

    static uint8_t tampon[OTA_TAILLE_BLOC];
    bool premier_bloc = true;
    size_t position = 0;
    uint32_t compteur_blocs = 0;
    uint32_t timeouts_consecutifs = 0;
    esp_err_t erreur_flux = ESP_OK;

    while (position < taille_annoncee) {
        size_t reste = taille_annoncee - position;
        size_t a_lire = reste > sizeof(tampon) ? sizeof(tampon) : reste;

        int recu = httpd_req_recv(req, (char *)tampon, a_lire);
        if (recu <= 0) {
            if (recu == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Meme tolerance que ota_verifier_flux() : un client lent
                   n'est pas forcement fautif. */
                if (++timeouts_consecutifs >= OTA_TIMEOUTS_CONSECUTIFS_MAX) {
                    erreur_flux = ESP_ERR_TIMEOUT;
                    break;
                }
                continue;
            }
            /* recu == 0 (connexion fermee cote client) ou autre erreur
               negative : flux interrompu. */
            erreur_flux = ESP_FAIL;
            break;
        }
        timeouts_consecutifs = 0;

        if (premier_bloc) {
            /* Magic ESP (0xE9) verifie sur le TOUT PREMIER octet recu, AVANT
               le moindre esp_ota_write() : un flux qui commence mal est
               abandonne sans avoir ecrit un seul octet dans la cible.
               esp_ota_abort() plus bas n'annule que ce qu'esp_ota_begin() a
               deja prepare (effacement d'entete interne a esp_ota_ops) --
               sans consequence, la cible est de toute facon le slot INACTIF,
               jamais celui en cours d'execution. */
            if (tampon[0] != 0xE9u) {
                erreur_flux = ESP_ERR_INVALID_VERSION;
                break;
            }
            premier_bloc = false;
        }

        err = esp_ota_write(handle, tampon, (size_t)recu);
        if (err != ESP_OK) {
            erreur_flux = err;
            break;
        }
        position += (size_t)recu;

        /* Cede la main a l'ordonnanceur tous les OTA_CEDER_TOUS_LES_N_BLOCS
           blocs -- voir le commentaire de tete de cette fonction : ce flux
           tourne sur la tache httpd, jamais sur une tache dediee, donc cette
           cession reguliere est le SEUL mecanisme qui laisse les taches IDLE
           s'executer (et donc le chien de garde des taches etre nourri)
           pendant les quelques secondes de ce transfert de ~4,5 Mio. */
        if (++compteur_blocs % OTA_CEDER_TOUS_LES_N_BLOCS == 0) {
            vTaskDelay(1);
        }
    }

    if (erreur_flux != ESP_OK) {
        /* Echec a toute etape de reception/ecriture : abort, message, PAS de
           set_boot -- esp_ota_end() n'est meme pas atteint. */
        esp_ota_abort(handle);
        const char *cause;
        if (erreur_flux == ESP_ERR_INVALID_VERSION) {
            cause = "magic invalide (premier octet != 0xE9)";
        } else if (erreur_flux == ESP_ERR_TIMEOUT) {
            cause = "timeout de reception";
        } else if (erreur_flux == ESP_FAIL) {
            cause = "connexion fermee";
        } else {
            cause = esp_err_to_name(erreur_flux);
        }
        ota_msg(msg, msg_taille, "erreur : ecriture interrompue a %u/%u octets (%s)",
                (unsigned)position, (unsigned)taille_annoncee, cause);
        ESP_LOGE(TAG, "commit OTA : ecriture interrompue a %u/%u octets (%s)",
                 (unsigned)position, (unsigned)taille_annoncee, cause);
        return erreur_flux;
    }

    /* esp_ota_end() est la SEULE porte de validation avant set_boot_partition
       ci-dessous : elle verifie que l'image ecrite est structurellement
       complete et coherente (taille, en-tete, sommes de controle internes a
       esp_ota_ops). Si elle echoue, l'image ecrite ne doit JAMAIS devenir la
       cible de demarrage -- esp_ota_set_boot_partition() n'est pas appele
       dans ce cas. */
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ota_msg(msg, msg_taille, "erreur : validation de l'image echouee (esp_ota_end : %s)",
                esp_err_to_name(err));
        ESP_LOGE(TAG, "commit OTA : esp_ota_end echoue : %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(cible);
    if (err != ESP_OK) {
        ota_msg(msg, msg_taille, "erreur : esp_ota_set_boot_partition echoue (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "commit OTA : esp_ota_set_boot_partition echoue : %s", esp_err_to_name(err));
        return err;
    }

    if (cible->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        /* Ce commit vient d'ecrire app0 pour la premiere fois (la garde
           ci-dessus l'exigeait) : poser le drapeau pour que les commits
           SUIVANTS ciblant app0 n'exigent plus une sauvegarde BTT -- BTT
           n'existe alors plus dans app0, la sauvegarde spiffs (si presente)
           reste la seule trace de l'original. */
        esp_err_t err_nvs = ota_nvs_btt_ecrase_marquer();
        if (err_nvs != ESP_OK) {
            ESP_LOGW(TAG, "drapeau NVS %s non pose (%s) -- sans consequence sur ce commit",
                     OTA_NVS_CLE_BTT_ECRASE, esp_err_to_name(err_nvs));
        }
    }

    /* Filet de secours (rescue.c, jamais reimplemente ici). NE PAS surestimer
       CET appel-ci : le minuteur qu'il arme vit en RAM et est detruit par
       l'esp_restart() ~500 ms plus bas, bien avant CONFIG_KTOUCH_RESCUE_TIMEOUT_MS
       -- il ne se declenchera donc quasiment jamais pour la nouvelle image. La
       VRAIE protection de rollback de la nouvelle image vient de SON PROPRE
       demarrage : le compteur de boot en memoire RTC (qui, lui, survit au
       redemarrage) plus le rescue_arm() rearme par app_main.c a chaque boot. Si
       la nouvelle image boucle au demarrage ou ne joint jamais le reseau, c'est
       ce compteur (RESCUE_DEMARRAGES_MAX) qui rebascule vers le slot precedent
       (le firmware qui tourne en ce moment). On garde cet arm-ci comme simple
       redondance a cout nul pour l'etroite fenetre avant le reboot. Le rollback
       natif d'IDF (esp_ota_mark_app_valid_cancel_rollback et consorts) N'EST PAS
       utilise -- rescue.c reste le seul mecanisme de secours de ce firmware. */
    esp_err_t err_rescue = rescue_arm(CONFIG_KTOUCH_RESCUE_TIMEOUT_MS);
    if (err_rescue != ESP_OK) {
        ESP_LOGW(TAG,
                 "rescue_arm a echoue apres le commit OTA (%s) -- le sauvetage sera quand meme "
                 "arme au prochain demarrage normal par app_main.c",
                 esp_err_to_name(err_rescue));
    }

    ota_msg(msg, msg_taille, "OK : image ecrite dans '%s' (%u octets), demarrage programme dessus",
            cible->label, (unsigned)position);
    ESP_LOGW(TAG, "commit OTA reussi : '%s' (%u octets), redemarrage imminent", cible->label,
             (unsigned)position);
    return ESP_OK;
}
