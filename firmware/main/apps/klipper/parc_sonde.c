/* Implémentation de parc_sonde.h -- voir ce header pour le contrat.
 * Structure de tâche : patron usb_scan.c EXACT (pérenne, pile PSRAM,
 * sémaphore, création paresseuse à RAM saine). */
#include "parc_sonde.h"

#ifdef ESP_PLATFORM
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "journal.h"
#include "parc_imprimantes.h"
#include "parc_parse.h"

static const char *TAG = "parc_sonde";

/* 8192 comme TOUTES les tâches esp_http_client du dépôt (miniature_fetch,
 * usb_upload_http -- revue du 2026-08-15, L10) : getaddrinfo/lwip sur un
 * hôte en NOM + les cadres internes du client dépassent vite 6 Kio, et la
 * pile étant en PSRAM, économiser 2 Kio ne libérait AUCUNE RAM interne. */
#define PARC_SONDE_TASK_STACK   8192
#define PARC_SONDE_TASK_PRIO    (tskIDLE_PRIORITY + 5)
#define PARC_SONDE_TIMEOUT_MS   1500 /* demande utilisateur : court, jamais 10 s */
#define PARC_SONDE_PAUSE_MS     1000 /* entre deux imprimantes sondées */
#define PARC_SONDE_REPONSE_MAX  4096 /* la réponse query 4 objets fait ~1-2 Ko */

/* La requête : les 4 objets exacts que parc_parse.h sait lire. */
#define PARC_SONDE_CHEMIN "/printer/objects/query?print_stats&extruder&heater_bed&display_status"

static SemaphoreHandle_t s_reveil;
static TaskHandle_t      s_tache;
static volatile bool     s_active;   /* lu par la tâche, écrit par l'écran (tâche LVGL) */
static char             *s_reponse;  /* scratch PSRAM persistant, alloué au démarrage paresseux */
static uint8_t           s_prochain; /* curseur round-robin, propre à la tâche */

/* Sonde UNE imprimante : GET bloquant (timeout court), lecture bornée,
 * parse, publication. Échec réseau/HTTP => publier atteignable=false (la
 * tuile passe "unreachable" en <= 1,5 s, jamais un gel d'écran : tout se
 * passe sur CETTE tâche, la tâche LVGL ne fait que lire le store). */
