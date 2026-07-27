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

/* Taille par défaut de `racine`, PAS LV_SIZE_CONTENT (voir le commentaire de
 * progression_creer() ci-dessous pour le piège précis que ça évite). 700 :
 * une largeur "quasiment pleine largeur" pour l'écran 800 px de la tâche 6,
 * assez proche du réel pour rendre quelque chose de sensé même si l'écran
 * appelant oublie de la redéfinir. 36 : assez pour loger BARRE_HAUTEUR (24)
 * plus l'étiquette de pourcentage (Montserrat 20, ~23 px de haut) centrée
 * par-dessus sans qu'elle déborde verticalement. Reste un défaut, pas une
 * contrainte : l'écran appelant garde le droit de lv_obj_set_size(p.racine,
 * ...) juste après progression_creer(), exactement comme simulateur/main.c
 * le fait déjà pour sa mise en page à lui. */
#define RACINE_LARGEUR_DEFAUT 700
#define RACINE_HAUTEUR_DEFAUT 36

void progression_creer(progression_t *p, lv_obj_t *parent)
{
    if (p == NULL || parent == NULL) {
        return;
    }

    p->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(p->racine);
    /* PAS LV_SIZE_CONTENT sur les deux dimensions ici (défaut de la revue
     * tâche 5, fix round 1) : `barre`, juste en dessous, est LV_PCT(100) de
     * large — un enfant en pourcentage dans un parent en LV_SIZE_CONTENT est
     * une dépendance circulaire que LVGL 9.2 résout en clouant l'enfant à
     * zéro plutôt que de boucler (voir simulateur/lvgl/src/core/lv_obj_pos.c,
     * la branche `w_is_pct` : "If parent has content size and the child has
     * pct size a circular dependency will occur. To solve it keep child size
     * at zero"). Ça ne s'est jamais vu à l'œil dans ce dépôt parce que
     * simulateur/main.c redimensionne toujours `racine` juste après cet
     * appel, avant la première passe de mise en page — mais rien n'y oblige
     * un futur écran, et tuile.c utilise LV_SIZE_CONTENT avec succès juste à
     * côté (ses enfants sont tous des labels de taille naturelle, aucun
     * pourcentage), ce qui rend l'erreur plausible par simple copier-coller
     * du motif voisin. Un défaut explicite élimine la classe d'erreur à la
     * source plutôt que de compter sur un commentaire pour la prévenir (voir
     * suite_progression() dans host-test/tests/test_widgets.c pour la
     * régression qui vérifie que `barre` a une largeur réelle sans qu'aucun
     * appelant n'ait besoin de la redéfinir). */
    lv_obj_set_size(p->racine, RACINE_LARGEUR_DEFAUT, RACINE_HAUTEUR_DEFAUT);
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
