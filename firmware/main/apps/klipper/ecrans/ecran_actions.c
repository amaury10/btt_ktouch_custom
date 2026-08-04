/* Implementation : voir ecran_actions.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : une seule
 * grille de 9 cases (3 colonnes x 3 lignes), dont seulement les 7 premieres
 * (ordre du brief : Move/Extrude/Fan/Temperature/Macros/Disable Motors/
 * Console) sont occupees -- CENTREE verticalement dans le contenu, meme
 * idiome de grille a cases fixes (position calculee, `_Static_assert` de
 * non-debordement + clearance du bandeau) que ecran_menu_reglages.c.
 *
 * Six des sept cases empilent un panneau deja construit par un jalon
 * precedent (Move/Extrude/Fan/Temperature/Macros/Console ->
 * navigation_empiler(), meme idiome X-macro que BOUTONS(X)/DEFINIR_CB dans
 * ecran_menu_reglages.c) ; la septieme ("Disable Motors") envoie M84
 * DIRECTEMENT (klipper_gcode_niveau_lit(KLIPPER_LIT_DISABLE) ->
 * construire_arguments_gcode()/envoyer_gcode(), copie exacte de l'idiome de
 * ecran_niveau_lit.c) -- KlipperScreen ne demande AUCUNE confirmation avant
 * ce bouton precis (task-2-brief.md, verbatim), ce depot ne l'invente pas
 * non plus.
 *
 * Menu purement statique : `mettre_a_jour = NULL` (rien a rafraichir, aucune
 * des sept actions ne depend d'une valeur lue ici), `detruire = NULL`
 * (aucune ressource au-dela du contexte -- voir ecran.h). */
#include "ecran_actions.h"

#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "ecran_console.h"      /* ECRAN_CONSOLE (feature "Console gcode", tache B) */
#include "ecran_deplacer.h"     /* ECRAN_DEPLACER */
#include "ecran_extruder.h"     /* ECRAN_EXTRUDER */
#include "ecran_macros.h"       /* ECRAN_MACROS */
#include "ecran_temperatures.h" /* ECRAN_TEMPERATURES */
#include "ecran_ventilateurs.h" /* ECRAN_VENTILATEURS */
#include "klipper_gcode.h"      /* klipper_gcode_niveau_lit()/KLIPPER_LIT_DISABLE */
#include "navigation.h"         /* navigation_empiler() */
#include "source_etat.h"        /* ui_commander() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE 14

/* --- Grille : 3 colonnes x 3 lignes (9 cases, 7 occupees) -- geometrie
 * propre a ce sous-menu (pas de zone au-dessus, comme ecran_menu_reglages.c).
 * Meme LARGEUR_CONTENU/MARGE/nombre de colonnes que ecran_menu_reglages.c :
 * meme largeur/ecart de colonne EXACTEMENT (696 = 3*232, ecart 9px de chaque
 * cote -- voir son commentaire complet pour le detail de la division
 * exacte). ------------------------------------------------------------- */
#define GRILLE_COLONNES 3
#define GRILLE_LIGNES   3
#define GRILLE_ECART_COLONNE 9
#define GRILLE_ECART_LIGNE   14

#define GRILLE_CELL_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - (GRILLE_COLONNES - 1) * GRILLE_ECART_COLONNE) / GRILLE_COLONNES)
#define GRILLE_CELL_HAUTEUR 64 /* >= 44px cible tactile minimale (brief : "aim ~64px") */
#define GRILLE_HAUTEUR (GRILLE_LIGNES * GRILLE_CELL_HAUTEUR + (GRILLE_LIGNES - 1) * GRILLE_ECART_LIGNE)

/* Grille centree verticalement dans le contenu -- meme choix purement
 * esthetique que ecran_menu_reglages.c (aucune contrainte de layout ne
 * l'impose ici). */
#define GRILLE_Y ((HAUTEUR_CONTENU - GRILLE_HAUTEUR) / 2)

/* Meme convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_menu_reglages.c (voir son commentaire complet) :
 * bande couverte par le bandeau de notification de habillage.c, en
 * coordonnees ABSOLUES d'ecran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(GRILLE_COLONNES * GRILLE_CELL_LARGEUR + (GRILLE_COLONNES - 1) * GRILLE_ECART_COLONNE ==
                    LARGEUR_CONTENU - 2 * MARGE,
                "la grille ne remplit plus exactement la largeur du contenu");
_Static_assert(GRILLE_Y + GRILLE_HAUTEUR <= HAUTEUR_CONTENU,
                "la grille deborde de la hauteur du contenu");
/* Meme garde-fou que dans ecran_menu_reglages.c : le bas de la grille, en
 * coordonnees ABSOLUES d'ecran, doit rester au-dessus du bandeau de
 * notification -- sans quoi une notification recouvrirait ET bloquerait le
 * tap sur les cases. */
