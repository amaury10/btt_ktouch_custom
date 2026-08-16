/* Implementation : voir ecran_menu_reglages.h pour le contrat et la note sur
 * la collision de noms avec ECRAN_CONFIGURATION (ecran de PREMIERE
 * configuration, jamais touche ici).
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : une seule
 * grille de 12 cases (3 colonnes x 4 lignes, ONZE peuplees -- voir
 * ecran_menu_reglages.h, Fine Tune est parti vers ecran_accueil.c),
 * CENTREE verticalement dans le contenu -- aucune zone de temperature
 * au-dessus, contrairement au hub (ecran_accueil_hub.c), dont ce fichier
 * reprend neanmoins l'idiome de grille a cases fixes (position calculee,
 * `_Static_assert` de non-debordement + clearance du bandeau).
 *
 * BOUTONS(X) liste les douze cases (indice, libelle, symbole ECRAN_*) -- X()
 * est instanciee deux fois plus bas : une premiere pour generer les onze
 * rappels de clic `menu_reglages_cb_<symbole>()` (chacun ferme sur son propre
 * `navigation_empiler(&<symbole>)`, meme X-macro que STUBS() dans
 * l'ancien ecran_stub.c, supprime avec son dernier stub), une seconde pour
 * peupler la table MENU_REGLAGES_DEFS (paire
 * libelle/rappel) lue par la boucle de construction. Menu purement statique :
 * `mettre_a_jour = NULL` (rien a rafraichir), `detruire = NULL` (rien a
 * liberer au-dela du contexte -- voir ecran.h). */
#include "ecran_menu_reglages.h"

#include "ecran_parc.h" /* ECRAN_PARC -- gestion de parc (2026-08-15) */

#include "ecran_console.h"       /* ECRAN_CONSOLE (feature "Console gcode", tache B) */
#include "ecran_limites.h"       /* ECRAN_LIMITES */
#include "ecran_niveau_lit.h"    /* ECRAN_NIVEAU_LIT */
#include "ecran_power.h"         /* ECRAN_POWER (feature "Power devices Moonraker", tache B) */
#include "ecran_reglages_wifi.h" /* ECRAN_REGLAGES_WIFI */
#include "ecran_retraction.h"    /* ECRAN_RETRACTION */
#include "ecran_bed_mesh.h"      /* ECRAN_BED_MESH -- plus un stub (2026-08-15) */
#include "ecran_input_shaper.h"  /* ECRAN_INPUT_SHAPER -- plus un stub (2026-08-15) */
#include "ecran_spoolman.h"      /* ECRAN_SPOOLMAN (dernier stub remplace, 2026-08-15) */
#include "ecran_updater.h"       /* ECRAN_UPDATER (Task 2, jalon OTA firmware -- plus un stub) */
#include "ecran_zcalibrate.h"    /* ECRAN_ZCALIBRATE */
#include "navigation.h"          /* navigation_empiler() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE 14

/* --- Grille : 3 colonnes x 4 lignes -- geometrie propre a ce sous-menu (pas
 * de zone de temperature au-dessus, contrairement au hub). --------------- */
#define GRILLE_COLONNES 3
#define GRILLE_LIGNES   4
/* Meme raisonnement que MENU_ECART_COLONNE dans ecran_accueil_hub.c : avec
 * LARGEUR_CONTENU=742, la largeur utile de la grille vaut 714 ; il faut que
 * (714 - 2*ecart) soit divisible par 3 pour que les 3 colonnes la remplissent
 * EXACTEMENT (_Static_assert plus bas). 714 % 3 == 0 impose ecart % 3 == 0 ;
 * 9 est le meme ajustement minimal depuis 8 que dans le hub (cases de 232px,
 * largement >= 44px). */
#define GRILLE_ECART_COLONNE 9
#define GRILLE_ECART_LIGNE   8

