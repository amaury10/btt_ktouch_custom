/* Implémentation : voir miniature_fetch.h pour le contrat. Réutilise
 * délibérément le PATRON des appels HTTP existants de backend_moonraker.c
 * (moonraker_requete()/moonraker_construire_url(), voir leurs commentaires)
 * -- même construction d'URL (crochets IPv6), même vérification de statut
 * AVANT toute question de troncature, même distinction "délai dépassé" vs
 * "connexion interrompue" -- adaptée ici à UNE requête ponctuelle sur SA
 * PROPRE tâche dédiée plutôt qu'un client réutilisé sur un cycle périodique
 * (ce fetch n'arrive qu'une fois par impression, pas une fois par seconde :
 * pas de justification à retenir un esp_http_client_handle_t entre deux
 * appels comme le fait backend_moonraker.c). */
#include "miniature_fetch.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "journal.h"
#include "miniature.h"

static const char *TAG = "miniature_fetch";

/* Même délai par opération et même plafond total que backend_moonraker.c
 * (MOONRAKER_DELAI_MS/MOONRAKER_DELAI_TOTAL_MS) -- un GET de miniature reste
 * une requête HTTP ordinaire sur le même LAN, aucune raison de lui donner un
 * budget différent. */
#define MINIATURE_FETCH_DELAI_MS       3000
#define MINIATURE_FETCH_DELAI_TOTAL_MS (2 * MINIATURE_FETCH_DELAI_MS)

/* "http://" + hôte + ":" + port + "/server/files/gcodes/" + chemin encodé
 * (jusqu'à 3x sa longueur, voir l'encodeur ci-dessous) + le nul. Marge ronde,
 * même esprit que MOONRAKER_URL_OCTETS dans backend_moonraker.c. */
#define MINIATURE_FETCH_URL_OCTETS (BACKEND_HOTE_LONGUEUR_MAX + (MINIATURE_CHEMIN_MAX * 3) + 128)

typedef struct {
    backend_hote_t hote;
    /* MINIATURE_CHEMIN_MAX (miniature.h), PAS une constante locale distincte
     * -- voir son commentaire : DOIT rester identique au tampon que
     * moonraker_ws.c utilise pour construire ce même chemin, sous peine
     * d'une troncature silencieuse d'un bout à l'autre de cet appel. */
    char           chemin_miniature[MINIATURE_CHEMIN_MAX];
    char           fichier_associe[MINIATURE_NOM_MAX];
} miniature_fetch_ctx_t;

/* Codage pourcentage (RFC 3986) pour un SEGMENT DE CHEMIN d'URL -- COPIE
 * délibérée de moonraker_url_encoder_script() (backend_moonraker.c), ÉCART
 * volontaire sur un seul point : '/' reste ICI un caractère direct-sûr (le
 * chemin miniature contient légitimement des séparateurs de sous-dossier,
 * voir miniature_construire_chemin()), alors que moonraker_url_encoder_script()
 * encode tout ce qui n'est pas alphanumérique/-_.~ parce qu'un script gcode
 * n'a, lui, jamais de '/' significatif. Rend false si `sortie` est trop court
 * (jamais de troncature silencieuse, même règle que le reste de ce dépôt). */
static bool miniature_url_encoder_chemin(const char *chemin, char *sortie, size_t taille)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (const char *p = chemin; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        bool direct = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                      c == '~' || c == '/';
        if (direct) {
            if (pos + 1 >= taille) {
                return false;
            }
            sortie[pos++] = (char)c;
        } else {
            if (pos + 3 >= taille) {
                return false;
            }
            sortie[pos++] = '%';
            sortie[pos++] = HEX[(c >> 4) & 0x0Fu];
            sortie[pos++] = HEX[c & 0x0Fu];
        }
    }
    if (pos >= taille) {
        return false;
    }
    sortie[pos] = '\0';
    return true;
}

/* COPIE de moonraker_construire_url() (backend_moonraker.c) -- même
 * détection IPv6 par ':' (voir son commentaire de tête pour la justification
 * RFC 3986 §3.2.2), dupliquée plutôt que partagée : backend_moonraker.c ne
 * l'expose pas (`static`), et une seule ligne de plus par appelant ne
 * justifie pas de créer un utilitaire partagé pour ça (même choix que
 * plusieurs "COPIE de ..." déjà dans ce dépôt, ex. envoyer_requete_fichiers()
 * dans moonraker_ws.c). */
