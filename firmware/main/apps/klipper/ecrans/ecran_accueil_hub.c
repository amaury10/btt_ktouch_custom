/* Implementation : voir ecran_accueil_hub.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : un resume
 * compact de QUATRE lignes en haut (temperatures, position + outil actif,
 * vitesse/flux, mini-progression d'impression), puis une grille de CINQ
 * tuiles de menu en dessous (Homing, Temperature, Actions, Configuration,
 * Print) -- geometrie verifiee par _Static_assert, meme discipline que
 * ecran_menu_reglages.c/ecran_deplacer.c.
 *
 * ECART delibere par rapport a l'ancien contenu de ce fichier (pool de
 * tuiles de temperature par palier, geometrie klipper_paliers.h) : le resume
 * n'a plus besoin d'accommoder jusqu'a 8 tetes en grand format -- une seule
 * ligne de texte tronquee (LV_LABEL_LONG_DOT) suffit pour un resume, le
 * reglage detaille restant derriere la tuile "Temperature" (ECRAN_TEMPERATURES).
 * Le resume est integralement en LECTURE SEULE : aucune de ses quatre lignes
 * n'est LV_OBJ_FLAG_CLICKABLE, contrairement a l'ancien pool de tuiles.
 *
 * Libelles de menu SANS accent ("Configuration", pas de caractere accentue)
 * -- aucun texte affiche a l'ecran dans ce depot n'utilise de caractere
 * accentue (voir "Nozzle target"/"Bed target"/rail.c "Accueil"/"Home"/
 * "Macros"/"STOP") -- les polices Montserrat embarquees (sdkconfig.defaults,
 * CONFIG_LV_FONT_MONTSERRAT_*) ne sont jamais garanties couvrir le Latin-1
 * Supplement, un glyphe manquant se rendrait en tofu silencieux. */
#include "ecran_accueil_hub.h"

#include <math.h>
#include <stdio.h>

#include "ecran_actions.h"       /* ECRAN_ACTIONS */
#include "ecran_fichiers.h"      /* ECRAN_FICHIERS */
#include "ecran_homing.h"        /* ECRAN_HOMING */
#include "ecran_menu_reglages.h" /* ECRAN_MENU_REGLAGES */
#include "ecran_temperatures.h"  /* ECRAN_TEMPERATURES */
#include "navigation.h"          /* navigation_empiler() */
#include "tuile.h"                /* ui_format_temperature() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE      14
#define ZONE_ECART 14 /* ecart vertical entre le resume et la grille de menu */

/* --- Resume : quatre lignes empilees, chacune un unique lv_label_t sur
 * TOUTE la largeur du contenu -- geometrie plate, derivee (RESUME_HAUTEUR),
 * pas recopiee, meme discipline que PROGRESSION_Y/BOUTONS_Y dans
 * ecran_accueil.c. ------------------------------------------------------- */
#define RESUME_Y          10
#define LIGNE_HAUTEUR      26
#define LIGNE_ECART         8 /* entre deux lignes du resume */

#define TEMPERATURES_Y RESUME_Y
#define POSITION_Y      (TEMPERATURES_Y + LIGNE_HAUTEUR + LIGNE_ECART)
#define VITESSE_FLUX_Y   (POSITION_Y + LIGNE_HAUTEUR + LIGNE_ECART)
#define PROGRESSION_Y    (VITESSE_FLUX_Y + LIGNE_HAUTEUR + LIGNE_ECART)

#define RESUME_HAUTEUR (PROGRESSION_Y + LIGNE_HAUTEUR - RESUME_Y)

/* --- Grille de menu : 5 colonnes x 1 ligne, sous le resume -- geometrie
 * propre a ce hub. 6 (et non un autre ecart) : avec LARGEUR_CONTENU=742, la
 * largeur utile de la grille vaut 714 ; il faut que (714 - 4*ecart) soit
 * divisible par 5 pour que les 5 colonnes la remplissent EXACTEMENT
 * (_Static_assert plus bas). 714 % 5 == 4 impose ecart % 5 == 1 (l'inverse de
 * 4 modulo 5 vaut 4, et 4*4 == 16 == 1 mod 5) ; 6 est le plus petit choix
 * >= 1 qui verifie ecart % 5 == 1 et laisse des cases largement >= 44px. --- */
#define MENU_COLONNES 5
#define MENU_ECART_COLONNE 6
#define MENU_CELL_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - (MENU_COLONNES - 1) * MENU_ECART_COLONNE) / MENU_COLONNES)
#define MENU_CELL_HAUTEUR 120 /* >= 44px cible tactile minimale, tuiles "principales" du hub */

#define MENU_ZONE_Y (RESUME_Y + RESUME_HAUTEUR + ZONE_ECART)

