/* Implementation : voir ecran_jouet.h pour le contrat.
 *
 * Mise en page minimale, sans souci d'alignement pixel-parfait (contrairement
 * a ecran_accueil.c) : ce n'est pas l'objet de la tache 11, seulement la
 * preuve qu'un ecran d'application tierce s'accroche au meme ecran.h. */
#include "ecran_jouet.h"

#include <stdio.h>

#include "backend_jouet.h"
#include "habillage.h"
#include "source_etat.h"

#define COULEUR_FOND         0x10161D
#define COULEUR_TEXTE        0xC9D1D9
#define COULEUR_GRISE        0x6B7280 /* impose par le brief : gris de peremption */
#define COULEUR_BOUTON       0x2A3644
#define COULEUR_TEXTE_BOUTON 0xFFFFFF

/* Meme motif que executer_commande() dans ecran_accueil.c : ui_commander()
 * est la seule porte (ui/source_etat.h), un echec SYNCHRONE se notifie via
 * habillage_notifier() -- jamais de boite d'erreur posee par l'ecran
 * lui-meme (spec 5.3). */
static void bouton_reset_cb(lv_event_t *e)
{
    ecran_jouet_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees) {
        /* Garde defensive : LV_STATE_DISABLED bloque deja un appui tactile
         * reel, mais un evenement envoye directement (host-test) n'y passe
         * pas -- voir le commentaire de ecran_jouet_ctx_t::donnees_perimees. */
        return;
    }
    esp_err_t erreur = ui_commander("reset", NULL);
    if (erreur != ESP_OK) {
        habillage_notifier("Command failed: reset", true);
    }
}

static void ecran_jouet_construire(lv_obj_t *parent, void *contexte)
{
    ecran_jouet_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titre = lv_label_create(parent);
    lv_obj_set_style_text_font(titre, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titre, lv_color_hex(COULEUR_TEXTE), 0);
    lv_label_set_text(titre, "Toy backend");
    lv_obj_set_pos(titre, 20, 20);

    ctx->valeur = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->valeur, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->valeur, lv_color_hex(COULEUR_TEXTE), 0);
    lv_label_set_text(ctx->valeur, "");
    lv_obj_set_pos(ctx->valeur, 20, 60);

    ctx->bouton = lv_button_create(parent);
    lv_obj_set_size(ctx->bouton, 150, 60);
    lv_obj_set_pos(ctx->bouton, 20, 110);
    lv_obj_set_style_bg_color(ctx->bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(ctx->bouton, 0, 0);
    lv_obj_set_style_shadow_width(ctx->bouton, 0, 0);
    lv_obj_set_style_radius(ctx->bouton, 10, 0);
    lv_obj_add_event_cb(ctx->bouton, bouton_reset_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *label = lv_label_create(ctx->bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, "Reset");
    lv_obj_center(label);
}

static void ecran_jouet_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_jouet_ctx_t *ctx = contexte;
    const etat_jouet_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    char texte[32];
    snprintf(texte, sizeof(texte), "Count: %u", (unsigned)e->compteur);
    lv_label_set_text(ctx->valeur, texte);

    /* Grisage systematique a chaque appel, jamais incremental -- meme regle
     * que ecran_accueil.c : un appel avec donnees_perimees=false doit rendre
     * exactement les couleurs normales, y compris apres plusieurs
     * allers-retours. */
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE;
    lv_obj_set_style_text_color(ctx->valeur, lv_color_hex(couleur), 0);

    ctx->donnees_perimees = donnees_perimees;
    if (donnees_perimees) {
        lv_obj_add_state(ctx->bouton, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(ctx->bouton, LV_STATE_DISABLED);
    }
}

const ecran_desc_t ECRAN_JOUET = {
    .id = "jouet",
    .titre = "Toy",
    .taille_contexte = sizeof(ecran_jouet_ctx_t),
    .construire = ecran_jouet_construire,
    .mettre_a_jour = ecran_jouet_mettre_a_jour,
    .detruire = NULL,
};
