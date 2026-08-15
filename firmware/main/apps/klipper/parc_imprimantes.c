/* Implémentation de parc_imprimantes.h -- voir ce header pour le contrat et
 * le POURQUOI. Verrou/PSRAM : copie EXACTE du patron de usb_fichiers.c
 * (portMUX, sections critiques COURTES limitées aux copies, instance PSRAM
 * paresseuse sans repli RAM interne ; no-op hors ESP_PLATFORM). */
#include "parc_imprimantes.h"

#include <stdio.h>
#include <string.h>

#include "journal.h"
#include "source_reglages.h" /* ui_reglages_hote() -- migration entrée 0, voir parc_charger() */

static const char *TAG = "parc";

typedef struct {
    parc_config_t config;
    parc_etat_t   etats[PARC_MAX];
} parc_store_t;

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hote_parse.h" /* relecture de la cle hote unique, voir parc_nvs_charger() */
#include "nvs.h"
#include "nvs_flash.h"

static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
#define VERROU_PRENDRE() portENTER_CRITICAL(&g_verrou)
#define VERROU_RENDRE()  portEXIT_CRITICAL(&g_verrou)

static parc_store_t *g_store; /* PSRAM ; NULL tant que non allouée OU après échec définitif */
static bool          g_echec;

static parc_store_t *store_obtenir(void)
{
    if (g_store != NULL) {
        return g_store;
    }
    if (g_echec) {
        return NULL;
    }
    parc_store_t *tampon = (parc_store_t *)heap_caps_malloc(sizeof(parc_store_t), MALLOC_CAP_SPIRAM);
    if (tampon == NULL) {
        JOURNAL_ERREUR(TAG, "heap_caps_malloc(PSRAM) a echoue pour le store du parc (%u octets)",
                       (unsigned)sizeof(parc_store_t));
        g_echec = true;
        return NULL;
    }
    memset(tampon, 0, sizeof(*tampon));
    g_store = tampon;
    return g_store;
}

/* Persistance NVS, namespace dédié "parc" -- mêmes idiomes que reglages.c
 * (clés courtes, jamais fatal : un échec NVS laisse le parc en RAM,
 * journalisé). Appelée HORS verrou (la NVS peut bloquer). */
#define PARC_NVS_NAMESPACE "parc"

/* Sérialise un hôte en UNE chaîne "adresse:port" ("[adresse]:port" en IPv6)
 * relisible par hote_parse() -- revue du 2026-08-15, L7 : l'hôte éclaté sur
 * deux clés NVS n'est pas transactionnel (reglages.c documente exactement ce
 * piège pour son propre hôte), une coupure entre les deux appariait une
 * adresse neuve à un port périmé. */
static void parc_hote_serialiser(char *dest, size_t n, const backend_hote_t *hote)
{
    if (strchr(hote->adresse, ':') != NULL) {
        snprintf(dest, n, "[%s]:%u", hote->adresse, (unsigned)hote->port);
    } else {
        snprintf(dest, n, "%s:%u", hote->adresse, (unsigned)hote->port);
    }
}

static void parc_nvs_ecrire(const parc_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t erreur = nvs_open(PARC_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "nvs_open(%s) a echoue : %s", PARC_NVS_NAMESPACE, esp_err_to_name(erreur));
        return;
    }
    /* Tous les retours verifies (revue L7 : les ignorer laissait persister
       un "nb" orphelin de ses entrees sur une NVS pleine) -- premier echec
       journalise, l'ecriture continue (persister le reste vaut mieux que
       rien), le commit tente quoi qu'il arrive. */
    esp_err_t premier_echec = ESP_OK;
#define PARC_NVS_VERIFIER(appel)                                                                   \
    do {                                                                                           \
        esp_err_t erreur_nvs_ = (appel);                                                           \
        if (erreur_nvs_ != ESP_OK && premier_echec == ESP_OK) {                                    \
            premier_echec = erreur_nvs_;                                                           \
        }                                                                                          \
    } while (0)
    PARC_NVS_VERIFIER(nvs_set_u8(handle, "nb", config->nb));
    PARC_NVS_VERIFIER(nvs_set_u8(handle, "actif", config->actif));
    for (uint8_t i = 0; i < PARC_MAX; i++) {
        char cle[8];
        char hote_txt[BACKEND_HOTE_LONGUEUR_MAX + 16];
        snprintf(cle, sizeof(cle), "n%u", (unsigned)i);
        PARC_NVS_VERIFIER(nvs_set_str(handle, cle, config->entrees[i].nom));
        snprintf(cle, sizeof(cle), "h%u", (unsigned)i);
        parc_hote_serialiser(hote_txt, sizeof(hote_txt), &config->entrees[i].hote);
        PARC_NVS_VERIFIER(nvs_set_str(handle, cle, hote_txt));
    }