static void construire_url(char *tampon, size_t taille, const backend_hote_t *hote,
                           const char *chemin_encode)
{
    if (strchr(hote->adresse, ':') != NULL) {
        snprintf(tampon, taille, "http://[%s]:%u/server/files/gcodes/%s", hote->adresse,
                 (unsigned)hote->port, chemin_encode);
    } else {
        snprintf(tampon, taille, "http://%s:%u/server/files/gcodes/%s", hote->adresse,
                 (unsigned)hote->port, chemin_encode);
    }
}

/* Fait tout le travail réseau -- tourne sur la tâche dédiée créée par
 * miniature_fetch_lancer(), jamais sur la tâche WS ni la tâche LVGL. Dépose
 * TOUJOURS un résultat (PRÊT ou ÉCHEC) dans le store avant de rendre la main,
 * sur TOUS les chemins de sortie -- voir chaque `goto echec`. */
static void miniature_fetch_tache(void *arg)
{
    miniature_fetch_ctx_t *ctx = (miniature_fetch_ctx_t *)arg;

    char chemin_encode[MINIATURE_CHEMIN_MAX * 3];
    char url[MINIATURE_FETCH_URL_OCTETS];
    esp_http_client_handle_t client = NULL;
    uint8_t *scratch = NULL;    /* tampon de reception, taille max, PSRAM */
    uint8_t *definitif = NULL;  /* copie ajustee a la taille reelle, transferee au store */
    size_t total = 0;

    if (!miniature_url_encoder_chemin(ctx->chemin_miniature, chemin_encode, sizeof(chemin_encode))) {
        JOURNAL_ALERTE(TAG, "codage URL du chemin miniature impossible (trop long) pour %s",
                       ctx->fichier_associe);
        goto echec;
    }
    construire_url(url, sizeof(url), &ctx->hote, chemin_encode);

    /* Tampon de reception en PSRAM UNIQUEMENT -- jamais la RAM interne,
     * jamais une pile de tache (priorite absolue de surete memoire de cette
     * feature, voir miniature.h). Taille MAX bornee (MINIATURE_TAILLE_MAX_OCTETS,
     * voir miniature.h) : un flux qui la depasse est ABANDONNE proprement
     * (voir la boucle de lecture plus bas), jamais grandi -- meme politique
     * que MOONRAKER_TAMPON_OCTETS dans backend_moonraker.c. */
    scratch = (uint8_t *)heap_caps_malloc(MINIATURE_TAILLE_MAX_OCTETS, MALLOC_CAP_SPIRAM);
    if (scratch == NULL) {
        JOURNAL_ALERTE(TAG, "PSRAM indisponible pour le tampon de reception (miniature %s)",
                       ctx->fichier_associe);
        goto echec;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = MINIATURE_FETCH_DELAI_MS,
    };
    client = esp_http_client_init(&config);
    if (client == NULL) {
        JOURNAL_ALERTE(TAG, "esp_http_client_init a echoue (miniature %s)", ctx->fichier_associe);
        goto echec;
    }

    int64_t debut_us = esp_timer_get_time();
    esp_err_t erreur = esp_http_client_open(client, 0);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "connexion a %s impossible (%s)", url, esp_err_to_name(erreur));
        goto echec;
    }

    (void)esp_http_client_fetch_headers(client);

    bool depasse = ((esp_timer_get_time() - debut_us) / 1000) >= MINIATURE_FETCH_DELAI_TOTAL_MS;
    while (!depasse && total < MINIATURE_TAILLE_MAX_OCTETS) {
        int lu = esp_http_client_read(client, (char *)(scratch + total),
                                       MINIATURE_TAILLE_MAX_OCTETS - total);
        if (lu <= 0) {
            break;
        }
        total += (size_t)lu;
        depasse = ((esp_timer_get_time() - debut_us) / 1000) >= MINIATURE_FETCH_DELAI_TOTAL_MS;
        /* Cede regulierement la main -- meme discipline defensive que
         * ota.c (OTA_CEDER_TOUS_LES_N_BLOCS), meme si un thumbnail de
         * quelques dizaines de Ko traverse cette boucle bien plus vite
         * qu'une image de plusieurs Mio. */
        vTaskDelay(1);
    }

    bool complete = esp_http_client_is_complete_data_received(client);
    int statut = esp_http_client_get_status_code(client);
    esp_http_client_close(client);

    if (statut < 200 || statut >= 300) {
        JOURNAL_ALERTE(TAG, "statut HTTP %d sur %s (miniature %s)", statut, url, ctx->fichier_associe);
        goto echec;
    }
    if (depasse) {
        JOURNAL_ALERTE(TAG, "delai total de %d ms depasse (miniature %s, %u octets recus)",
                       MINIATURE_FETCH_DELAI_TOTAL_MS, ctx->fichier_associe, (unsigned)total);
        goto echec;
    }
    if (!complete) {
        if (total >= MINIATURE_TAILLE_MAX_OCTETS) {
            JOURNAL_ALERTE(TAG, "miniature %s au-dela de %u octets ; abandonnee",
                           ctx->fichier_associe, (unsigned)MINIATURE_TAILLE_MAX_OCTETS);
        } else {
            JOURNAL_ALERTE(TAG, "connexion interrompue apres %u octets (miniature %s)",
                           (unsigned)total, ctx->fichier_associe);
        }
        goto echec;
    }
    if (total == 0) {
        JOURNAL_ALERTE(TAG, "reponse vide (miniature %s)", ctx->fichier_associe);
        goto echec;
    }

    /* Copie ajustee a la taille REELLE avant depot dans le store : le
     * tampon MAX (MINIATURE_TAILLE_MAX_OCTETS, 64 Kio) ne doit pas rester
     * vivant en PSRAM pour toute la duree de l'impression si le PNG reel
     * est bien plus petit (le cas courant -- un thumbnail <=160 px pese
     * generalement quelques Ko). */
    definitif = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (definitif == NULL) {
        JOURNAL_ALERTE(TAG, "PSRAM indisponible pour la copie definitive (miniature %s)",
                       ctx->fichier_associe);
        goto echec;
    }
    memcpy(definitif, scratch, total);
    heap_caps_free(scratch);
    scratch = NULL;

    /* miniature_poser_prete() prend possession de `definitif` -- ne JAMAIS
     * le liberer ici apres cet appel, quel que soit le resultat (accepte ou
     * rejete silencieusement pour cause de fichier perime : dans les deux
     * cas c'est miniature.c qui en dispose desormais, voir miniature.h). */
    miniature_poser_prete(ctx->fichier_associe, definitif, total, 0, 0);
    JOURNAL_INFO(TAG, "miniature %s recuperee (%u octets)", ctx->fichier_associe, (unsigned)total);
    goto fin;