/* Meme convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_menu_reglages.c (voir son commentaire complet) :
 * bande couverte par le bandeau de notification de habillage.c, en
 * coordonnees ABSOLUES d'ecran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(MENU_CELL_HAUTEUR >= 44, "les tuiles de menu doivent rester >= 44px de cible tactile");
_Static_assert(MENU_COLONNES * MENU_CELL_LARGEUR + (MENU_COLONNES - 1) * MENU_ECART_COLONNE ==
                    LARGEUR_CONTENU - 2 * MARGE,
                "la grille de menu ne remplit plus exactement la largeur du contenu");
_Static_assert(MENU_ZONE_Y + MENU_CELL_HAUTEUR <= HAUTEUR_CONTENU,
                "le resume + la grille de menu debordent de la hauteur du contenu (436px)");
/* Meme garde-fou que la grille de menu de ecran_menu_reglages.c (voir son
 * commentaire complet) : le bas de la grille, en coordonnees ABSOLUES
 * d'ecran, doit rester au-dessus du bandeau de notification -- sans quoi une
 * notification recouvrirait ET bloquerait le tap sur les tuiles. */
_Static_assert(BARRE_HAUTEUR_ECRAN + MENU_ZONE_Y + MENU_CELL_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la grille de menu chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Tampon du texte de la ligne de temperatures -- large marge au-dela du pire
 * cas realiste (8 extrudeurs + plateau, chaque entree "T7 205.0/210.0"
 * separee par deux espaces) : le texte est de toute facon tronque a
 * l'affichage par LV_LABEL_LONG_DOT si la ligne deborde la largeur du
 * contenu, ce tampon n'a besoin que de ne jamais ecrire hors limites. */
#define TEMP_TEXTE_MAX 192

/* Ecrit "%.1f" si `reference` est vrai, "--" sinon -- copie de formater_axe()
 * de ecran_deplacer.c : ne jamais presenter comme mesuree une position
 * qu'aucun homing n'a etablie. Copie plutot que partagee, meme choix que le
 * reste de ce depot (fonction static, chaque ecran garde la sienne -- voir
 * le commentaire de tete de ecran_zcalibrate.c pour le meme choix ailleurs). */
static void formater_axe(char *sortie, size_t taille, float valeur, bool reference)
{
    if (!reference) {
        snprintf(sortie, taille, "--");
        return;
    }
    snprintf(sortie, taille, "%.1f", (double)valeur);
}

/* Construit la ligne de temperatures compacte : chaque extrudeur present
 * (T0, T1, ...) puis le plateau (Bed), "actuelle/consigne" via
 * ui_format_temperature(), separes par deux espaces. Append borne
 * defensivement (meme idiome que les constructeurs de gcode de
 * klipper_gcode.c : `ecrit < 0 || (size_t)ecrit >= reste` avant d'avancer
 * `pos`) -- un depassement du tampon coupe simplement le reste de la ligne,
 * jamais un acces hors limites ; la troncature visuelle finale reste de
 * toute facon a la charge de LV_LABEL_LONG_DOT sur le label lui-meme. */
static void construire_texte_temperatures(char *sortie, size_t taille, const etat_klipper_t *e,
                                           uint8_t nb_extrudeurs)
{
    if (sortie == NULL || taille == 0) {
        return;
    }
    sortie[0] = '\0';

    size_t pos = 0;
    bool   premier = true;
    char   valeur[16];
    char   consigne[16];

    for (uint8_t i = 0; i < nb_extrudeurs; i++) {
        if (!e->extrudeurs[i].presente) {
            continue;
        }
        ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[i].actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[i].consigne);
        int ecrit = snprintf(sortie + pos, taille - pos, "%sT%u %s/%s", premier ? "" : "  ", (unsigned)i, valeur,
                              consigne);
        if (ecrit < 0 || (size_t)ecrit >= taille - pos) {
            return;
        }
        pos += (size_t)ecrit;
        premier = false;
    }

    if (e->plateau.presente) {
        ui_format_temperature(valeur, sizeof(valeur), e->plateau.actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->plateau.consigne);
        int ecrit = snprintf(sortie + pos, taille - pos, "%sBed %s/%s", premier ? "" : "  ", valeur, consigne);
        if (ecrit < 0 || (size_t)ecrit >= taille - pos) {
            return;
        }
        pos += (size_t)ecrit;
    }
}

static lv_obj_t *ligne_resume_creer(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(label, "");
    lv_obj_set_pos(label, MARGE, y);
    return label;
}