#undef PARC_NVS_VERIFIER
    if (premier_echec != ESP_OK) {
        JOURNAL_ALERTE(TAG, "ecriture NVS du parc incomplete : %s", esp_err_to_name(premier_echec));
    }
    erreur = nvs_commit(handle);
    if (erreur != ESP_OK) {
        JOURNAL_ALERTE(TAG, "nvs_commit(parc) a echoue : %s", esp_err_to_name(erreur));
    }
    nvs_close(handle);
}

static void parc_nvs_charger(parc_config_t *config)
{
    memset(config, 0, sizeof(*config));
    nvs_handle_t handle;
    if (nvs_open(PARC_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return; /* premier boot : namespace absent, parc vide -- migration ensuite */
    }
    nvs_get_u8(handle, "nb", &config->nb);
    nvs_get_u8(handle, "actif", &config->actif);
    for (uint8_t i = 0; i < PARC_MAX; i++) {
        char   cle[8];
        char   hote_txt[BACKEND_HOTE_LONGUEUR_MAX + 16] = "";
        size_t taille = sizeof(config->entrees[i].nom);
        snprintf(cle, sizeof(cle), "n%u", (unsigned)i);
        nvs_get_str(handle, cle, config->entrees[i].nom, &taille);
        taille = sizeof(hote_txt);
        snprintf(cle, sizeof(cle), "h%u", (unsigned)i);
        if (nvs_get_str(handle, cle, hote_txt, &taille) == ESP_OK && hote_txt[0] != '\0') {
            if (!hote_parse(hote_txt, &config->entrees[i].hote)) {
                /* Entrée illisible (flash corrompue ?) : hôte vide, la sonde
                   et l'écran la sautent -- jamais un hôte inventé. */
                memset(&config->entrees[i].hote, 0, sizeof(config->entrees[i].hote));
            }
        }
    }
    nvs_close(handle);
}
#else
#define VERROU_PRENDRE() ((void)0)
#define VERROU_RENDRE()  ((void)0)
static parc_store_t g_store_hote;
static parc_store_t *store_obtenir(void)
{
    return &g_store_hote;
}
static void parc_nvs_ecrire(const parc_config_t *config)
{
    (void)config; /* pas de NVS sur PC : RAM seule, suffisant pour les tests */
}
static void parc_nvs_charger(parc_config_t *config)
{
    memset(config, 0, sizeof(*config));
}
#endif

static uint32_t g_generation;

void parc_config_lire(parc_config_t *dest)
{
    if (dest == NULL) {
        return;
    }
    parc_store_t *store = store_obtenir();
    if (store == NULL) {
        memset(dest, 0, sizeof(*dest));
        return;
    }
    VERROU_PRENDRE();
    *dest = store->config;
    VERROU_RENDRE();
}

esp_err_t parc_config_definir(const parc_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    parc_store_t *store = store_obtenir();
    if (store == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Copie locale bornée AVANT le verrou (bornes + NUL-terminaisons hors
       section critique -- discipline "verrou court"). parc_config_t fait
       ~550 octets : acceptable sur les piles appelantes (tâche LVGL,
       sonde), rien à voir avec les 8,8 Ko d'usb_fichiers_t. */
    parc_config_t propre = *config;
    if (propre.nb > PARC_MAX) {
        propre.nb = PARC_MAX;
    }
    if (propre.actif >= propre.nb) {
        propre.actif = 0;
    }
    for (uint8_t i = 0; i < PARC_MAX; i++) {
        propre.entrees[i].nom[PARC_NOM_MAX - 1] = '\0';
        propre.entrees[i].hote.adresse[sizeof(propre.entrees[i].hote.adresse) - 1] = '\0';
    }

    VERROU_PRENDRE();
    store->config = propre;
    g_generation++;
    VERROU_RENDRE();

    parc_nvs_ecrire(&propre); /* hors verrou : la NVS peut bloquer */
    return ESP_OK;
}

void parc_etat_publier(uint8_t indice, const parc_etat_t *etat)
{
    if (indice >= PARC_MAX || etat == NULL) {
        return;
    }
    parc_store_t *store = store_obtenir();
    if (store == NULL) {
        return;
    }
    parc_etat_t propre = *etat;
    propre.etat[sizeof(propre.etat) - 1] = '\0';
    if (propre.progression_pct > 100) {
        propre.progression_pct = 100;
    }
    /* Génération SEULEMENT sur changement réel (revue du 2026-08-15, L9) :
       la sonde publie ~1 fois/s même sur un parc parfaitement immobile --
       sans ce memcmp, l'écran Parc re-labellisait toutes ses tuiles chaque
       seconde pour rien (famille du jank « clavier rame »). memcmp valable :
       la sonde construit toujours ses états par memset(0) + champs, aucun
       octet de bourrage indéterminé. */
    VERROU_PRENDRE();
    if (memcmp(&store->etats[indice], &propre, sizeof(propre)) != 0) {
        store->etats[indice] = propre;
        g_generation++;
    }
    VERROU_RENDRE();
}

void parc_etats_lire(parc_etat_t dest[PARC_MAX])
{
    if (dest == NULL) {
        return;
    }
    parc_store_t *store = store_obtenir();
    if (store == NULL) {
        memset(dest, 0, sizeof(parc_etat_t) * PARC_MAX);
        return;
    }
    VERROU_PRENDRE();
    memcpy(dest, store->etats, sizeof(parc_etat_t) * PARC_MAX);
    VERROU_RENDRE();
}

uint32_t parc_generation(void)
{
    uint32_t generation;
    VERROU_PRENDRE();
    generation = g_generation;
    VERROU_RENDRE();
    return generation;
}

void parc_charger(void)
{
    parc_config_t config;
    parc_nvs_charger(&config);

    /* Bornes defensives sur ce qui sort de la NVS (flash usée, autre
       firmware) -- mêmes règles que parc_config_definir(). */
    if (config.nb > PARC_MAX) {
        config.nb = PARC_MAX;
    }
    if (config.actif >= config.nb) {
        config.actif = 0;
    }

    /* Migration : parc vide MAIS un hôte historique configuré (l'appareil
       tournait avant cette feature) -> il devient l'entrée 0, active. Sans
       ça, l'écran Parc serait vide sur un appareil pourtant configuré. */
    bool a_persister = false;
    backend_hote_t historique;
    bool historique_valide = ui_reglages_hote(&historique);
    if (config.nb == 0) {
        if (historique_valide) {
            memset(&config.entrees[0], 0, sizeof(config.entrees[0]));
            snprintf(config.entrees[0].nom, sizeof(config.entrees[0].nom), "Printer 1");
            config.entrees[0].hote = historique;
            config.nb = 1;
            config.actif = 0;
            a_persister = true;
            JOURNAL_INFO(TAG, "migration : hote historique %s:%u -> entree 0 du parc",
                         historique.adresse, (unsigned)historique.port);
        }
    } else if (historique_valide &&
               (strcmp(config.entrees[config.actif].hote.adresse, historique.adresse) != 0 ||
                config.entrees[config.actif].hote.port != historique.port)) {
        /* Réconciliation (revue du 2026-08-15, L2) : le réglage hôte
           historique reste éditable par l'écran Configuration, qui ne
           connaît pas le parc -- SANS cette resynchronisation au boot, une
           IP corrigée là-bas était silencieusement annulée à la prochaine
           bascule aller-retour (l'entrée parc périmée écrasait la
           correction). Le réglage historique est LA source de vérité du
           boot : l'entrée active du parc s'aligne dessus, jamais l'inverse
           ici. */
        JOURNAL_INFO(TAG, "reconciliation : hote de l'entree active realigne sur %s:%u",
                     historique.adresse, (unsigned)historique.port);
        config.entrees[config.actif].hote = historique;
        a_persister = true;
    }

    if (a_persister) {
        parc_nvs_ecrire(&config);
    }

    parc_store_t *store = store_obtenir();
    if (store == NULL) {
        return;
    }
    VERROU_PRENDRE();
    store->config = config;
    g_generation++;
    VERROU_RENDRE();
}
