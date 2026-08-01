/* Implémentation : voir ecran_accueil_hub.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : la zone de
 * température en haut (extrudeurs présents puis plateau, géométrie par
 * palier -- klipper_paliers.h, IDENTIQUE à l'ancien ecran_accueil_idle.c
 * (supprimé en tâche 7), voir geometrie_pour_palier()/cellule_creer()
 * ci-dessous, copiés depuis ce fichier -- tâche 5, "Réutilisation (DRY)" du
 * brief), puis une grille de 6 cases de menu (3 colonnes x 2 lignes) en
 * dessous.
 *
 * ÉCART délibéré par rapport à l'ancien ecran_accueil_idle.c : les cellules de
 * température ne réagissent PAS comme avant -- au palier COMPACT, l'ancien
 * idle ouvrait le clavier numérique directement depuis la cellule tapée ;
 * ici (sous-projet 2, tâche 2) un tap sur une tuile OU sur la case de menu
 * « Températures » empile ECRAN_TEMPERATURES (ecran_temperatures.h), qui
 * porte lui-même le réglage détaillé (clavier + préréglages) -- aucun
 * ciblage par chauffe fait ici, le panneau gère ça seul. Le hub n'a NI jog
 * NI homing NI préréglages : uniquement les tuiles + la grille de menu
 * (task-5-brief.md).
 *
 * Libellés de menu SANS accent ("Deplacer", pas "Déplacer") bien que le
 * brief les nomme avec accents en prose : aucun texte affiché à l'écran
 * dans ce dépôt n'utilise de caractère accentué (voir "Nozzle target"/"Bed
 * target"/rail.c "Accueil"/"Home"/"Macros"/"STOP") -- les polices Montserrat
 * embarquées (sdkconfig.defaults, CONFIG_LV_FONT_MONTSERRAT_*) ne sont
 * jamais garanties couvrir le Latin-1 Supplement, un glyphe manquant se
 * rendrait en tofu silencieux. Choix délibéré, pas un oubli. */
#include "ecran_accueil_hub.h"

#include <stdio.h>

#include "ecran_deplacer.h" /* ECRAN_DEPLACER */
#include "ecran_extruder.h" /* ECRAN_EXTRUDER */
#include "ecran_temperatures.h" /* ECRAN_TEMPERATURES */
#include "klipper_paliers.h"
#include "navigation.h" /* navigation_empiler() */
#include "tuile.h" /* ui_format_temperature() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE        14
#define GRILLE_ECART  6

#define ZONE_ECART 14 /* écart vertical entre la zone de température et la grille de menu */

#define TEMP_ZONE_Y 6

/* --- Géométrie de la zone de température : copie EXACTE des constantes de
 * l'ancien ecran_accueil_idle.c (supprimé en tâche 7, même valeurs, même
 * raisonnement pour le détail du budget par palier). Dupliquées ici
 * plutôt que partagées, voir le commentaire de tête de ce fichier et celui
 * de ecran_accueil_hub.h ("Réutilisation (DRY)"). ------------------------ */
#define CELL_LARGEUR_1COL 714
#define CELL_LARGEUR_2COL 354

#define CELL_HAUTEUR_MONO    100
#define CELL_HAUTEUR_MOYEN    70
#define CELL_HAUTEUR_COMPACT  44

#define LIGNES_PIRE_CAS_MONO    2 /* 1 extrudeur + 1 plateau, colonnes=1 */
#define LIGNES_PIRE_CAS_MOYEN   3 /* 4 extrudeurs + 1 plateau, colonnes=2 */
#define LIGNES_PIRE_CAS_COMPACT 5 /* 8 extrudeurs + 1 plateau, colonnes=2 */

#define ZONE_TEMP_HAUTEUR_MAX 244

/* --- Grille de menu : 3 colonnes x 2 lignes, sous la zone de température --
 * géométrie propre à ce hub (l'idle n'avait pas cette zone). ------------- */
#define MENU_COLONNES 3
#define MENU_LIGNES   2
/* 9 (et non 8 comme au temps du contenu 800px) : avec LARGEUR_CONTENU=742, la
 * largeur utile de la grille vaut 714 ; il faut que (714 - 2*ecart) soit
 * divisible par 3 pour que les 3 colonnes la remplissent EXACTEMENT
 * (_Static_assert plus bas). 714 % 3 == 0 impose ecart % 3 == 0 ; 9 est
 * l'ajustement minimal depuis 8 (cases de 232px, largement >= 44px). */
