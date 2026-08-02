/* Implementation : voir ecran_accueil_hub.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : DEUX
 * colonnes cote a cote. La colonne GAUCHE porte les lignes de chauffants
 * (nom + valeur, ECRAN_ACCUEIL_HUB_HEATER_LIGNES au plus), un resume compact
 * (position + outil actif, vitesse/flux, mini-progression) puis un `lv_chart`
 * d'historique de temperature qui occupe le reste de la colonne. La colonne
 * DROITE porte les cinq tuiles de menu, empilees VERTICALEMENT (contre une
 * rangee horizontale sous le resume dans l'ancienne mise en page une
 * colonne) -- geometrie verifiee par _Static_assert, meme discipline que
 * ecran_menu_reglages.c/ecran_deplacer.c.
 *
 * ECART delibere par rapport a l'ancien contenu de ce fichier (resume quatre
 * lignes pleine largeur + grille 5 cases EN DESSOUS) : le sous-projet
 * "graphes de temperature" (task-3-brief.md) demande une refonte en deux
 * colonnes pour loger le graphe d'historique (klipper_temp_historique.h,
 * tache 1 du meme sous-projet) sans repousser la grille de menu hors de
 * l'ecran. Toutes les constantes de geometrie sont DERIVEES (jamais
 * recopiees) et verifiees les unes par rapport aux autres, meme idiome que
 * ecran_deplacer.c.
 *
 * Lignes de chauffants EN LECTURE SEULE (aucun tap) : voir le commentaire de
 * tete du .h pour pourquoi nom/valeur sont deux `lv_label_t` distincts des
 * cette tache-ci plutot qu'un texte concatene -- la tache 4/5 du meme
 * sous-projet posera LV_OBJ_FLAG_CLICKABLE sur chaque paire sans retoucher
 * cette mise en page.
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
#include "klipper_temp_historique.h"
#include "navigation.h" /* navigation_empiler() */
#include "tuile.h"      /* ui_format_temperature() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
/* Pas de HAUTEUR_CONTENU ici (contrairement a l'ancien contenu de ce
 * fichier) : ZONE_CONTENU_MAX plus bas (derivee de BANDEAU_Y_ECRAN, plus
 * stricte) est le SEUL plafond vertical dont ce fichier a besoin -- le
 * conserver en plus aurait ete une constante jamais lue. */

#define MARGE    14
#define COL_ECART 14 /* ecart horizontal entre les deux colonnes */

/* --- Deux colonnes qui remplissent exactement la largeur du contenu
 * (_Static_assert plus bas) -- 350px chacune, le meme partage a peu pres
 * egal (~360px) que demande task-3-brief.md. -------------------------- */
#define GAUCHE_X       MARGE
#define GAUCHE_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - COL_ECART) / 2)
#define DROITE_X       (GAUCHE_X + GAUCHE_LARGEUR + COL_ECART)
#define DROITE_LARGEUR (LARGEUR_CONTENU - MARGE - DROITE_X)

/* Meme convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_menu_reglages.c (voir son commentaire complet) :
 * bande couverte par le bandeau de notification de habillage.c, en
 * coordonnees ABSOLUES d'ecran. ZONE_CONTENU_MAX (DERIVEE) est le plafond
 * relatif au contenu (jamais recopie) sous lequel les DEUX colonnes doivent
 * rester -- task-3-brief.md : "the bottom of each column stays above the
 * banner". */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN     (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)
#define ZONE_CONTENU_MAX    (BANDEAU_Y_ECRAN - BARRE_HAUTEUR_ECRAN)

#define CONTENU_Y 6 /* Y de depart commun aux deux colonnes */

/* --- Colonne gauche : lignes de chauffants -------------------------------
 * Nom ("T0"/"Bed", largeur fixe courte) + valeur ("205.0/210.0", le reste de
 * la colonne) sur la MEME ligne, deux lv_label_t distincts et adjacents (voir
 * le commentaire de tete du .h). --------------------------------------- */
#define CHAUFFANT_LIGNE_HAUTEUR 20
#define CHAUFFANT_LIGNE_ECART    2
#define CHAUFFANT_NOM_LARGEUR   50
#define CHAUFFANT_VALEUR_X      (CHAUFFANT_NOM_LARGEUR + 10)
#define CHAUFFANT_VALEUR_LARGEUR (GAUCHE_LARGEUR - CHAUFFANT_VALEUR_X)

#define CHAUFFANTS_ZONE_Y      CONTENU_Y
#define CHAUFFANTS_ZONE_HAUTEUR (ECRAN_ACCUEIL_HUB_HEATER_LIGNES * CHAUFFANT_LIGNE_HAUTEUR + \
                                  (ECRAN_ACCUEIL_HUB_HEATER_LIGNES - 1) * CHAUFFANT_LIGNE_ECART)