_Static_assert(BARRE_HAUTEUR_ECRAN + GRILLE_Y + GRILLE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la grille chevauche la bande du bandeau de notification de l'habillage");
/* Sept cases occupent au plus les 9 de la grille -- garde-fou de coherence
 * entre ECRAN_ACTIONS_NB (le .h) et GRILLE_COLONNES*GRILLE_LIGNES (ici), pour
 * qu'un futur ajout de case ne deborde jamais silencieusement de la grille
 * fixe. */
_Static_assert(ECRAN_ACTIONS_NB <= GRILLE_COLONNES * GRILLE_LIGNES,
                "plus de cases que la grille 3x3 n'en contient");

#define COULEUR_FOND         0x10161D
#define COULEUR_BOUTON       0x2A3644
#define COULEUR_TEXTE_BOUTON 0xFFFFFF

/* Tampon suffisant pour {"script":"<gcode>"} -- meme raisonnement que
 * GCODE_ARGS_MAX dans ecran_niveau_lit.c/ecran_deplacer.c/
 * ecran_reglage_fin.c/ecran_zcalibrate.c/ecran_temperatures.c/
 * ecran_extruder.c/ecran_ventilateurs.c. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_niveau_lit.c (voir son commentaire
 * complet sur pourquoi cJSON plutot qu'un snprintf a la main). Copie plutot
 * que partage, meme choix que le reste de ce depot. */
static bool construire_arguments_gcode(const char *script, char *sortie, size_t taille)
{
    if (script == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return false;
    }
    if (cJSON_AddStringToObject(racine, "script", script) == NULL) {
        cJSON_Delete(racine);
        return false;
    }
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return false;
    }
    size_t longueur = strlen(texte);
    bool ok = longueur < taille;
    if (ok) {
        memcpy(sortie, texte, longueur + 1);
    }
    cJSON_free(texte);
    return ok;
}

static void envoyer_gcode(const char *script)
{
    if (script == NULL) {
        return;
    }
    char arguments[GCODE_ARGS_MAX];
    if (!construire_arguments_gcode(script, arguments, sizeof(arguments))) {
        return; /* ne devrait jamais arriver : GCODE_ARGS_MAX suffit toujours */
    }
    ui_commander(BACKEND_ACTION_GCODE, arguments);
}

/* --- Table maitresse des six cases de navigation -- symbole du descripteur
 * cible, libelle affiche, ORDRE = ordre de remplissage de la grille (voir
 * ecran_actions.h pour les indices ECRAN_ACTIONS_CASE_*). "Disable Motors"
 * (une action gcode, pas une navigation) n'y figure pas -- traitee a part
 * plus bas. Valeurs EXACTES du brief de la tache, verbatim -- ASCII/anglais,
 * meme discipline que le reste de ce depot. ------------------------------ */
#define BOUTONS_NAV(X)                                                                                               \
    X(ECRAN_ACTIONS_CASE_MOVE, "Move", ECRAN_DEPLACER)                                                              \
    X(ECRAN_ACTIONS_CASE_EXTRUDE, "Extrude", ECRAN_EXTRUDER)                                                        \
    X(ECRAN_ACTIONS_CASE_FAN, "Fan", ECRAN_VENTILATEURS)                                                            \
    X(ECRAN_ACTIONS_CASE_TEMP, "Temperature", ECRAN_TEMPERATURES)                                                   \
    X(ECRAN_ACTIONS_CASE_MACROS, "Macros", ECRAN_MACROS)                                                            \
    X(ECRAN_ACTIONS_CASE_CONSOLE, "Console", ECRAN_CONSOLE)

/* Un rappel de clic par case de navigation -- signature imposee par
 * lv_obj_add_event_cb(), chacun ferme sur son propre
 * `navigation_empiler(&<symbole>)`. Echec (ESP_ERR_NO_MEM, pile deja pleine)
 * delibrement ignore, meme raison que menu_reglages_cb_##symbole() dans
 * ecran_menu_reglages.c : ce sous-menu n'a rien de plus utile a journaliser
 * qu'un simple "rien ne se passe", et la pile est bornee a
 * NAVIGATION_PROFONDEUR_MAX -- un actions-vers-panneau est toujours une
 * profondeur 2->3, jamais pres de cette borne en usage normal. */