#define MENU_ECART_COLONNE 9
#define MENU_ECART_LIGNE   8

#define MENU_ZONE_Y (TEMP_ZONE_Y + ZONE_TEMP_HAUTEUR_MAX + ZONE_ECART)
#define MENU_CELL_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - (MENU_COLONNES - 1) * MENU_ECART_COLONNE) / MENU_COLONNES)
#define MENU_CELL_HAUTEUR 52 /* >= 44px cible tactile minimale, largement */
#define MENU_ZONE_HAUTEUR (MENU_LIGNES * MENU_CELL_HAUTEUR + (MENU_LIGNES - 1) * MENU_ECART_LIGNE)

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_macros.c (voir son commentaire complet) : bande
 * couverte par le bandeau de notification de habillage.c, en coordonnées
 * ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(2 * MARGE + CELL_LARGEUR_1COL == LARGEUR_CONTENU,
                "la cellule pleine largeur (palier MONO) ne remplit plus exactement 742px");
_Static_assert(2 * MARGE + 2 * CELL_LARGEUR_2COL + GRILLE_ECART == LARGEUR_CONTENU,
                "les deux colonnes (paliers MOYEN/COMPACT) ne remplissent plus exactement 742px");

_Static_assert(LIGNES_PIRE_CAS_MONO * CELL_HAUTEUR_MONO + (LIGNES_PIRE_CAS_MONO - 1) * GRILLE_ECART
                   <= ZONE_TEMP_HAUTEUR_MAX,
                "le palier MONO deborde de la zone de temperature reservee");
_Static_assert(LIGNES_PIRE_CAS_MOYEN * CELL_HAUTEUR_MOYEN + (LIGNES_PIRE_CAS_MOYEN - 1) * GRILLE_ECART
                   <= ZONE_TEMP_HAUTEUR_MAX,
                "le palier MOYEN deborde de la zone de temperature reservee");
_Static_assert(LIGNES_PIRE_CAS_COMPACT * CELL_HAUTEUR_COMPACT + (LIGNES_PIRE_CAS_COMPACT - 1) * GRILLE_ECART
                   == ZONE_TEMP_HAUTEUR_MAX,
                "le palier COMPACT (le plus haut des trois) ne definit plus ZONE_TEMP_HAUTEUR_MAX");

_Static_assert(MENU_COLONNES * MENU_CELL_LARGEUR + (MENU_COLONNES - 1) * MENU_ECART_COLONNE ==
                    LARGEUR_CONTENU - 2 * MARGE,
                "la grille de menu ne remplit plus exactement la largeur du contenu");
_Static_assert(MENU_ZONE_Y + MENU_ZONE_HAUTEUR <= HAUTEUR_CONTENU,
                "la grille de menu deborde de la hauteur du contenu");
/* Même garde-fou que CONTROLES_Y dans l'ancien ecran_accueil_idle.c
 * (supprimé en tâche 7, "PLAFOND REEL de CONTROLES_HAUTEUR") : le bas de la
 * grille de menu, en coordonnées ABSOLUES d'écran, doit rester au-dessus du
 * bandeau de notification -- sans quoi une notification recouvrirait ET
 * bloquerait le tap sur les cases de menu (le bandeau est un lv_obj_t normal,
 * cliquable). Contrairement à MACROS_Y dans l'ancien idle (qui ne pouvait
 * structurellement pas l'éviter, dernière zone de l'écran), ce hub A la
 * place de l'éviter -- fait ici, pas juste accepté. */
