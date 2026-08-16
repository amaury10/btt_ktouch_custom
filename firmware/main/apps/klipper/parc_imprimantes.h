/* Store du parc d'imprimantes (spec 2026-08-15-gestion-parc-design.md) :
 * la CONFIGURATION (jusqu'à PARC_MAX entrées nom + hôte, une active) et les
 * ÉTATS SONDÉS (remplis par la sonde séquentielle parc_sonde.h, lus par
 * l'écran Parc).
 *
 * POURQUOI un store dédié, même patron que usb_fichiers.h : écrit par la
 * tâche de sonde, lu par la tâche LVGL — verrou COURT + génération, instance
 * unique en PSRAM (jamais de .bss RAM interne, leçons des 14-15/08). La
 * persistance NVS (namespace "parc") n'existe que côté ESP ; côté
 * host-test/simulateur le store vit en RAM seule, ce qui suffit aux tests.
 *
 * L'imprimante ACTIVE reste par ailleurs recopiée dans le réglage hôte
 * historique (ui_reglages_definir_hote()) au moment d'une bascule : c'est
 * LUI que app_main lit au boot pour démarrer le backend — ce store n'est
 * jamais une seconde source de vérité pour la connexion courante. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "backend.h" /* backend_hote_t -- compilable PC, voir source_reglages.h */
#include "esp_err.h"

#define PARC_MAX     6
#define PARC_NOM_MAX 24

typedef struct {
    char           nom[PARC_NOM_MAX]; /* libellé affiché ("CR-10 S5") ; NUL-terminé */
    backend_hote_t hote;              /* adresse+port, produit par ecran_configuration_valider() */
} parc_entree_t;

typedef struct {
    parc_entree_t entrees[PARC_MAX];
    uint8_t       nb;    /* entrées valides dans entrees[0..nb-1] */
    uint8_t       actif; /* indice de l'imprimante active, < nb (0 si nb == 0) */
} parc_config_t;

/* État d'UNE imprimante vu par la sonde -- volontairement minuscule (pas un
 * etat_klipper_t : la sonde ne parse que 4 champs, voir parc_parse.h). */
typedef struct {
    bool    sonde;               /* faux tant qu'aucun tour de sonde n'a touché cette entrée */
    bool    atteignable;         /* faux = timeout/refus au dernier tour */
    char    etat[16];            /* print_stats.state ("printing", "standby", ...) */
    float   buse;                /* °C */
    float   lit;                 /* °C */
    uint8_t progression_pct;     /* 0..100 */
} parc_etat_t;

/* Copie la configuration courante (sous verrou). `dest` NULL = no-op. */
void parc_config_lire(parc_config_t *dest);

/* Remplace la configuration (bornes défensives : nb rabattu à PARC_MAX,
 * actif >= nb rabattu à 0), persiste en NVS côté ESP, +1 génération.
 * ESP_ERR_INVALID_ARG si `config` est NULL. */
esp_err_t parc_config_definir(const parc_config_t *config);

/* Retire l'entrée `indice` de `*config` : décale les entrées suivantes d'un
 * cran, décrémente `nb`, remet à zéro la case libérée, et décrémente `actif`
 * si l'entrée retirée le PRÉCÉDAIT (sans quoi le marqueur d'imprimante active
 * désignerait silencieusement une autre machine après le décalage).
 *
 * Fonction PURE : elle travaille sur la copie fournie, ne touche NI au store
 * NI à la NVS -- l'appelant persiste ensuite avec parc_config_definir(), même
 * schéma que l'ajout dans ecran_parc.c. C'est ce qui la rend testable
 * (host-test/tests/test_parc.c).
 *
 * ESP_ERR_INVALID_ARG : `config` NULL, ou `indice` >= `config->nb` (donc tout
 * indice sur un parc vide).
 *
 * ESP_ERR_INVALID_STATE : `indice` désigne l'imprimante ACTIVE alors que le
 * parc en compte d'autres -- refus délibéré (décision du 2026-08-16) : la
 * retirer imposerait de réécrire l'hôte de boot ET de redémarrer la dalle,
 * effet de bord qu'un geste de suppression ne doit pas déclencher. L'appelant
 * invite à basculer d'abord. `*config` n'est alors PAS modifiée.
 * SEULE exception : l'active est la dernière entrée du parc -- la retirer vide
 * le parc (`nb` = 0), il n'y a rien vers quoi rebasculer. L'hôte de boot n'est
 * pas du ressort de cette fonction. */
esp_err_t parc_config_retirer(parc_config_t *config, uint8_t indice);

/* Publie l'état sondé de l'entrée `indice` (+1 génération). Indice hors
 * bornes ou `etat` NULL : no-op silencieux (la sonde borne déjà). */
void parc_etat_publier(uint8_t indice, const parc_etat_t *etat);

/* Copie les PARC_MAX états (sous verrou) ; les entrées jamais sondées ont
 * `sonde == false` et le reste à zéro. `dest` NULL = no-op. */
void parc_etats_lire(parc_etat_t dest[PARC_MAX]);

/* Compteur monotone, +1 à chaque parc_config_definir()/parc_etat_publier()
 * -- même idiome que usb_fichiers_generation() (l'écran ne redessine que
 * sur changement). */
uint32_t parc_generation(void);

/* Charge la configuration depuis la NVS (ESP ; no-op RAM côté host) puis,
 * si le parc est VIDE, migre l'hôte historique (ui_reglages_hote()) en
 * entrée 0 "Printer 1" -- un appareil déjà configuré voit son imprimante
 * apparaître dans le parc sans aucune saisie. À appeler une fois au boot
 * (app_main), après reglages_charger(). */
void parc_charger(void);
