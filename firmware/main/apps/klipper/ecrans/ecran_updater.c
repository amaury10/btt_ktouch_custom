/* Implementation : voir ecran_updater.h pour le contrat et pourquoi ce
 * panneau remplace le stub "Requires OTA..." de ecran_stub.c (Task 2 : le
 * symbole ECRAN_UPDATER est retire de STUBS() dans ce meme depot, plus aucun
 * doublon).
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c -- meme repere
 * que les stubs restants, voir ecran_stub.c) : titre "Updater" en haut, puis
 * trois lignes centrees empilees en lecture seule (slot, version, rappel de
 * procedure) -- meme idiome visuel que ecran_stub_peindre() (label titre +
 * labels secondaires empiles via LV_ALIGN_OUT_BOTTOM_MID), sans aucun element
 * cliquable (brief : "no clickable elements needed"). */
#include "ecran_updater.h"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#endif

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9

/* Cree un label centre, soit ancre en haut du parent (`au_dessus == NULL`,
 * meme position que le titre des six stubs, y=160), soit empile sous
 * `au_dessus` avec un ecart vertical -- factorise les quatre labels de cet
 * ecran (titre + trois lignes), meme role que ecran_stub_peindre() pour les
 * six stubs. */
static lv_obj_t *ligne_creer(lv_obj_t *parent, lv_obj_t *au_dessus, const char *texte, const lv_font_t *police,
                              uint32_t couleur, lv_coord_t ecart)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, police, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, texte);
    if (au_dessus != NULL) {
        lv_obj_align_to(label, au_dessus, LV_ALIGN_OUT_BOTTOM_MID, 0, ecart);
    } else {
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 160);
    }
    return label;
}

static void ecran_updater_construire(lv_obj_t *parent, void *contexte)
{
    ecran_updater_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titre = ligne_creer(parent, NULL, "Updater", &lv_font_montserrat_28, COULEUR_TEXTE_PRINCIPAL, 0);

    /* --- slot OTA courant : esp_ota_get_running_partition()->label sur
     * cible ("app0"/"app1", meme partition que /status et /revert -- voir
     * gestion_status()/rescue.c). Hors cible (host-test, simulateur PC), ni
     * esp_ota_ops.h ni une vraie partition n'existent : "sim" en repli, meme
     * discipline que wifi_scanner dans ecran_reglages_wifi.c. ------------- */
    char texte_slot[48];
#ifdef ESP_PLATFORM
    const esp_partition_t *courante = esp_ota_get_running_partition();
    snprintf(texte_slot, sizeof(texte_slot), "Slot: %s", courante != NULL ? courante->label : "?");
#else
    snprintf(texte_slot, sizeof(texte_slot), "Slot: sim");
#endif
    ctx->label_slot = ligne_creer(parent, titre, texte_slot, &lv_font_montserrat_20, COULEUR_TEXTE_SECONDAIRE, 16);

    /* --- version firmware : esp_app_get_description()->version, exactement
     * le meme champ que /status publie (web.c, gestion_status()) -- jamais
     * une deuxieme source qui pourrait diverger. "dev" en repli hors
     * cible. -------------------------------------------------------------- */
    char texte_version[64];
#ifdef ESP_PLATFORM
    const esp_app_desc_t *description = esp_app_get_description();
    snprintf(texte_version, sizeof(texte_version), "Version: %s", description != NULL ? description->version : "?");
#else
    snprintf(texte_version, sizeof(texte_version), "Version: dev");
#endif
    ctx->label_version =
        ligne_creer(parent, ctx->label_slot, texte_version, &lv_font_montserrat_20, COULEUR_TEXTE_SECONDAIRE, 8);

    /* --- rappel de procedure, texte FIXE : aucune route de mise a jour
     * n'est cablee depuis cet ecran ni depuis ce firmware a ce jalon (voir le
     * commentaire de tete du .h). ------------------------------------------ */
    ctx->label_update = ligne_creer(parent, ctx->label_version, "Update via /ota (browser)", &lv_font_montserrat_20,
                                     COULEUR_TEXTE_SECONDAIRE, 24);
}

const ecran_desc_t ECRAN_UPDATER = {
    .id = "updater",
    .titre = "Updater",
    .taille_contexte = sizeof(ecran_updater_ctx_t),
    .construire = ecran_updater_construire,
    .mettre_a_jour = NULL,
    .detruire = NULL,
};