_Static_assert(BARRE_HAUTEUR_ECRAN + MENU_ZONE_Y + MENU_ZONE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la grille de menu chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_FOND_CELLULE     0x1B2430
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
/* Bleu dedie a « outil actif », copie de l'ancien ecran_accueil_idle.c --
 * jamais le vert de habillage_couleur_liaison(). */
#define COULEUR_ACTIF 0x3B82F6

/* --- Zone de température : copie de idle_geometrie_t/geometrie_pour_palier()/
 * police_pour_taille()/cellule_creer() de l'ancien ecran_accueil_idle.c
 * (supprimé en tâche 7), voir le commentaire de tête de ce fichier pour
 * pourquoi une copie plutôt qu'un partage. ---------------------------------*/
typedef struct {
    uint8_t          colonnes;
    lv_coord_t       largeur;
    lv_coord_t       hauteur;
    const lv_font_t *police_valeur;
    bool             afficher_consigne;
} hub_geometrie_t;

static const lv_font_t *police_pour_taille(uint8_t taille)
{
    switch (taille) {
    case 48:
        return &lv_font_montserrat_48;
    case 28:
        return &lv_font_montserrat_28;
    case 20:
    default:
        return &lv_font_montserrat_20;
    }
}

static hub_geometrie_t geometrie_pour_palier(palier_outils_t palier)
{
    hub_geometrie_t g;
    g.colonnes = palier_colonnes(palier);
    g.police_valeur = police_pour_taille(palier_taille_police(palier));
    switch (palier) {
    case PALIER_MONO:
        g.largeur = CELL_LARGEUR_1COL;
        g.hauteur = CELL_HAUTEUR_MONO;
        g.afficher_consigne = true;
        break;
    case PALIER_MOYEN:
        g.largeur = CELL_LARGEUR_2COL;
        g.hauteur = CELL_HAUTEUR_MOYEN;
        g.afficher_consigne = true;
        break;
    case PALIER_COMPACT:
    default:
        /* Au palier COMPACT, pas de consigne inline (spec §6, identique a
         * l'idle) -- le reglage detaille vit desormais derriere la case de
         * menu "Temperatures" (sous-projet futur), pas un clavier ouvert
         * depuis la cellule (voir le commentaire de tete de ce fichier). */
        g.largeur = CELL_LARGEUR_2COL;
        g.hauteur = CELL_HAUTEUR_COMPACT;
        g.afficher_consigne = false;
        break;
    }
    return g;
}

static void cellule_creer(ecran_accueil_hub_cellule_t *c, lv_obj_t *parent)
{
    c->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(c->racine);
    lv_obj_set_style_bg_color(c->racine, lv_color_hex(COULEUR_FOND_CELLULE), 0);
    lv_obj_set_style_bg_opa(c->racine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c->racine, 10, 0);
    lv_obj_set_style_border_color(c->racine, lv_color_hex(COULEUR_ACTIF), 0);
    lv_obj_clear_flag(c->racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c->racine, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c->racine, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c->racine, 2, 0);

    c->nom = lv_label_create(c->racine);
    lv_obj_set_style_text_font(c->nom, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c->nom, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(c->nom, "");

    c->valeur = lv_label_create(c->racine);
    lv_obj_set_style_text_color(c->valeur, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(c->valeur, "");

    c->consigne = lv_label_create(c->racine);
    lv_obj_set_style_text_font(c->consigne, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c->consigne, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(c->consigne, "");
}

/* --- Grille de menu ------------------------------------------------------
 *
 * DEPLACER et TEMPERATURES ont chacune un rappel de clic reel
 * (navigation_empiler()) ; les quatre autres n'ont AUCUN rappel attache (voir
 * le commentaire de tete de ecran_accueil_hub.h) -- un no-op scope, pas un
 * ecran bricole. */
static void menu_deplacer_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_DEPLACER);
    /* Echec (ESP_ERR_NO_MEM, pile deja pleine) delibrement ignore ici : ce
     * hub n'a rien a journaliser de plus utile qu'un simple "rien ne se
     * passe" -- la pile est bornee a NAVIGATION_PROFONDEUR_MAX (navigation.h)
     * et un hub-vers-deplacer est toujours une profondeur 1->2, jamais pres
     * de cette borne en usage normal. */
}

/* Meme idiome que menu_deplacer_cb() ci-dessus, meme echec ignore pour la
 * meme raison -- rappel PARTAGE entre la case de menu "Temperatures" et
 * chaque tuile de temperature (voir cellule_creer()/la boucle de creation
 * dans ecran_accueil_hub_construire()) : aucun ciblage par chauffe requis,
 * les deux points d'entree ouvrent simplement le meme panneau. */
static void ouvrir_temperatures_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_TEMPERATURES);
}

/* Meme idiome que menu_deplacer_cb()/ouvrir_temperatures_cb() ci-dessus,
 * meme echec ignore pour la meme raison -- sous-projet 3, tache 2 : la case
 * de menu "Extruder" navigue desormais reellement vers ECRAN_EXTRUDER. */