#define DEFINIR_CB(indice, libelle, symbole)                                                                         \
    static void actions_cb_##symbole(lv_event_t *e)                                                                  \
    {                                                                                                                  \
        (void)e;                                                                                                       \
        navigation_empiler(&symbole);                                                                                  \
    }
BOUTONS_NAV(DEFINIR_CB)
#undef DEFINIR_CB

/* Rappel de clic de la septieme case ("Disable Motors") : envoi PUR de M84,
 * toujours sur, aucune confirmation -- voir le commentaire de tete pour
 * pourquoi (KlipperScreen ne le fait pas non plus). Echec de
 * klipper_gcode_niveau_lit() (ne devrait jamais arriver, KLIPPER_GCODE_MAX
 * suffit toujours a "M84") silencieusement ignore, meme discipline que le
 * reste de ce fichier. */
static void actions_cb_disable_motors(lv_event_t *e)
{
    (void)e;
    char script[KLIPPER_GCODE_MAX];
    if (klipper_gcode_niveau_lit(script, sizeof(script), KLIPPER_LIT_DISABLE)) {
        envoyer_gcode(script);
    }
}

typedef void (*actions_cb_t)(lv_event_t *e);

static const struct {
    const char  *libelle;
    actions_cb_t cb;
} ACTIONS_DEFS[ECRAN_ACTIONS_NB] = {
#define DEFINIR_ENTREE(indice, libelle, symbole) [indice] = {libelle, actions_cb_##symbole},
    BOUTONS_NAV(DEFINIR_ENTREE)
#undef DEFINIR_ENTREE
    [ECRAN_ACTIONS_CASE_DISABLE] = {"Disable Motors", actions_cb_disable_motors},
};

#undef BOUTONS_NAV

static void ecran_actions_construire(lv_obj_t *parent, void *contexte)
{
    ecran_actions_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- grille, 9 emplacements a taille fixe, 7 occupes (voir GRILLE_* en
     * tete de fichier) -- meme idiome que la grille de menu de
     * ecran_menu_reglages.c/ecran_accueil_hub.c. ------------------------- */
    ctx->zone_grille = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_grille);
    lv_obj_clear_flag(ctx->zone_grille, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_grille, LARGEUR_CONTENU - 2 * MARGE, GRILLE_HAUTEUR);
    lv_obj_set_pos(ctx->zone_grille, MARGE, GRILLE_Y);

    for (uint8_t i = 0; i < ECRAN_ACTIONS_NB; i++) {
        uint8_t ligne = i / GRILLE_COLONNES;
        uint8_t colonne = i % GRILLE_COLONNES;
        lv_coord_t x = (lv_coord_t)(colonne * (GRILLE_CELL_LARGEUR + GRILLE_ECART_COLONNE));
        lv_coord_t y = (lv_coord_t)(ligne * (GRILLE_CELL_HAUTEUR + GRILLE_ECART_LIGNE));

        lv_obj_t *bouton = lv_button_create(ctx->zone_grille);
        /* lv_obj_remove_style_all() : meme raison que dans
         * ecran_menu_reglages.c -- theme par defaut + transition animee
         * otes, pour ne pas alourdir style_trans_ll cote host-test. */
        lv_obj_remove_style_all(bouton);
        lv_obj_set_size(bouton, GRILLE_CELL_LARGEUR, GRILLE_CELL_HAUTEUR);
        lv_obj_set_pos(bouton, x, y);
        lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_border_width(bouton, 0, 0);
        lv_obj_set_style_shadow_width(bouton, 0, 0);
        lv_obj_set_style_radius(bouton, 10, 0);

        lv_obj_t *label = lv_label_create(bouton);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, ACTIONS_DEFS[i].libelle);
        lv_obj_center(label);

        lv_obj_add_event_cb(bouton, ACTIONS_DEFS[i].cb, LV_EVENT_CLICKED, NULL);

        ctx->boutons[i] = bouton;
    }
}

const ecran_desc_t ECRAN_ACTIONS = {
    .id = "actions",
    .titre = "Actions",
    .taille_contexte = sizeof(ecran_actions_ctx_t),
    .construire = ecran_actions_construire,
    .mettre_a_jour = NULL,
    .detruire = NULL,
};
