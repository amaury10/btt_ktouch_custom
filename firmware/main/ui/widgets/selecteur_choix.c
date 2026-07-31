/* Implémentation : voir selecteur_choix.h pour le contrat. */
#include "selecteur_choix.h"

#define COULEUR_BOUTON        0x2A3644
#define COULEUR_ACTIF         0x3B82F6 /* meme bleu "actif" que COULEUR_ACTIF dans selecteur_pas.c/ecran_accueil_idle.c */
#define COULEUR_TEXTE_BOUTON  0xFFFFFF

/* nb hors [NB_MIN, NB_MAX] -> no-op (voir le contrat dans le .h) : deux
 * boutons au minimum pour que "exclusif" ait un sens, huit au maximum --
 * taille du tableau boutons[] du contrat public (voir le .h). */
#define NB_MIN 2
#define NB_MAX 8

/* Taille par defaut de `racine`, PAS LV_SIZE_CONTENT -- meme piege, meme
 * choix que selecteur_pas.c (voir son commentaire complet) : chaque bouton
 * est en LV_PCT(100) de haut et lv_obj_set_flex_grow(b, 1) repartit la
 * LARGEUR egalement entre les `nb` boutons, y compris apres que l'ecran
 * appelant ait redefini la taille de `racine`. BOUTON_LARGEUR_DEFAUT n'est
 * donc qu'un defaut sense pour calculer la largeur totale, jamais une
 * contrainte par-bouton.
 *
 * RACINE_HAUTEUR_DEFAUT est deliberement 44 (pas les 40 px de
 * selecteur_pas.c) : contrainte explicite de la tache -- cible tactile
 * minimale de 44 px -- que selecteur_pas.c, ecrit avant cette regle, ne
 * respectait pas encore. */
#define BOUTON_LARGEUR_DEFAUT 56
#define RACINE_HAUTEUR_DEFAUT 44
#define ECART_BOUTONS          8

/* Un seul actif a la fois (LV_STATE_CHECKED, style local dedie -- meme
 * raisonnement que selecteur_pas.c/bouton_pas_cb() pour le detail complet).
 * `cible` est retrouve par egalite de pointeur dans s->boutons[0..nb-1]
 * plutot que par un indice porte a part : le contrat public de
 * selecteur_choix_t (voir le .h) n'a pas de place pour un indice
 * supplementaire dans le user_data, et lv_event_get_user_data() porte deja
 * `s` lui-meme, suffisant pour retrouver quel bouton a ete clique -- et
 * combien de boutons existent reellement (s->nb). */
static void bouton_choix_cb(lv_event_t *e)
{
    selecteur_choix_t *s = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (s == NULL || cible == NULL) {
        return;
    }

    uint8_t indice_clique = s->index_actif;
    bool trouve = false;
    for (uint8_t i = 0; i < s->nb; i++) {
        if (s->boutons[i] == cible) {
            indice_clique = i;
            trouve = true;
            break;
        }
    }
    if (!trouve) {
        return; /* ne devrait jamais arriver : seuls s->boutons[] portent ce rappel */
    }

    s->index_actif = indice_clique;
    for (uint8_t i = 0; i < s->nb; i++) {
        if (i == s->index_actif) {
            lv_obj_add_state(s->boutons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s->boutons[i], LV_STATE_CHECKED);
        }
    }
}

void selecteur_choix_creer(selecteur_choix_t *s, lv_obj_t *parent,
                            const char *const *libelles, uint8_t nb, uint8_t defaut)
{
    if (s == NULL) {
        return;
    }
    /* `s->racine` doit rester NULL sur tout chemin de sortie anticipe
     * ci-dessous (contrat du .h) -- pose une bonne fois avant les gardes,
     * plutot que de repeter `s->racine = NULL;` sur chaque `return`. */
    s->racine = NULL;

    if (parent == NULL || libelles == NULL || nb < NB_MIN || nb > NB_MAX) {
        return;
    }

    s->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(s->racine);
    lv_obj_set_size(s->racine, nb * BOUTON_LARGEUR_DEFAUT + (nb - 1) * ECART_BOUTONS,
                     RACINE_HAUTEUR_DEFAUT);
    lv_obj_clear_flag(s->racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s->racine, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s->racine, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s->racine, ECART_BOUTONS, 0);

    s->nb = nb;

    for (uint8_t i = 0; i < nb; i++) {
        lv_obj_t *b = lv_button_create(s->racine);
        /* lv_obj_remove_style_all() AVANT toute autre lv_obj_set_style_*()/
         * lv_obj_set_height()/flex_grow() -- meme piege, meme ordre que
         * selecteur_pas.c (voir son commentaire complet : ote le theme par
         * defaut ET sa transition de couleur animee sur bg_color, et
         * bg_opa/LV_OPA_TRANSP doit etre reaffirme en LV_OPA_COVER juste
         * apres, sans quoi bg_color resterait invisible). */
        lv_obj_remove_style_all(b);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(COULEUR_ACTIF), LV_STATE_CHECKED);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 3, LV_STATE_CHECKED);
        lv_obj_set_style_border_color(b, lv_color_hex(COULEUR_TEXTE_BOUTON), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);

        lv_obj_t *label = lv_label_create(b);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_label_set_text(label, libelles[i]);
        lv_obj_center(label);

        s->boutons[i] = b;
        lv_obj_add_event_cb(b, bouton_choix_cb, LV_EVENT_CLICKED, s);
    }

    /* `defaut` borne a nb - 1 (contrat du .h) : une valeur hors bornes ne
     * pointe donc jamais sur un s->boutons[i] inexistant. */
    s->index_actif = (defaut < nb) ? defaut : (uint8_t)(nb - 1);
    lv_obj_add_state(s->boutons[s->index_actif], LV_STATE_CHECKED);
}

uint8_t selecteur_choix_index(const selecteur_choix_t *s)
{
    if (s == NULL) {
        return 0;
    }
    return s->index_actif;
}