/* --- Colonne gauche : resume (position/outil, vitesse-flux, progression) -- */
#define ZONE_ECART   8 /* ecart vertical entre les "blocs" de la colonne gauche */
#define LIGNE_HAUTEUR 20
#define LIGNE_ECART    2

#define POSITION_Y      (CHAUFFANTS_ZONE_Y + CHAUFFANTS_ZONE_HAUTEUR + ZONE_ECART)
#define VITESSE_FLUX_Y  (POSITION_Y + LIGNE_HAUTEUR + LIGNE_ECART)
#define PROGRESSION_Y   (VITESSE_FLUX_Y + LIGNE_HAUTEUR + LIGNE_ECART)

/* --- Colonne gauche : graphe -- occupe tout le reste de la colonne jusqu'au
 * plafond ZONE_CONTENU_MAX (DERIVEE, jamais un nombre choisi a la main) --
 * c'est ce qui rend le graphe "aussi grand que possible" sans jamais
 * chevaucher le bandeau de notification. -------------------------------- */
#define CHART_Y       (PROGRESSION_Y + LIGNE_HAUTEUR + ZONE_ECART)
#define CHART_HAUTEUR (ZONE_CONTENU_MAX - CHART_Y)

/* --- Colonne droite : cinq tuiles empilees verticalement, meme hauteur
 * fixe -- TUILE_HAUTEUR est DERIVEE pour que les cinq tuiles + les quatre
 * ecarts remplissent EXACTEMENT la meme plage verticale que la colonne
 * gauche (CONTENU_Y .. ZONE_CONTENU_MAX), _Static_assert plus bas. -------- */
#define TUILE_ECART            10
#define DROITE_ZONE_HAUTEUR    (ZONE_CONTENU_MAX - CONTENU_Y)
#define TUILE_HAUTEUR           ((DROITE_ZONE_HAUTEUR - (ECRAN_ACCUEIL_HUB_MENU_NB - 1) * TUILE_ECART) / \
                                  ECRAN_ACCUEIL_HUB_MENU_NB)

_Static_assert(MARGE + GAUCHE_LARGEUR + COL_ECART + DROITE_LARGEUR + MARGE == LARGEUR_CONTENU,
                "les deux colonnes ne remplissent plus exactement la largeur du contenu");
_Static_assert(TUILE_HAUTEUR >= 44, "les tuiles de menu doivent rester >= 44px de cible tactile");
_Static_assert(ECRAN_ACCUEIL_HUB_MENU_NB * TUILE_HAUTEUR + (ECRAN_ACCUEIL_HUB_MENU_NB - 1) * TUILE_ECART ==
                    DROITE_ZONE_HAUTEUR,
                "les cinq tuiles ne remplissent plus exactement la hauteur disponible de la colonne droite");
_Static_assert(CHART_HAUTEUR > 0, "le graphe ne laisse plus de hauteur disponible dans la colonne gauche");
/* Meme garde-fou que la grille de menu de ecran_menu_reglages.c (voir son
 * commentaire complet) : le bas de CHAQUE colonne, en coordonnees ABSOLUES
 * d'ecran, doit rester au-dessus du bandeau de notification -- sans quoi une
 * notification recouvrirait le graphe ET bloquerait le tap sur les tuiles. */