echec:
    miniature_poser_echec(ctx->fichier_associe);
fin:
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    if (scratch != NULL) {
        heap_caps_free(scratch);
    }
    free(ctx);
    vTaskDelete(NULL);
}

void miniature_fetch_lancer(const backend_hote_t *hote, const char *chemin_miniature,
                            const char *fichier_associe)
{
    if (hote == NULL || chemin_miniature == NULL || fichier_associe == NULL ||
        fichier_associe[0] == '\0') {
        return;
    }

    /* Contexte alloué sur le tas (RAM interne -- une poignée d'octets,
     * jamais de PSRAM nécessaire pour de simples chaînes de métadonnées) :
     * la tâche dédiée ne retient jamais un pointeur vers la pile de
     * l'appelant (la tâche WS), qui aura repris son travail bien avant que
     * ce fetch ne se termine. Libéré par la tâche elle-même en sortie (voir
     * miniature_fetch_tache()). */
    miniature_fetch_ctx_t *ctx = (miniature_fetch_ctx_t *)malloc(sizeof(*ctx));
    if (ctx == NULL) {
        JOURNAL_ALERTE(TAG, "contexte de fetch miniature indisponible (memoire epuisee)");
        miniature_poser_echec(fichier_associe);
        return;
    }
    ctx->hote = *hote;
    snprintf(ctx->chemin_miniature, sizeof(ctx->chemin_miniature), "%s", chemin_miniature);
    snprintf(ctx->fichier_associe, sizeof(ctx->fichier_associe), "%s", fichier_associe);

    /* Pile 8192 (comme ota.c) : porte esp_http_client (structures internes,
     * tampons d'emission/reception d'entetes) -- le tampon de RECEPTION du
     * corps, lui, est en PSRAM (voir miniature_fetch_tache()), jamais sur
     * cette pile. Priorite identique a ota.c/rescue.c : nettement au-dessus
     * d'IDLE, sans rivaliser avec les taches internes du pilote WiFi ni la
     * tache WS elle-meme. */
    BaseType_t cree = xTaskCreate(miniature_fetch_tache, "miniature_fetch", 8192, ctx,
                                   tskIDLE_PRIORITY + 5, NULL);
    if (cree != pdPASS) {
        JOURNAL_ALERTE(TAG, "tache de fetch miniature non creee (memoire epuisee)");
        free(ctx);
        miniature_poser_echec(fichier_associe);
    }
}
