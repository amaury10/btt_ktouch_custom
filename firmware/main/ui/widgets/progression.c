/* Implémentation : voir progression.h pour le contrat. */
#include "progression.h"

#include <math.h>
#include <stdio.h>

#define COULEUR_FOND_BARRE       0x2A3644
#define COULEUR_INDICATEUR       0x2E86F5
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_GRISE            0x6B7280 /* imposée par le brief : gris de péremption */

/* Plage interne du lv_bar : dixièmes de pourcent (0..1000) plutôt que 0..100,
 * pour que la fraction transmise à progression_definir() (0.0..1.0) se
 * convertisse directement en un texte à un décimale sans perte. */
#define BARRE_PLAGE_MAX 1000

#define BARRE_HAUTEUR 24

void progression_creer(progression_t *p, lv_obj_t *parent)
{
    if (p == NULL || parent == NULL) {
        return;
    }

    p->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(p->racine);
    lv_obj_set_size(p->racine, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(p->racine, LV_OBJ_FLAG_SCROLLABLE);

    p->barre = lv_bar_create(p->racine);
    lv_obj_set_size(p->barre, LV_PCT(100), BARRE_HAUTEUR);
    lv_bar_set_range(p->barre, 0, BARRE_PLAGE_MAX);
    lv_bar_set_value(p->barre, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(p->barre, lv_color_hex(COULEUR_FOND_BARRE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p->barre, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p->barre, lv_color_hex(COULEUR_INDICATEUR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(p->barre, LV_OPA_COVER, LV_PART_INDICATOR);

    p->etiquette = lv_label_create(p->racine);
    lv_obj_set_style_text_font(p->etiquette, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(p->etiquette, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(p->etiquette, "0.0%");
    lv_obj_center(p->etiquette);
}

void progression_definir(progression_t *p, float fraction)
{
    if (p == NULL || p->barre == NULL) {
        return;
    }

    /* isnan()/isinf() d'abord : une comparaison ordinaire avec un NaN est
     * toujours fausse, donc `fraction < 0.0f` ne le rattraperait pas — voir
     * le même garde-fou dans ui_format_temperature() (tuile.c). */
    if (isnan(fraction) || isinf(fraction)) {
        fraction = (isinf(fraction) && fraction > 0.0f) ? 1.0f : 0.0f;
    } else if (fraction < 0.0f) {
        fraction = 0.0f;
    } else if (fraction > 1.0f) {
        fraction = 1.0f;
    }

    int32_t valeur_barre = (int32_t)(fraction * (float)BARRE_PLAGE_MAX);
    lv_bar_set_value(p->barre, valeur_barre, LV_ANIM_OFF);

    char tampon[8];
    snprintf(tampon, sizeof(tampon), "%.1f%%", (double)(fraction * 100.0f));
    lv_label_set_text(p->etiquette, tampon);
}

void progression_griser(progression_t *p, bool grise)
{
    if (p == NULL || p->barre == NULL) {
        return;
    }
    /* Même politique de recoloration systématique que tuile_griser(). */
    lv_obj_set_style_bg_color(p->barre, lv_color_hex(grise ? COULEUR_GRISE : COULEUR_INDICATEUR),
                               LV_PART_INDICATOR);
    lv_obj_set_style_text_color(p->etiquette, lv_color_hex(grise ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL), 0);
}

void ui_format_duree(char *sortie, size_t taille, uint32_t secondes)
{
    if (sortie == NULL || taille == 0) {
        return;
    }
    if (secondes == 0) {
        /* 0 encode "inconnu", pas "zéro seconde restante" (voir le
         * commentaire de KLIPPER_TEMPS_RESTANT_MAX_S dans etat_klipper.h) :
         * même règle que la température, ne jamais présenter comme mesuré
         * ce qui ne l'est pas. */
        snprintf(sortie, taille, "--");
        return;
    }

    uint32_t minutes_totales = secondes / 60u;
    uint32_t heures = minutes_totales / 60u;
    uint32_t minutes = minutes_totales % 60u;

    if (heures > 0u) {
        snprintf(sortie, taille, "%uh %02um", (unsigned)heures, (unsigned)minutes);
    } else {
        snprintf(sortie, taille, "%um", (unsigned)minutes);
    }
}