/* --- Grille de menu : chaque tuile navigue reellement vers un ecran deja
 * construit par un jalon precedent -- aucune case no-op ici, contrairement a
 * l'ancien contenu de ce fichier a ses tout premiers jalons. Echec
 * (ESP_ERR_NO_MEM, pile deja pleine) delibrement ignore, meme raison que
 * menu_reglages_cb_##symbole() de ecran_menu_reglages.c : ce hub n'a rien de
 * plus utile a journaliser qu'un simple "rien ne se passe", et la pile est
 * bornee a NAVIGATION_PROFONDEUR_MAX -- un hub-vers-panneau est toujours une
 * profondeur 1->2, jamais pres de cette borne en usage normal. --------- */
static void ouvrir_homing_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_HOMING);
}

static void ouvrir_temperature_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_TEMPERATURES);
}

static void ouvrir_actions_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_ACTIONS);
}

static void ouvrir_configuration_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_MENU_REGLAGES);
}

static void ouvrir_print_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_FICHIERS);
}

typedef void (*menu_cb_t)(lv_event_t *e);

static const struct {
    const char *titre;
    menu_cb_t   cb;
} MENU_DEFS[ECRAN_ACCUEIL_HUB_MENU_NB] = {
    [ECRAN_ACCUEIL_HUB_MENU_HOMING]        = { "Homing",        ouvrir_homing_cb },
    [ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE]   = { "Temperature",   ouvrir_temperature_cb },
    [ECRAN_ACCUEIL_HUB_MENU_ACTIONS]       = { "Actions",       ouvrir_actions_cb },
    [ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION] = { "Configuration", ouvrir_configuration_cb },
    [ECRAN_ACCUEIL_HUB_MENU_PRINT]         = { "Print",         ouvrir_print_cb },
};

static void ecran_accueil_hub_construire(lv_obj_t *parent, void *contexte)
{
    ecran_accueil_hub_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- resume, quatre lignes en lecture seule -------------------------
     * Auto-dimensionnees (LV_SIZE_CONTENT par defaut de lv_label_create(),
     * comme la ligne de position de ecran_deplacer.c) : PAS de
     * LV_LABEL_LONG_DOT ici (essaye puis retire -- son calcul de troncature
     * lit les coordonnees RESOLUES de l'objet, qui exigent une passe de
     * layout entre le lv_obj_set_size() de construire() et le
     * lv_label_set_text() de mettre_a_jour() ; sans cette passe -- jamais
     * declenchee ici, construire()/mettre_a_jour() s'enchainent directement,
     * y compris cote host-test -- le calcul lit une taille perimee proche de
     * zero et tronque TOUT le texte en "..."). Une machine reelle a 1-2
     * extrudeurs (voir machines-klipper-reelles) tient tres largement dans
     * 714px a la police par defaut ; le cas extreme (8 extrudeurs) deborde
     * simplement du cadre visuellement plutot que de risquer ce piege. -- */
    ctx->temperatures = ligne_resume_creer(parent, TEMPERATURES_Y);
    ctx->position = ligne_resume_creer(parent, POSITION_Y);
    ctx->vitesse_flux = ligne_resume_creer(parent, VITESSE_FLUX_Y);
    ctx->progression = ligne_resume_creer(parent, PROGRESSION_Y);
    lv_obj_add_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN); /* visible seulement si impression_en_cours, voir mettre_a_jour() */

    /* --- grille de menu, 5 cases a taille fixe (voir MENU_* en tete de
     * fichier) --------------------------------------------------------- */
    ctx->zone_menu = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_menu);
    lv_obj_clear_flag(ctx->zone_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_menu, LARGEUR_CONTENU - 2 * MARGE, MENU_CELL_HAUTEUR);
    lv_obj_set_pos(ctx->zone_menu, MARGE, MENU_ZONE_Y);

    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        lv_coord_t x = (lv_coord_t)(i * (MENU_CELL_LARGEUR + MENU_ECART_COLONNE));

        lv_obj_t *bouton = lv_button_create(ctx->zone_menu);
        /* lv_obj_remove_style_all() : meme raison que dans
         * ecran_menu_reglages.c -- theme par defaut + transition animee
         * otes, pour ne pas alourdir style_trans_ll cote host-test. */
        lv_obj_remove_style_all(bouton);
        lv_obj_set_size(bouton, MENU_CELL_LARGEUR, MENU_CELL_HAUTEUR);
        lv_obj_set_pos(bouton, x, 0);
        lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_border_width(bouton, 0, 0);
        lv_obj_set_style_shadow_width(bouton, 0, 0);
        lv_obj_set_style_radius(bouton, 10, 0);

        lv_obj_t *label = lv_label_create(bouton);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, MENU_DEFS[i].titre);
        lv_obj_center(label);

        lv_obj_add_event_cb(bouton, MENU_DEFS[i].cb, LV_EVENT_CLICKED, NULL);

        ctx->menu_boutons[i] = bouton;
    }
}