static void ouvrir_extruder_cb(lv_event_t *e)
{
    (void)e;
    navigation_empiler(&ECRAN_EXTRUDER);
}

static const struct {
    const char *titre;
    const char *sous_titre; /* "" pour les liens reels (Deplacer, Temperatures, Extruder), "A venir" pour les cases encore no-op */
} MENU_DEFS[ECRAN_ACCUEIL_HUB_MENU_NB] = {
    [ECRAN_ACCUEIL_HUB_MENU_DEPLACER]     = { "Deplacer",     "" },
    [ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES] = { "Temperatures", "" },
    [ECRAN_ACCUEIL_HUB_MENU_EXTRUDER]     = { "Extruder",     "" },
    [ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS] = { "Ventilateurs", "A venir" },
    [ECRAN_ACCUEIL_HUB_MENU_IMPRIMER]     = { "Imprimer",     "A venir" },
    [ECRAN_ACCUEIL_HUB_MENU_REGLAGES]     = { "Reglages",     "A venir" },
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

    /* --- tuiles de temperature : pool au pire cas, masquees tant que
     * mettre_a_jour() n'a pas tourne -- meme politique que cellule_creer()
     * dans l'ancien ecran_accueil_idle.c. Chaque tuile est rendue cliquable
     * (LV_OBJ_FLAG_CLICKABLE : `racine` est un lv_obj_t nu via
     * lv_obj_create(), pas un lv_button_create(), donc le flag ne coule pas
     * de source comme pour les boutons de la grille de menu plus bas) et
     * partage ouvrir_temperatures_cb() -- voir son commentaire. -----------*/
    for (size_t i = 0; i < ECRAN_ACCUEIL_HUB_CELLULES_MAX; i++) {
        cellule_creer(&ctx->cellules[i], parent);
        lv_obj_add_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ctx->cellules[i].racine, ouvrir_temperatures_cb, LV_EVENT_CLICKED, NULL);
    }

    /* --- grille de menu, 6 cases a taille fixe (voir MENU_* en tete de
     * fichier) --------------------------------------------------------- */
    ctx->zone_menu = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_menu);
    lv_obj_clear_flag(ctx->zone_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_menu, LARGEUR_CONTENU - 2 * MARGE, MENU_ZONE_HAUTEUR);
    lv_obj_set_pos(ctx->zone_menu, MARGE, MENU_ZONE_Y);

    for (uint8_t i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        uint8_t ligne = i / MENU_COLONNES;
        uint8_t colonne = i % MENU_COLONNES;
        lv_coord_t x = (lv_coord_t)(colonne * (MENU_CELL_LARGEUR + MENU_ECART_COLONNE));
        lv_coord_t y = (lv_coord_t)(ligne * (MENU_CELL_HAUTEUR + MENU_ECART_LIGNE));

        lv_obj_t *bouton = lv_button_create(ctx->zone_menu);
        /* lv_obj_remove_style_all() : meme raison que home_bouton_creer()
         * dans l'ancien ecran_accueil_idle.c -- theme par defaut +
         * transition animee otes, pour ne pas alourdir style_trans_ll cote
         * host-test. */
        lv_obj_remove_style_all(bouton);
        lv_obj_set_size(bouton, MENU_CELL_LARGEUR, MENU_CELL_HAUTEUR);
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
        if (MENU_DEFS[i].sous_titre[0] != '\0') {
            lv_label_set_text_fmt(label, "%s\n%s", MENU_DEFS[i].titre, MENU_DEFS[i].sous_titre);
        } else {
            lv_label_set_text(label, MENU_DEFS[i].titre);
        }
        lv_obj_center(label);

        ctx->menu_boutons[i] = bouton;
    }

    /* DEPLACER et TEMPERATURES naviguent reellement (voir le commentaire de
     * tete de ce bloc) -- ordre de creation FIXE (boucle ci-dessus), donc
     * ECRAN_ACCUEIL_HUB_MENU_DEPLACER == 0 / ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES
     * == 1 sont bien les deux premiers boutons crees, meme convention que
     * rail_t.boutons[i]. */
    lv_obj_add_event_cb(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_DEPLACER], menu_deplacer_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES], ouvrir_temperatures_cb,
                         LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_EXTRUDER], ouvrir_extruder_cb, LV_EVENT_CLICKED,
                         NULL);
}