#define GRILLE_CELL_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - (GRILLE_COLONNES - 1) * GRILLE_ECART_COLONNE) / GRILLE_COLONNES)
#define GRILLE_CELL_HAUTEUR 52 /* >= 44px cible tactile minimale, largement -- meme valeur que MENU_CELL_HAUTEUR du hub */
#define GRILLE_HAUTEUR (GRILLE_LIGNES * GRILLE_CELL_HAUTEUR + (GRILLE_LIGNES - 1) * GRILLE_ECART_LIGNE)

/* Grille centree verticalement dans le contenu (204px de marge totale au-dessus
 * + en dessous des 12 cases, contre 436px de hauteur de contenu) -- choix
 * esthetique, aucune contrainte de layout ne l'impose ici (contrairement au
 * hub, qui doit reserver ZONE_TEMP_HAUTEUR_MAX au-dessus). */
#define GRILLE_Y ((HAUTEUR_CONTENU - GRILLE_HAUTEUR) / 2)

/* Meme convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_accueil_hub.c (voir son commentaire complet) :
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
/* Meme garde-fou que la grille de menu de ecran_accueil_hub.c (voir son
 * commentaire complet) : le bas de la grille, en coordonnees ABSOLUES
 * d'ecran, doit rester au-dessus du bandeau de notification -- sans quoi une
 * notification recouvrirait ET bloquerait le tap sur les cases. */