static void sonder_une(uint8_t indice, const parc_entree_t *entree)
{
    parc_etat_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.sonde = true;
    etat.atteignable = false;

    /* Crochets IPv6 (revue du 2026-08-15, L4) : même branche que les quatre
       autres constructeurs d'URL du dépôt (backend_moonraker.c,
       miniature_fetch.c, moonraker_ws.c, usb_upload_http.c) -- hote_parse()
       stocke une IPv6 SANS crochets, l'URL doit les remettre. */
    char url[192];
    int ecrit;
    if (strchr(entree->hote.adresse, ':') != NULL) {
        ecrit = snprintf(url, sizeof(url), "http://[%s]:%u" PARC_SONDE_CHEMIN,
                         entree->hote.adresse, (unsigned)entree->hote.port);
    } else {
        ecrit = snprintf(url, sizeof(url), "http://%s:%u" PARC_SONDE_CHEMIN,
                         entree->hote.adresse, (unsigned)entree->hote.port);
    }
    if (ecrit < 0 || (size_t)ecrit >= sizeof(url)) {
        parc_etat_publier(indice, &etat);
        return;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = PARC_SONDE_TIMEOUT_MS,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        parc_etat_publier(indice, &etat);
        return;
    }

    /* Lecture en flux (open/fetch/read) plutôt que esp_http_client_perform
       + gros tampon interne : la réponse atterrit directement dans le
       scratch PSRAM, bornée. */
    int longueur = -1;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        int statut = esp_http_client_get_status_code(client);
        if (statut == 200) {
            longueur = esp_http_client_read_response(client, s_reponse, PARC_SONDE_REPONSE_MAX - 1);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (longueur > 0) {
        s_reponse[longueur] = '\0';
        if (!parc_parse_reponse(s_reponse, (size_t)longueur, &etat)) {
            /* JSON inattendu : joignable au sens TCP mais réponse
               inexploitable -- affiché injoignable, plus honnête qu'un
               zéro partout étiqueté mesuré. */
            memset(&etat, 0, sizeof(etat));
            etat.sonde = true;
            etat.atteignable = false;
        }
    }
    parc_etat_publier(indice, &etat);
}

static void parc_sonde_tache(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_active) {
            /* Désarmée : dormir jusqu'au prochain activer(true). Le
               sémaphore binaire coalesce les armements répétés. */
            xSemaphoreTake(s_reveil, portMAX_DELAY);
            continue;
        }

        parc_config_t config;
        parc_config_lire(&config);

        /* Prochaine entrée sondable en round-robin : configurée, adresse
           non vide, PAS l'active. Aucune candidate (parc a 0/1 entrée) :
           attendre un peu puis revérifier (la config peut changer). */
        uint8_t candidate = PARC_MAX;
        for (uint8_t essais = 0; essais < PARC_MAX; essais++) {
            uint8_t indice = (uint8_t)((s_prochain + essais) % PARC_MAX);
            if (indice >= config.nb || indice == config.actif ||
                config.entrees[indice].hote.adresse[0] == '\0') {
                continue;
            }
            candidate = indice;
            break;
        }

        if (candidate == PARC_MAX) {
            vTaskDelay(pdMS_TO_TICKS(PARC_SONDE_PAUSE_MS));
            continue;
        }

        sonder_une(candidate, &config.entrees[candidate]);
        s_prochain = (uint8_t)((candidate + 1) % PARC_MAX);
        vTaskDelay(pdMS_TO_TICKS(PARC_SONDE_PAUSE_MS));
    }
}

void parc_sonde_demarrage_paresseux(void)
{
    static bool s_demarre = false;
    if (s_demarre) {
        return;
    }

    if (s_reponse == NULL) {
        s_reponse = (char *)heap_caps_malloc(PARC_SONDE_REPONSE_MAX, MALLOC_CAP_SPIRAM);
        if (s_reponse == NULL) {
            JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour le tampon de sonde ; sonde non demarree");
            return;
        }
    }
    if (s_reveil == NULL) {
        s_reveil = xSemaphoreCreateBinary();
        if (s_reveil == NULL) {
            JOURNAL_ERREUR(TAG, "creation du semaphore de sonde echouee ; sonde non demarree");
            return;
        }
    }
    if (s_tache == NULL) {
        /* Pile en RAM INTERNE (retour arriere du 2026-08-15 apres-midi) :
           l'experience "pile en PSRAM via xTaskCreateWithCaps" est
           l'element commun aux 4 crashs WDT(7) muets (RTC watchdog, les
           deux coeurs interruptions coupees, coredump jamais ecrit) --
           une tache a pile PSRAM spinnant en section critique pendant une
           fenetre cache-flash-desactive de l'autre coeur est le suspect
           structurel. La marge interne (~87 Ko au repos depuis les fix du
           14/08) absorbe largement ces 8 Ko. */
        BaseType_t cree = xTaskCreate(parc_sonde_tache, "parc_sonde", PARC_SONDE_TASK_STACK,
                                      NULL, PARC_SONDE_TASK_PRIO, &s_tache);
        if (cree != pdPASS) {
            s_tache = NULL;
            JOURNAL_ERREUR(TAG, "creation de la tache de sonde echouee ; sonde non demarree");
            return;
        }
    }
    s_demarre = true;
}

void parc_sonde_activer(bool active)
{
    s_active = active;
    if (active && s_reveil != NULL && s_tache != NULL) {
        xSemaphoreGive(s_reveil);
    }
}
#else
void parc_sonde_demarrage_paresseux(void)
{
    /* no-op host : esp_http_client n'existe pas sur PC, même politique que
       usb_scan.c -- le parseur (parc_parse.c) est, lui, testé host. */
}

void parc_sonde_activer(bool active)
{
    (void)active;
}
#endif
