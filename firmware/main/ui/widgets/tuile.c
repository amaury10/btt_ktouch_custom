/* Implémentation : voir tuile.h pour le contrat. */
#include "tuile.h"

#include <math.h>
#include <stdio.h>

#define COULEUR_FOND_TUILE       0x1B2430
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* imposée par le brief : gris de péremption */

/* Plausibilité d'une température Klipper (voir le commentaire de
 * ui_format_temperature() dans tuile.h). */
#define TEMPERATURE_MIN_C -5.0f
#define TEMPERATURE_MAX_C 500.0f

void tuile_creer(tuile_t *t, lv_obj_t *parent, const char *libelle)
{
    if (t == NULL || parent == NULL) {
        return;
    }

    t->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(t->racine);
    lv_obj_set_size(t->racine, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(t->racine, lv_color_hex(COULEUR_FOND_TUILE), 0);
    lv_obj_set_style_bg_opa(t->racine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t->racine, 10, 0);
    /* Bare lv_obj_create() affiche une barre de défilement et un padding de
     * thème par défaut (constaté tâche 1, voir habillage.c pour le même
     * motif) : neutralisés ici, jamais laissés à la charge de l'écran
     * appelant. */
    lv_obj_clear_flag(t->racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(t->racine, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t->racine, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(t->racine, 4, 0);

    t->libelle = lv_label_create(t->racine);
    lv_obj_set_style_text_font(t->libelle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t->libelle, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(t->libelle, libelle != NULL ? libelle : "");

    t->valeur = lv_label_create(t->racine);
    lv_obj_set_style_text_font(t->valeur, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(t->valeur, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(t->valeur, "");

    t->consigne = lv_label_create(t->racine);
    lv_obj_set_style_text_font(t->consigne, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t->consigne, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(t->consigne, "");
}

void tuile_definir_valeur(tuile_t *t, const char *texte)
{
    if (t == NULL || t->valeur == NULL) {
        return;
    }
    lv_label_set_text(t->valeur, texte != NULL ? texte : "");
}

void tuile_definir_consigne(tuile_t *t, const char *texte)
{
    if (t == NULL || t->consigne == NULL) {
        return;
    }
    lv_label_set_text(t->consigne, texte != NULL ? texte : "");
}

void tuile_griser(tuile_t *t, bool grise)
{
    if (t == NULL || t->racine == NULL) {
        return;
    }
    /* Recoloré systématiquement à partir de `grise`, jamais incrémental :
     * un appel avec grise=false doit rendre exactement les couleurs
     * normales, y compris après plusieurs allers-retours (revue tâche 4,
     * fix round 1 : le grisage doit rester réversible). */
    uint32_t couleur_valeur = grise ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    uint32_t couleur_secondaire = grise ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(t->libelle, lv_color_hex(couleur_secondaire), 0);
    lv_obj_set_style_text_color(t->valeur, lv_color_hex(couleur_valeur), 0);
    lv_obj_set_style_text_color(t->consigne, lv_color_hex(couleur_secondaire), 0);
}

void ui_format_temperature(char *sortie, size_t taille, float celsius)
{
    if (sortie == NULL || taille == 0) {
        return;
    }
    /* isnan()/isinf() d'abord : une comparaison ordinaire (<, >) avec un NaN
     * est toujours fausse et laisserait passer une valeur non plausible
     * sans le détecter (même piège que VERIFIER_FLOAT, voir petit_test.h). */
    if (isnan(celsius) || isinf(celsius) || celsius < TEMPERATURE_MIN_C || celsius > TEMPERATURE_MAX_C) {
        snprintf(sortie, taille, "--");
        return;
    }
    snprintf(sortie, taille, "%.1f", (double)celsius);
}
