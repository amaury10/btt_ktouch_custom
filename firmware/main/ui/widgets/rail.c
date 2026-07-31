/* Implémentation : voir rail.h pour le contrat. */
#include "rail.h"

#define COULEUR_BOUTON        0x2A3644 /* meme gris de fond que selecteur_pas.c/selecteur_choix.c */
#define COULEUR_ACTIF         0x3B82F6 /* meme bleu "actif" que COULEUR_ACTIF dans selecteur_pas.c/ecran_accueil_hub.c */
#define COULEUR_STOP          0xE5484D /* rouge -- imposee par le brief de la tache */
#define COULEUR_TEXTE_BOUTON  0xFFFFFF

/* Taille par defaut de `racine`, PAS LV_SIZE_CONTENT -- meme piege, meme
 * choix que RACINE_LARGEUR_DEFAUT/RACINE_HAUTEUR_DEFAUT dans
 * selecteur_pas.c/progression.c (voir leur commentaire complet). 58 px de
 * large : valeur explicite du brief ("colonne ~58 px"). La hauteur par
 * defaut n'est qu'un defaut sense pour un rail persistant que l'appelant
 * redimensionne de toute facon a la hauteur utile de l'ecran (meme droit que
 * progression_t/selecteur_pas_t, voir leur commentaire de tete) : quatre
 * boutons de BOUTON_HAUTEUR_DEFAUT px + trois ecarts de ECART_BOUTONS px.
 * Contrairement a selecteur_pas.c/selecteur_choix.c (rangee HORIZONTALE, RAS
 * boutons en flex_grow pour repartir la LARGEUR), les boutons ici ont une
 * hauteur FIXE plutot qu'un flex_grow sur la hauteur : necessaire pour que
 * STOP (positionne independamment du flux flex, voir rail_creer()) ait un
 * ecart croissant au-dessus de lui plutot que d'etre colle au bouton Macros
 * -- l'effet "margin-top:auto" demande par le brief n'existe que si les trois
 * premiers restent compacts en haut plutot que de se dilater pour remplir
 * `racine`. */
#define RACINE_LARGEUR_DEFAUT   58
#define BOUTON_HAUTEUR_DEFAUT   80
#define ECART_BOUTONS            8
#define RACINE_HAUTEUR_DEFAUT   (RAIL_NB * BOUTON_HAUTEUR_DEFAUT + (RAIL_NB - 1) * ECART_BOUTONS)

static const char *const LIBELLES[RAIL_NB] = { "Accueil", "Home", "Macros", "STOP" };
/* Icones distinctes malgre le risque de confusion entre "Accueil" (retour a
 * l'ecran d'accueil idle) et "Home" (homing des axes) : LV_SYMBOL_HOME (une
 * maison) pour l'ecran d'accueil, LV_SYMBOL_GPS (une cible) pour le homing,
 * jamais le meme symbole pour les deux malgre le nom anglais partage. */
static const char *const ICONES[RAIL_NB] = {
    LV_SYMBOL_HOME,
    LV_SYMBOL_GPS,
    LV_SYMBOL_LIST,
    LV_SYMBOL_STOP,
};

/* `cible` est retrouve par egalite de pointeur dans r->boutons[] plutot que
 * par un indice porte a part -- meme raisonnement que bouton_pas_cb() dans
 * selecteur_pas.c/bouton_choix_cb() dans selecteur_choix.c (voir leur
 * commentaire complet) : le contrat public de rail_t n'a pas de place pour
 * un indice supplementaire dans le user_data, et lv_event_get_user_data()
 * porte deja `r` lui-meme, suffisant pour retrouver quel bouton a ete
 * clique. Difference cle avec ces deux widgets : AUCUN lv_obj_add_state()/
 * remove_state() ici -- le rail ne fait ni navigation ni gcode (voir le
 * commentaire de tete du .h), donc un clic ne bascule jamais lui-meme un
 * etat "actif" ; seul rail_marquer_actif(), appele par l'integration future,
 * decide de ce qui est surligne. */
static void bouton_rail_cb(lv_event_t *e)
{
    rail_t *r = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (r == NULL || cible == NULL || r->sur_action == NULL) {
        return;
    }

    for (uint8_t i = 0; i < RAIL_NB; i++) {
        if (r->boutons[i] == cible) {
            r->sur_action((rail_action_t)i, r->ctx);
            return;
        }
    }
    /* ne devrait jamais arriver : seuls r->boutons[] portent ce rappel */
}