_Static_assert(BARRE_HAUTEUR_ECRAN + GRILLE_Y + GRILLE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la grille chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND         0x10161D
#define COULEUR_BOUTON       0x2A3644
#define COULEUR_TEXTE_BOUTON 0xFFFFFF

/* --- Table maitresse des onze cases -- symbole du descripteur cible de
 * chaque case, libelle affiche, ORDRE = ordre de remplissage de la grille
 * (rangee par rangee, voir ecran_menu_reglages.h pour les indices
 * ECRAN_MENU_REGLAGES_CASE_*). Valeurs EXACTES du brief de la tache,
 * verbatim -- ASCII/anglais, meme discipline que le reste de ce depot (voir
 * ecran_accueil_hub.c, "Libelles de menu SANS accent"). ------------------- */
#define BOUTONS(X)                                                                                                    \
    X(ECRAN_MENU_REGLAGES_CASE_ZCALIBRATE, "Z Calibrate", ECRAN_ZCALIBRATE)                                           \
    X(ECRAN_MENU_REGLAGES_CASE_BED_LEVEL, "Bed Level", ECRAN_NIVEAU_LIT)                                              \
    X(ECRAN_MENU_REGLAGES_CASE_LIMITS, "Limits", ECRAN_LIMITES)                                                       \
    X(ECRAN_MENU_REGLAGES_CASE_RETRACTION, "Retraction", ECRAN_RETRACTION)                                            \
    X(ECRAN_MENU_REGLAGES_CASE_NETWORK, "Network", ECRAN_REGLAGES_WIFI)                                               \
    X(ECRAN_MENU_REGLAGES_CASE_POWER, "Power", ECRAN_POWER)                                                           \
    X(ECRAN_MENU_REGLAGES_CASE_BED_MESH, "Bed Mesh", ECRAN_BED_MESH)                                                  \
    X(ECRAN_MENU_REGLAGES_CASE_INPUT_SHAPER, "Input Shaper", ECRAN_INPUT_SHAPER)                                      \
    X(ECRAN_MENU_REGLAGES_CASE_SPOOLMAN, "Spoolman", ECRAN_SPOOLMAN)                                                  \
    X(ECRAN_MENU_REGLAGES_CASE_UPDATER, "Updater", ECRAN_UPDATER)                                                     \
    X(ECRAN_MENU_REGLAGES_CASE_CONSOLE, "Console", ECRAN_CONSOLE)                                                     \
    X(ECRAN_MENU_REGLAGES_CASE_PRINTERS, "Printers", ECRAN_PARC)

/* Un rappel de clic par case -- signature imposee par lv_obj_add_event_cb(),
 * chacun ferme sur son propre `navigation_empiler(&<symbole>)`. Echec
 * (ESP_ERR_NO_MEM, pile deja pleine) delibrement ignore, meme raison que
 * menu_deplacer_cb() dans ecran_accueil_hub.c : ce sous-menu n'a rien de plus
 * utile a journaliser qu'un simple "rien ne se passe", et la pile est bornee
 * a NAVIGATION_PROFONDEUR_MAX -- un menu_reglages-vers-panneau est toujours
 * une profondeur 2->3 (voire 3->4 pour Network, qui ouvre un clavier),
 * jamais pres de cette borne en usage normal (voir task-8-brief.md, "note
 * de profondeur de navigation"). */
#define DEFINIR_CB(indice, libelle, symbole)                                                                          \
    static void menu_reglages_cb_##symbole(lv_event_t *e)                                                            \
    {                                                                                                                  \
        (void)e;                                                                                                       \
        navigation_empiler(&symbole);                                                                                  \
    }
BOUTONS(DEFINIR_CB)
#undef DEFINIR_CB

typedef void (*menu_reglages_cb_t)(lv_event_t *e);

static const struct {
    const char        *libelle;
    menu_reglages_cb_t cb;
} MENU_REGLAGES_DEFS[ECRAN_MENU_REGLAGES_NB] = {
#define DEFINIR_ENTREE(indice, libelle, symbole) [indice] = {libelle, menu_reglages_cb_##symbole},
    BOUTONS(DEFINIR_ENTREE)
#undef DEFINIR_ENTREE
};

#undef BOUTONS

static void ecran_menu_reglages_construire(lv_obj_t *parent, void *contexte)
{
    ecran_menu_reglages_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- grille, 12 cases a taille fixe (ONZE peuplees, voir GRILLE_* en
     * tete de fichier) -- meme idiome que la grille de menu de
     * ecran_accueil_hub.c. La boucle ci-dessous s'arrete a
     * ECRAN_MENU_REGLAGES_NB (11) : la douzieme case (derniere rangee)
     * reste simplement vide, aucune retouche de geometrie necessaire. ----- */
    ctx->zone_grille = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_grille);
    lv_obj_clear_flag(ctx->zone_grille, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_grille, LARGEUR_CONTENU - 2 * MARGE, GRILLE_HAUTEUR);
    lv_obj_set_pos(ctx->zone_grille, MARGE, GRILLE_Y);

    for (uint8_t i = 0; i < ECRAN_MENU_REGLAGES_NB; i++) {
        uint8_t ligne = i / GRILLE_COLONNES;
        uint8_t colonne = i % GRILLE_COLONNES;
        lv_coord_t x = (lv_coord_t)(colonne * (GRILLE_CELL_LARGEUR + GRILLE_ECART_COLONNE));
        lv_coord_t y = (lv_coord_t)(ligne * (GRILLE_CELL_HAUTEUR + GRILLE_ECART_LIGNE));

        lv_obj_t *bouton = lv_button_create(ctx->zone_grille);
        /* lv_obj_remove_style_all() : meme raison que dans
         * ecran_accueil_hub.c -- theme par defaut + transition animee otes,
         * pour ne pas alourdir style_trans_ll cote host-test. */
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
        lv_label_set_text(label, MENU_REGLAGES_DEFS[i].libelle);
        lv_obj_center(label);

        lv_obj_add_event_cb(bouton, MENU_REGLAGES_DEFS[i].cb, LV_EVENT_CLICKED, NULL);

        ctx->boutons[i] = bouton;
    }
}

const ecran_desc_t ECRAN_MENU_REGLAGES = {
    .id = "menu_reglages",
    .titre = "Configuration",
    .taille_contexte = sizeof(ecran_menu_reglages_ctx_t),
    .construire = ecran_menu_reglages_construire,
    .mettre_a_jour = NULL,
    .detruire = NULL,
};