static void ecran_accueil_hub_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_accueil_hub_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme, meme garde que l'ancien
     * contenu de ce fichier. */
    uint8_t nb_extrudeurs = e->nb_extrudeurs;
    if (nb_extrudeurs > KLIPPER_EXTRUDEURS_MAX) {
        nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
    }

    /* --- ligne de temperatures ------------------------------------------ */
    char texte_temperatures[TEMP_TEXTE_MAX];
    construire_texte_temperatures(texte_temperatures, sizeof(texte_temperatures), e, nb_extrudeurs);
    lv_label_set_text(ctx->temperatures, texte_temperatures);

    /* --- ligne de position + outil actif --------------------------------
     * meme idiome que ecran_deplacer_mettre_a_jour() : "--" si l'axe n'a
     * jamais ete reference par un homing. */
    bool axe_x_reference = (e->axes_references & 0x1u) != 0;
    bool axe_y_reference = (e->axes_references & 0x2u) != 0;
    bool axe_z_reference = (e->axes_references & 0x4u) != 0;
    char pos_x[8];
    char pos_y[8];
    char pos_z[8];
    formater_axe(pos_x, sizeof(pos_x), e->position[0], axe_x_reference);
    formater_axe(pos_y, sizeof(pos_y), e->position[1], axe_y_reference);
    formater_axe(pos_z, sizeof(pos_z), e->position[2], axe_z_reference);
    char texte_position[48];
    if (nb_extrudeurs > 0) {
        snprintf(texte_position, sizeof(texte_position), "X:%s Y:%s Z:%s  T%u", pos_x, pos_y, pos_z,
                  (unsigned)e->outil_actif);
    } else {
        snprintf(texte_position, sizeof(texte_position), "X:%s Y:%s Z:%s  T--", pos_x, pos_y, pos_z);
    }
    lv_label_set_text(ctx->position, texte_position);

    /* --- ligne vitesse / flux -------------------------------------------- */
    char texte_vitesse_flux[32];
    snprintf(texte_vitesse_flux, sizeof(texte_vitesse_flux), "Speed: %u%%  Flow: %u%%", (unsigned)e->vitesse_pct,
              (unsigned)e->flux_pct);
    lv_label_set_text(ctx->vitesse_flux, texte_vitesse_flux);

    /* --- mini-progression, visible SEULEMENT si une impression est en
     * cours -- re-evaluee a CHAQUE appel (systematique, meme discipline que
     * bouton_macros dans ecran_accueil.c) : une impression qui se termine ne
     * doit pas laisser cette ligne visible sur la foi d'un etat perime.
     * isnan()/isinf() d'abord, meme garde-fou que progression_definir()
     * (progression.c) -- `e->progression` vient de Moonraker, une source qui
     * peut renvoyer n'importe quoi pendant un redemarrage de klippy. --------- */
    if (e->impression_en_cours) {
        float fraction = e->progression;
        if (isnan(fraction) || isinf(fraction)) {
            fraction = (isinf(fraction) && fraction > 0.0f) ? 1.0f : 0.0f;
        } else if (fraction < 0.0f) {
            fraction = 0.0f;
        } else if (fraction > 1.0f) {
            fraction = 1.0f;
        }
        unsigned pct = (unsigned)(fraction * 100.0f + 0.5f);
        char texte_progression[24];
        snprintf(texte_progression, sizeof(texte_progression), "Printing: %u%%", pct);
        lv_label_set_text(ctx->progression, texte_progression);
        lv_obj_clear_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN);
    }

    /* --- grisage integral du resume, style RESOLU (spec C3, ecran.h) --
     * systematique a chaque appel, jamais incremental (meme lecon que
     * tuile_griser()/l'ancien contenu de ce fichier). La grille de menu,
     * elle, N'EST JAMAIS grisee -- meme choix delibere que la grille de
     * l'ancien hub ("naviguer... reste sans danger meme avec un etat
     * perime") : chaque tuile ouvre un ecran qui grise lui-meme son propre
     * contenu si besoin, et Homing/Actions/Configuration restent sans
     * danger a atteindre meme hors ligne. -------------------------------- */
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(ctx->temperatures, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->position, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->vitesse_flux, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->progression, lv_color_hex(couleur), 0);
}

const ecran_desc_t ECRAN_ACCUEIL_HUB = {
    .id = "accueil_hub",
    .titre = "Home",
    .taille_contexte = sizeof(ecran_accueil_hub_ctx_t),
    .construire = ecran_accueil_hub_construire,
    .mettre_a_jour = ecran_accueil_hub_mettre_a_jour,
    .detruire = NULL,
};