static void ecran_accueil_hub_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_accueil_hub_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme, meme garde que l'ancienne
     * ecran_accueil_idle_mettre_a_jour() (supprimee en tache 7). */
    uint8_t nb_extrudeurs = e->nb_extrudeurs;
    if (nb_extrudeurs > KLIPPER_EXTRUDEURS_MAX) {
        nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
    }

    palier_outils_t palier = palier_outils(nb_extrudeurs);
    hub_geometrie_t geo = geometrie_pour_palier(palier);

    /* --- Contenu : extrudeurs presents puis plateau, dans cet ordre -----
     * copie du corps de l'ancienne ecran_accueil_idle_mettre_a_jour()
     * (supprimee en tache 7). ---------------------------------------------*/
    size_t total = 0;
    char valeur[16];
    char consigne[16];
    char nom[8];

    for (uint8_t i = 0; i < nb_extrudeurs; i++) {
        if (!e->extrudeurs[i].presente) {
            continue;
        }
        ecran_accueil_hub_cellule_t *c = &ctx->cellules[total];
        snprintf(nom, sizeof(nom), "T%u", (unsigned)i);
        lv_label_set_text(c->nom, nom);
        ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[i].actuelle);
        lv_label_set_text(c->valeur, valeur);
        if (geo.afficher_consigne) {
            ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[i].consigne);
            lv_label_set_text(c->consigne, consigne);
            lv_obj_clear_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_border_width(c->racine, (i == e->outil_actif) ? 3 : 0, 0);
        total++;
    }

    if (e->plateau.presente) {
        ecran_accueil_hub_cellule_t *c = &ctx->cellules[total];
        lv_label_set_text(c->nom, "Bed");
        ui_format_temperature(valeur, sizeof(valeur), e->plateau.actuelle);
        lv_label_set_text(c->valeur, valeur);
        if (geo.afficher_consigne) {
            ui_format_temperature(consigne, sizeof(consigne), e->plateau.consigne);
            lv_label_set_text(c->consigne, consigne);
            lv_obj_clear_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_border_width(c->racine, 0, 0);
        total++;
    }

    /* --- Geometrie + visibilite : appliquees a TOUT le pool, systematique,
     * jamais incremental -- meme discipline que l'ancien ecran_accueil_idle.c. ---- */
    for (size_t i = 0; i < ECRAN_ACCUEIL_HUB_CELLULES_MAX; i++) {
        ecran_accueil_hub_cellule_t *c = &ctx->cellules[i];
        if (i >= total) {
            lv_obj_add_flag(c->racine, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(c->racine, LV_OBJ_FLAG_HIDDEN);
        size_t ligne = i / geo.colonnes;
        size_t colonne = i % geo.colonnes;
        lv_coord_t x = MARGE + (lv_coord_t)(colonne * (geo.largeur + GRILLE_ECART));
        lv_coord_t y = TEMP_ZONE_Y + (lv_coord_t)(ligne * (geo.hauteur + GRILLE_ECART));
        lv_obj_set_size(c->racine, geo.largeur, geo.hauteur);
        lv_obj_set_pos(c->racine, x, y);
        lv_obj_set_style_text_font(c->valeur, geo.police_valeur, 0);
    }

    /* --- Grisage integral des tuiles, style RESOLU (spec C3, ecran.h) --
     * systematique a chaque appel, jamais incremental (meme lecon que
     * tuile_griser()/l'ancien ecran_accueil_idle.c). La grille de menu, elle,
     * N'EST JAMAIS grisee -- meme choix delibere que bouton_macros dans
     * l'ancien idle ("naviguer... reste sans danger meme avec un etat
     * perime") : DEPLACER reste toujours accessible, et les cinq cases
     * no-op ne font de toute facon rien de dangereux. ---------------------*/
    uint32_t couleur_texte = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    uint32_t couleur_valeur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    for (size_t i = 0; i < total; i++) {
        ecran_accueil_hub_cellule_t *c = &ctx->cellules[i];
        lv_obj_set_style_text_color(c->nom, lv_color_hex(couleur_texte), 0);
        lv_obj_set_style_text_color(c->valeur, lv_color_hex(couleur_valeur), 0);
        lv_obj_set_style_text_color(c->consigne, lv_color_hex(couleur_texte), 0);
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