void rail_creer(rail_t *r, lv_obj_t *parent, void (*sur_action)(rail_action_t, void *), void *ctx)
{
    if (r == NULL) {
        return;
    }
    /* `r->racine` doit rester NULL sur tout chemin de sortie anticipe
     * ci-dessous (contrat du .h) -- pose une bonne fois avant la garde,
     * plutot que de repeter `r->racine = NULL;` sur chaque `return` (meme
     * choix que selecteur_choix_creer()). */
    r->racine = NULL;

    if (parent == NULL) {
        return;
    }

    r->sur_action = sur_action;
    r->ctx = ctx;

    r->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(r->racine);
    lv_obj_set_size(r->racine, RACINE_LARGEUR_DEFAUT, RACINE_HAUTEUR_DEFAUT);
    lv_obj_clear_flag(r->racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r->racine, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(r->racine, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(r->racine, ECART_BOUTONS, 0);

    for (uint8_t i = 0; i < RAIL_NB; i++) {
        lv_obj_t *b = lv_button_create(r->racine);
        /* lv_obj_remove_style_all() AVANT toute autre lv_obj_set_style_*()/
         * lv_obj_set_height()/etc. -- meme piege, meme ordre que
         * selecteur_pas.c/selecteur_choix.c (voir leur commentaire complet :
         * ote le theme par defaut ET sa transition de couleur animee sur
         * bg_color, et bg_opa/LV_OPA_TRANSP doit etre reaffirme en
         * LV_OPA_COVER juste apres, sans quoi bg_color resterait
         * invisible). */
        lv_obj_remove_style_all(b);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_height(b, BOUTON_HAUTEUR_DEFAUT);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        /* STOP est rouge en permanence, PAS via un etat -- la couleur
         * "actif"/surligne (rail_marquer_actif(), voir plus bas) est portee
         * uniquement par une bordure, jamais par bg_color : sinon marquer
         * STOP actif remplacerait son rouge par le meme bleu que les trois
         * autres, "banalisant" exactement le signal que le brief demande de
         * garder distinct. */
        lv_obj_set_style_bg_color(b, lv_color_hex(i == RAIL_STOP ? COULEUR_STOP : COULEUR_BOUTON), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 3, LV_STATE_CHECKED);
        lv_obj_set_style_border_color(b, lv_color_hex(COULEUR_ACTIF), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);

        lv_obj_t *label = lv_label_create(b);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_fmt(label, "%s\n%s", ICONES[i], LIBELLES[i]);
        lv_obj_center(label);

        r->boutons[i] = b;
        lv_obj_add_event_cb(b, bouton_rail_cb, LV_EVENT_CLICKED, r);
    }

    /* STOP pousse en bas de la colonne : sorti du flux flex
     * (LV_OBJ_FLAG_IGNORE_LAYOUT) puis aligne explicitement sur le bas de
     * `racine` -- emulation de "margin-top:auto" (CSS), qu'aucune propriete
     * de lv_flex ne fournit telle quelle (voir simulateur/lvgl/src/layouts/
     * flex/lv_flex.c, aucune notion de marge "auto"). Les trois premiers
     * restent geres par le flex column normal, empiles compactement en haut
     * (voir le commentaire de RACINE_HAUTEUR_DEFAUT plus haut sur pourquoi
     * ils ne sont volontairement PAS en flex_grow) ; seul STOP est
     * positionne independamment, avec pour consequence que l'ecart entre le
     * groupe de navigation et STOP grandit avec la hauteur de `racine` --
     * exactement le comportement voulu par le brief, plutot qu'un ecart fixe
     * de ECART_BOUTONS comme entre les trois premiers. La largeur LV_PCT(100)
     * deja posee ci-dessus reste valide : IGNORE_LAYOUT ne retire l'objet que
     * du calcul de POSITION par l'algorithme flex, pas de la resolution
     * normale de ses propres dimensions. */
    lv_obj_t *stop = r->boutons[RAIL_STOP];
    lv_obj_add_flag(stop, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void rail_marquer_actif(rail_t *r, rail_action_t action)
{
    if (r == NULL) {
        return;
    }

    /* `action == RAIL_NB` (aucun actif) : la boucle ci-dessous ne rencontre
     * jamais i == RAIL_NB (i reste dans [0, RAIL_NB[), donc chaque bouton
     * repasse en LV_STATE_CHECKED retire -- aucun cas particulier necessaire
     * pour ce cas, contrairement a ce qu'un `if (action == RAIL_NB) return;`
     * laisserait presumer. */
    for (uint8_t i = 0; i < RAIL_NB; i++) {
        if (i == action) {
            lv_obj_add_state(r->boutons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(r->boutons[i], LV_STATE_CHECKED);
        }
    }
}
