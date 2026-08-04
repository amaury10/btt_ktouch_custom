/* Ecran Power (feature "Power devices Moonraker", tache B -- integration ESP) :
 * remplace l'ancien stub "Requires Moonraker power API - not yet available"
 * (ecran_stub.c, symbole retire de STUBS() par cette meme tache) par une
 * vraie liste des prises pilotees par Moonraker (`[power ...]`, API
 * `machine.device_power.*`) -- lecture depuis le store dedie power_devices.h
 * (tache A, deja cable par moonraker_ws.c), JAMAIS depuis `etat_klipper_t`
 * (le store power n'y entre jamais, voir le commentaire de tete de
 * power_devices.h).
 *
 * Mise en page (742x436, meme repere que ecran_fichiers.c) : une COLONNE
 * UNIQUE de lignes pleine largeur, SCROLLABLE -- contrairement a
 * ecran_fichiers.c (pagine) : POWER_DEVICES_MAX (8) tient en une poignee de
 * lignes, un defilement simple (meme idiome que `zone_chauffants` dans
 * ecran_accueil_hub.c) est plus direct qu'une pagination pour si peu
 * d'entrees. Une ligne par prise : nom a gauche, pastille ON/OFF coloree
 * (vert/gris) a droite. "No power devices" si la liste est vide -- meme
 * discipline "jamais un ecran muet" que "No files"/"No macros".
 *
 * `mettre_a_jour()` ne RECONSTRUIT les lignes que si `power_devices_t.generation`
 * a change depuis le dernier appel (memorisee dans le contexte) : evite de
 * retoucher tous les labels a chaque cycle du socle alors que rien n'a change
 * cote Moonraker (`generation` n'avance qu'a `definir()`/`maj_un()`, voir
 * power_devices.h). Le grisage de peremption (`donnees_perimees`), lui, est
 * reapplique a CHAQUE appel -- independant du contenu, il peut changer sans
 * qu'aucune prise n'ait bouge.
 *
 * Tap sur une ligne -> confirmation ("Toggle device?", patron "Print file?"
 * de ecran_fichiers.c) -> `BACKEND_ACTION_POWER`, JAMAIS d'action directe :
 * une prise peut couper l'imprimante ou une impression en cours (meme
 * raison que la confirmation d'impression de ecran_fichiers.c).
 *
 * `ecran_power_ctx_t` est expose (comme `ecran_fichiers_ctx_t`) pour
 * d'eventuels tests hote futurs -- pas de suite dediee a ce jour, cette
 * tache se limite au cablage + a l'ecran. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "lvgl.h"
#include "power_devices.h"

typedef struct ecran_power_ctx_s ecran_power_ctx_t;

/* user_data d'un rappel de clic de ligne -- meme idiome que
 * ecran_fichiers_emplacement_t (position FIXE dans le pool de lignes,
 * `ctx` jamais NULL une fois construire() passe). */
typedef struct {
    ecran_power_ctx_t *ctx;
    uint8_t            emplacement; /* 0..POWER_DEVICES_MAX-1 */
} ecran_power_emplacement_t;

struct ecran_power_ctx_s {
    lv_obj_t *zone; /* conteneur scrollable, porte le pool de lignes */
    lv_obj_t *vide; /* "No power devices", visible seulement si nb == 0 */

    lv_obj_t *lignes[POWER_DEVICES_MAX];
    lv_obj_t *labels_nom[POWER_DEVICES_MAX];
    lv_obj_t *labels_etat[POWER_DEVICES_MAX];
    ecran_power_emplacement_t emplacements[POWER_DEVICES_MAX]; /* user_data des rappels de clic */

    /* Copie bornee (defensivement NUL-terminee) de ce qui est REELLEMENT
     * affiche, relue par le rappel de clic -- jamais le store relu au
     * moment du tap, meme raison que ctx->fichiers_copie dans
     * ecran_fichiers_ctx_t : ce qui est affiche doit rester ce qui bascule. */
    char    copie_noms[POWER_DEVICES_MAX][POWER_NOM_MAX];
    bool    copie_allumee[POWER_DEVICES_MAX];
    uint8_t nb;

    uint32_t derniere_generation; /* derniere power_devices_t.generation vue */
    bool     premiere_maj;        /* force le premier redessin (generation peut deja valoir 0 dans le store) */
    bool     donnees_perimees;

    /* Nom de la prise tapee, en attente de la resolution de la confirmation
     * ouverte -- meme raison que ctx->nom_attente dans ecran_fichiers_ctx_t
     * (le dialogue reste modal potentiellement plusieurs cycles). */
    char nom_attente[POWER_NOM_MAX];
};

extern const ecran_desc_t ECRAN_POWER;
