/* Implémentation : voir selecteur_pas.h pour le contrat. */
#include "selecteur_pas.h"

#define COULEUR_BOUTON        0x2A3644
#define COULEUR_ACTIF         0x3B82F6 /* meme bleu "actif" que COULEUR_ACTIF dans ecran_accueil_idle.c */
#define COULEUR_TEXTE_BOUTON  0xFFFFFF

/* Taille par defaut de `racine`, PAS LV_SIZE_CONTENT -- meme piege, meme
 * choix que RACINE_LARGEUR_DEFAUT/RACINE_HAUTEUR_DEFAUT dans progression.c
 * (voir son commentaire complet) : chaque bouton est en LV_PCT(100) de haut
 * (pour remplir toute la hauteur de `racine`, quelle qu'elle soit) et un
 * enfant en pourcentage dans un parent LV_SIZE_CONTENT est une dependance
 * circulaire que LVGL 9.2 resout en clouant l'enfant a zero plutot que de
 * boucler. lv_obj_set_flex_grow(b, 1) sur chaque bouton repartit la LARGEUR
 * (elle, jamais en pourcentage) egalement entre les quatre -- y compris
 * apres que l'ecran appelant ait redefini la taille de `racine` (meme droit
 * que progression_t, voir son commentaire de tete). Les deux chiffres
 * ci-dessous ne sont donc qu'un defaut sensé, jamais une contrainte : 4
 * boutons de 56 px + 3 ecarts de 8 px. */
#define RACINE_LARGEUR_DEFAUT (4 * 56 + 3 * 8)
#define RACINE_HAUTEUR_DEFAUT 40
#define ECART_BOUTONS            8

const float SELECTEUR_PAS_MM[4] = { 0.1f, 1.0f, 10.0f, 100.0f };

static const char *const LIBELLES[4] = { "0.1", "1", "10", "100" };

/* Un seul actif a la fois (LV_STATE_CHECKED, style local dedie -- meme
 * raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_macros.c/
 * ecran_accueil.c pour LV_STATE_DISABLED : LV_STATE_CHECKED seul, sans style
 * local enregistre pour cet etat precis, est pixel-identique a l'etat
 * normal tant qu'aucune couleur n'est postee dessus -- ce serait le meme
 * grisage-mensonger que la lecon de la revue finale du jalon 2b, appliquee
 * ici a "actif" plutot qu'a "desactive". `cible` est retrouve par egalite de
 * pointeur dans s->boutons[] plutot que par un indice porte a part : le
 * contrat public de selecteur_pas_t (voir le .h) n'a pas de place pour un
 * indice supplementaire dans le user_data, et lv_event_get_user_data() porte
 * deja `s` lui-meme, suffisant pour retrouver quel bouton a ete clique. */
static void bouton_pas_cb(lv_event_t *e)
{
    selecteur_pas_t *s = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (s == NULL || cible == NULL) {
        return;
    }

    uint8_t indice_clique = s->index_actif;
    bool trouve = false;
    for (uint8_t i = 0; i < 4; i++) {
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
    for (uint8_t i = 0; i < 4; i++) {
        if (i == s->index_actif) {
            lv_obj_add_state(s->boutons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s->boutons[i], LV_STATE_CHECKED);
        }
    }
}

void selecteur_pas_creer(selecteur_pas_t *s, lv_obj_t *parent)
{
    if (s == NULL || parent == NULL) {
        return;
    }

    s->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(s->racine);
    lv_obj_set_size(s->racine, RACINE_LARGEUR_DEFAUT, RACINE_HAUTEUR_DEFAUT);
    lv_obj_clear_flag(s->racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s->racine, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s->racine, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s->racine, ECART_BOUTONS, 0);

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(s->racine);
        /* lv_obj_remove_style_all() AVANT toute autre lv_obj_set_style_*()/
         * lv_obj_set_height()/flex_grow() (des proprietes de style, elles
         * aussi effacees par cet appel) -- ote le theme par defaut ET sa
         * transition de couleur animee sur bg_color (meme piege que
         * pomper_transitions_style() dans host-test/tests/test_commandes.c,
         * mais corrige ICI plutot que contourne par un test : le bouton
         * "1 mm" actif par defaut passe a l'etat LV_STATE_CHECKED DANS
         * selecteur_pas_creer(), avant le tout premier rendu -- une capture
         * hors ecran (simulateur/main.c, un seul afficheur_pomper(0), AUCUNE
         * avance d'horloge LVGL) le montrerait alors a mi-transition, donc
         * visuellement PAS actif, constate sur idle-jog.png avant ce fix.
         * Meme technique que tuile.c/progression.c (widgets non-themes,
         * jamais de transition a purger). */
        lv_obj_remove_style_all(b);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_color(b, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(COULEUR_ACTIF), LV_STATE_CHECKED);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);

        lv_obj_t *label = lv_label_create(b);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_label_set_text(label, LIBELLES[i]);
        lv_obj_center(label);

        s->boutons[i] = b;
        lv_obj_add_event_cb(b, bouton_pas_cb, LV_EVENT_CLICKED, s);
    }

    s->index_actif = 1; /* 1 mm, voir le commentaire de selecteur_pas_creer() dans le .h */
    lv_obj_add_state(s->boutons[1], LV_STATE_CHECKED);
}

float selecteur_pas_valeur(const selecteur_pas_t *s)
{
    if (s == NULL || s->index_actif >= 4) {
        return SELECTEUR_PAS_MM[1];
    }
    return SELECTEUR_PAS_MM[s->index_actif];
}