_Static_assert(BARRE_HAUTEUR_ECRAN + CHART_Y + CHART_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la colonne gauche (graphe) chevauche la bande du bandeau de notification de l'habillage");
_Static_assert(BARRE_HAUTEUR_ECRAN + CONTENU_Y + DROITE_ZONE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la colonne droite (tuiles) chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_FOND_CHART       0x1B2430
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Une couleur distincte par serie du graphe (task-3-brief.md : "distinct
 * color per series"), indexee EXACTEMENT comme klipper_temp_historique.h
 * (0..KLIPPER_EXTRUDEURS_MAX-1 = extrudeurs, KLIPPER_EXTRUDEURS_MAX =
 * plateau) -- le plateau recoit une teinte froide (bleu ardoise) qui ne
 * ressemble a aucune couleur d'extrudeur, meme si un pire cas a 8 extrudeurs
 * finit par reutiliser un ton proche (aucune palette de 9 couleurs n'est
 * parfaitement distinguable a l'oeil, hors de portee de cette tache). */
static const uint32_t COULEURS_SERIE[KLIPPER_HISTO_SERIES] = {
    0xEF4444, /* T0 rouge */
    0xF59E0B, /* T1 ambre */
    0xEAB308, /* T2 jaune */
    0x22C55E, /* T3 vert */
    0x14B8A6, /* T4 sarcelle */
    0x3B82F6, /* T5 bleu */
    0x8B5CF6, /* T6 violet */
    0xEC4899, /* T7 rose */
    0x94A3B8, /* Bed (indice KLIPPER_EXTRUDEURS_MAX) : bleu ardoise */
};

/* Bornes Y du graphe (task-3-brief.md : "sensible Y range, e.g. 0..300") --
 * couvre la plage realiste d'une buse (jusqu'a ~300 C) et d'un plateau (bien
 * en dessous), sans avoir besoin de s'adapter dynamiquement aux valeurs
 * courantes -- un axe qui bouge rendrait la courbe plus dure a lire d'un
 * coup d'oeil que quelques pixels "perdus" en bas du graphe. */
#define CHART_Y_MIN 0
#define CHART_Y_MAX 300

/* Cree la serie `i` sur le chart (couleur COULEURS_SERIE[i]) et la backfille
 * depuis le store -- factorisee entre construire() ET mettre_a_jour() : au
 * BOOT REEL (app_main.c), navigation_empiler(&ECRAN_ACCUEIL_HUB) tourne
 * AVANT que le minuteur d'echantillonnage (echantillon_temp_cb(), periode
 * 5 s) n'ait jamais pousse le moindre point -- klipper_temp_historique_serie_presente()
 * rend donc FALSE pour toutes les series au moment de construire(), et aucune
 * n'y est ajoutee. Sans ce rattrapage cote mettre_a_jour() (plus bas), le
 * chart resterait vide A JAMAIS : son propre garde-fou de generation
 * n'ajoute jamais de point a une serie qui n'existe pas encore. Verifie
 * empiriquement par capture --scenario 11 (U1, chauffants non nuls) pendant
 * l'implementation de cette tache -- le graphe restait plat sans ce
 * correctif. `tampon` LOCAL, jamais une copie du store entier -- meme regle
 * que le backfill de construire(). */
static void chart_ajouter_serie(ecran_accueil_hub_ctx_t *ctx, uint8_t i)
{
    ctx->serie[i] = lv_chart_add_series(ctx->chart, lv_color_hex(COULEURS_SERIE[i]), LV_CHART_AXIS_PRIMARY_Y);
    if (ctx->serie[i] == NULL) {
        return; /* ne devrait jamais arriver hors epuisement memoire LVGL */
    }
    int16_t tampon[KLIPPER_HISTO_POINTS];
    size_t n = klipper_temp_historique_serie(i, tampon, KLIPPER_HISTO_POINTS);
    for (size_t k = 0; k < n; k++) {
        lv_chart_set_next_value(ctx->chart, ctx->serie[i], tampon[k]);
    }
}

/* Ecrit "%.1f" si `reference` est vrai, "--" sinon -- copie de formater_axe()
 * de ecran_deplacer.c : ne jamais presenter comme mesuree une position
 * qu'aucun homing n'a etablie. Copie plutot que partagee, meme choix que le
 * reste de ce depot (voir le commentaire de tete de ecran_zcalibrate.c pour
 * le meme choix ailleurs). */
static void formater_axe(char *sortie, size_t taille, float valeur, bool reference)
{
    if (!reference) {
        snprintf(sortie, taille, "--");
        return;
    }
    snprintf(sortie, taille, "%.1f", (double)valeur);
}

static lv_obj_t *ligne_creer(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(label, "");
    lv_obj_set_pos(label, GAUCHE_X, y);
    return label;
}

/* --- Colonne droite : chaque tuile navigue reellement vers un ecran deja
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

    /* --- colonne gauche : lignes de chauffants, lecture seule (voir le
     * commentaire de tete du .h) -- pool a taille fixe
     * (ECRAN_ACCUEIL_HUB_HEATER_LIGNES), masque/rempli par mettre_a_jour()
     * selon le nombre de chauffants reellement presents. ------------------ */
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        lv_coord_t y = (lv_coord_t)(CHAUFFANTS_ZONE_Y + i * (CHAUFFANT_LIGNE_HAUTEUR + CHAUFFANT_LIGNE_ECART));

        lv_obj_t *nom = lv_label_create(parent);
        lv_obj_set_style_text_font(nom, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(nom, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
        lv_label_set_text(nom, "");
        lv_obj_set_pos(nom, GAUCHE_X, y);
        lv_obj_set_width(nom, CHAUFFANT_NOM_LARGEUR);
        ctx->chauffant_nom[i] = nom;

        lv_obj_t *valeur = lv_label_create(parent);
        lv_obj_set_style_text_font(valeur, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(valeur, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
        lv_label_set_text(valeur, "");
        lv_obj_set_pos(valeur, GAUCHE_X + CHAUFFANT_VALEUR_X, y);
        lv_obj_set_width(valeur, CHAUFFANT_VALEUR_LARGEUR);
        ctx->chauffant_valeur[i] = valeur;
    }

    /* --- colonne gauche : resume, trois lignes en lecture seule -- Voir le
     * commentaire de tete de l'ancien contenu de ce fichier (git, avant
     * cette reecriture) pour pourquoi PAS de LV_LABEL_LONG_DOT ici : son
     * calcul de troncature lit les coordonnees RESOLUES de l'objet, qui
     * exigent une passe de layout entre construire() et le premier
     * mettre_a_jour() -- jamais declenchee ici, les deux s'enchainent
     * directement y compris cote host-test. --------------------------- */
    ctx->position = ligne_creer(parent, POSITION_Y);
    ctx->vitesse_flux = ligne_creer(parent, VITESSE_FLUX_Y);
    ctx->progression = ligne_creer(parent, PROGRESSION_Y);
    lv_obj_add_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN); /* visible seulement si impression_en_cours, voir mettre_a_jour() */

    /* --- colonne gauche : graphe d'historique -- une serie par chauffant
     * PRESENT au moment de cet appel (jamais ajoutee/retiree ensuite, voir
     * le commentaire de tete du .h), backfillee depuis le store (tache 1)
     * SANS JAMAIS copier le tampon entier -- un seul tampon LOCAL de
     * KLIPPER_HISTO_POINTS points (240 octets), reutilise serie par serie. */
    ctx->chart = lv_chart_create(parent);
    lv_obj_remove_style_all(ctx->chart);
    lv_obj_set_style_bg_color(ctx->chart, lv_color_hex(COULEUR_FOND_CHART), 0);
    lv_obj_set_style_bg_opa(ctx->chart, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->chart, 8, 0);
    /* Largeur de trait seule : la couleur de LV_PART_ITEMS est de toute
     * facon ecrasee par la couleur PROPRE de chaque serie au dessin (voir
     * lv_chart.c, chart_draw_series_line() -- `line_dsc.color = ser->color`
     * inconditionnel), la fixer ici serait une configuration morte. */
    lv_obj_set_style_line_width(ctx->chart, 2, LV_PART_ITEMS);
    lv_obj_set_pos(ctx->chart, GAUCHE_X, CHART_Y);
    lv_obj_set_size(ctx->chart, GAUCHE_LARGEUR, CHART_HAUTEUR);
    lv_chart_set_type(ctx->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ctx->chart, KLIPPER_HISTO_POINTS);
    lv_chart_set_update_mode(ctx->chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y, CHART_Y_MIN, CHART_Y_MAX);
    lv_chart_set_div_line_count(ctx->chart, 3, 0);

    for (uint8_t i = 0; i < KLIPPER_HISTO_SERIES; i++) {
        if (klipper_temp_historique_serie_presente(i)) {
            chart_ajouter_serie(ctx, i);
        } else {
            ctx->serie[i] = NULL; /* rattrapee par mettre_a_jour() si ce chauffant apparait plus tard, voir chart_ajouter_serie() */
        }
    }
    /* Capturee APRES le backfill : le premier mettre_a_jour() ne doit pas
     * re-ajouter le dernier point que le backfill vient deja d'inclure. */
    ctx->derniere_gen = klipper_temp_historique_generation();

    /* --- colonne droite : cinq tuiles empilees verticalement (voir
     * ECRAN_ACCUEIL_HUB_MENU_* -- meme idiome que la grille de menu de
     * l'ancien contenu de ce fichier, une seule colonne au lieu d'une
     * rangee). ------------------------------------------------------------ */
    ctx->zone_menu = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_menu);
    lv_obj_clear_flag(ctx->zone_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_menu, DROITE_LARGEUR, DROITE_ZONE_HAUTEUR);
    lv_obj_set_pos(ctx->zone_menu, DROITE_X, CONTENU_Y);

    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        lv_coord_t y = (lv_coord_t)(i * (TUILE_HAUTEUR + TUILE_ECART));

        lv_obj_t *bouton = lv_button_create(ctx->zone_menu);
        /* lv_obj_remove_style_all() : meme raison que dans
         * ecran_menu_reglages.c -- theme par defaut + transition animee
         * otes, pour ne pas alourdir style_trans_ll cote host-test. */
        lv_obj_remove_style_all(bouton);
        lv_obj_set_size(bouton, DROITE_LARGEUR, TUILE_HAUTEUR);
        lv_obj_set_pos(bouton, 0, y);
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

    /* --- lignes de chauffants : extrudeurs presents puis plateau, dans cet
     * ordre, bornes a ECRAN_ACCUEIL_HUB_HEATER_LIGNES -- systematique a
     * chaque appel (les lignes au-dela du nombre present sont masquees,
     * jamais un etat fige depuis le premier passage). --------------------- */
    uint8_t total = 0;
    char    valeur[16];
    char    consigne[16];
    char    nom[8];
    char    texte_valeur[40];

    for (uint8_t i = 0; i < nb_extrudeurs && total < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        if (!e->extrudeurs[i].presente) {
            continue;
        }
        snprintf(nom, sizeof(nom), "T%u", (unsigned)i);
        ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[i].actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[i].consigne);
        snprintf(texte_valeur, sizeof(texte_valeur), "%s/%s", valeur, consigne);
        lv_label_set_text(ctx->chauffant_nom[total], nom);
        lv_label_set_text(ctx->chauffant_valeur[total], texte_valeur);
        total++;
    }
    if (e->plateau.presente && total < ECRAN_ACCUEIL_HUB_HEATER_LIGNES) {
        ui_format_temperature(valeur, sizeof(valeur), e->plateau.actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->plateau.consigne);
        snprintf(texte_valeur, sizeof(texte_valeur), "%s/%s", valeur, consigne);
        lv_label_set_text(ctx->chauffant_nom[total], "Bed");
        lv_label_set_text(ctx->chauffant_valeur[total], texte_valeur);
        total++;
    }
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        if (i < total) {
            lv_obj_clear_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

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

    /* --- grisage integral du resume (chauffants + position/vitesse-flux/
     * progression), style RESOLU (spec C3, ecran.h) -- systematique a chaque
     * appel, jamais incremental (meme lecon que tuile_griser()/l'ancien
     * contenu de ce fichier). Le graphe et la grille de menu, eux, NE SONT
     * JAMAIS grises -- le graphe reste une trace historique valable meme sur
     * un etat courant perime, et chaque tuile de menu ouvre un ecran qui
     * grise lui-meme son propre contenu si besoin (meme choix delibere que
     * l'ancien hub : "naviguer... reste sans danger meme avec un etat
     * perime"). -------------------------------------------------------- */
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        lv_obj_set_style_text_color(ctx->chauffant_nom[i], lv_color_hex(couleur), 0);
        lv_obj_set_style_text_color(ctx->chauffant_valeur[i], lv_color_hex(couleur), 0);
    }
    lv_obj_set_style_text_color(ctx->position, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->vitesse_flux, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->progression, lv_color_hex(couleur), 0);

    /* --- graphe : rafraichi UNIQUEMENT quand le store (tache 1) a avance
     * depuis le dernier appel -- l'echantillonneur pousse un point toutes
     * les 5 s (app_main.c/simulateur/main.c), largement plus lent que la
     * cadence d'appel de cette fonction (~200 ms, voir habillage_pomper()) :
     * sans ce garde-fou, ce chart redessinerait ~25x pour rien entre deux
     * points reels. */
    uint32_t generation = klipper_temp_historique_generation();
    if (generation != ctx->derniere_gen) {
        for (uint8_t i = 0; i < KLIPPER_HISTO_SERIES; i++) {
            if (ctx->serie[i] == NULL) {
                /* Chauffant apparu APRES construire() (voir chart_ajouter_serie()
                 * pour le cas reel qui declenche ceci au boot) : rattrapage
                 * unique, cree la serie et la backfille d'un coup plutot que
                 * de la laisser demarrer vide et ne grossir que point par
                 * point a partir de maintenant. */
                if (klipper_temp_historique_serie_presente(i)) {
                    chart_ajouter_serie(ctx, i);
                }
                continue;
            }
            int16_t dernier;
            if (klipper_temp_historique_dernier(i, &dernier)) {
                lv_chart_set_next_value(ctx->chart, ctx->serie[i], dernier);
            }
        }
        lv_chart_refresh(ctx->chart);
        ctx->derniere_gen = generation;
    }
}

const ecran_desc_t ECRAN_ACCUEIL_HUB = {
    .id = "accueil_hub",
    .titre = "Home",
    .taille_contexte = sizeof(ecran_accueil_hub_ctx_t),
    .construire = ecran_accueil_hub_construire,
    .mettre_a_jour = ecran_accueil_hub_mettre_a_jour,
    .detruire = NULL,
};
