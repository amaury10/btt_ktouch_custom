/* Implémentation : voir ecran_accueil_idle.h pour le contrat.
 *
 * Mise en page (800x436, sous la barre d'état construite par habillage.c) :
 * une grille de cellules de température en haut (extrudeurs puis plateau,
 * disposées selon klipper_paliers.h), une ligne de position + outil actif,
 * une zone de contrôles réservée (placeholder tâche 3), une rangée Macros
 * (placeholder tâche 3). Toutes les constantes de position sont dérivées ou
 * vérifiées les unes par rapport aux autres via _Static_assert (voir plus
 * bas), même discipline que ecran_accueil.c : un futur ajustement de l'une
 * d'entre elles qui ferait déborder ou chevaucher une zone devient une
 * erreur de compilation, jamais un pixel qui sort du cadre sans que
 * personne ne le remarque avant une capture. */
#include "ecran_accueil_idle.h"

#include <stdio.h>
#include <string.h>

#include "klipper_paliers.h"
#include "tuile.h" /* ui_format_temperature() */

#define LARGEUR_CONTENU 800
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE        14
#define GRILLE_ECART  6

#define TEMP_ZONE_Y 16

/* Largeur d'une cellule pour 1 colonne (palier MONO, pleine largeur) et pour
 * 2 colonnes (paliers MOYEN/COMPACT) -- voir les _Static_assert plus bas
 * pour la preuve que chacune remplit exactement LARGEUR_CONTENU avec ses
 * marges. Le nombre de colonnes lui-même vient de palier_colonnes()
 * (klipper_paliers.h), jamais recopié en dur ici : seules les dimensions en
 * pixels, que klipper_paliers.h ne fournit pas, sont propres à cet écran. */
#define CELL_LARGEUR_1COL 772
#define CELL_LARGEUR_2COL 383

/* Hauteur d'une cellule par palier -- décroît avec la police (48/28/20,
 * palier_taille_police()) et croît avec le nombre de lignes que le palier
 * peut avoir à afficher (voir LIGNES_PIRE_CAS_* plus bas) : c'est ce qui
 * permet aux trois paliers de se partager la MÊME zone réservée
 * (ZONE_TEMP_HAUTEUR_MAX) sans jamais déborder ni se marcher sur les pieds
 * avec la ligne de position/la zone de contrôles/la rangée Macros
 * ci-dessous, qui démarrent toutes à une position FIXE indépendante du
 * palier courant. */
#define CELL_HAUTEUR_MONO    100
#define CELL_HAUTEUR_MOYEN    70
#define CELL_HAUTEUR_COMPACT  44

/* Lignes du PIRE CAS par palier (nb_extrudeurs maximal de ce palier + 1
 * plateau, voir klipper_paliers.h pour les bornes 0-1/2-4/5-8), utilisées
 * UNIQUEMENT pour réserver la hauteur maximale de la zone de température --
 * jamais pour calculer le nombre de lignes RÉELLEMENT dessinées, qui dépend
 * du nombre de chauffeurs présents à l'exécution (voir
 * ecran_accueil_idle_mettre_a_jour() plus bas). Une régression future des
 * bornes de klipper_paliers.c sans mise à jour ici resterait silencieuse
 * (aucun _Static_assert n'est possible contre une fonction) : compromis
 * assumé plutôt qu'ignoré, documenté ici explicitement. */
#define LIGNES_PIRE_CAS_MONO    2 /* 1 extrudeur + 1 plateau, colonnes=1 */
#define LIGNES_PIRE_CAS_MOYEN   3 /* 4 extrudeurs + 1 plateau, colonnes=2 */
#define LIGNES_PIRE_CAS_COMPACT 5 /* 8 extrudeurs + 1 plateau, colonnes=2 */

/* La plus grande des trois hauteurs de zone (voir les _Static_assert plus
 * bas pour la preuve que COMPACT est bien la plus haute) : la ligne de
 * position, la zone de contrôles et la rangée Macros démarrent à une
 * position FIXE dérivée de CETTE constante, jamais recalculée par palier --
 * pour qu'aucune d'elles ne puisse jamais chevaucher la zone de
 * température, quel que soit le nombre réel de chauffeurs présents. */
#define ZONE_TEMP_HAUTEUR_MAX 244

#define POSITION_Y        270
#define POSITION_HAUTEUR    26
#define OUTIL_ACTIF_LARGEUR 220

#define CONTROLES_Y        306
#define CONTROLES_HAUTEUR   70

#define MACROS_HAUTEUR       50
#define MACROS_Y            386

_Static_assert(2 * MARGE + CELL_LARGEUR_1COL == LARGEUR_CONTENU,
                "la cellule pleine largeur (palier MONO) ne remplit plus exactement 800px");
_Static_assert(2 * MARGE + 2 * CELL_LARGEUR_2COL + GRILLE_ECART == LARGEUR_CONTENU,
                "les deux colonnes (paliers MOYEN/COMPACT) ne remplissent plus exactement 800px");

_Static_assert(LIGNES_PIRE_CAS_MONO * CELL_HAUTEUR_MONO + (LIGNES_PIRE_CAS_MONO - 1) * GRILLE_ECART
                   <= ZONE_TEMP_HAUTEUR_MAX,
                "le palier MONO deborde de la zone de temperature reservee");
_Static_assert(LIGNES_PIRE_CAS_MOYEN * CELL_HAUTEUR_MOYEN + (LIGNES_PIRE_CAS_MOYEN - 1) * GRILLE_ECART
                   <= ZONE_TEMP_HAUTEUR_MAX,
                "le palier MOYEN deborde de la zone de temperature reservee");
_Static_assert(LIGNES_PIRE_CAS_COMPACT * CELL_HAUTEUR_COMPACT + (LIGNES_PIRE_CAS_COMPACT - 1) * GRILLE_ECART
                   == ZONE_TEMP_HAUTEUR_MAX,
                "le palier COMPACT (le plus haut des trois) ne definit plus ZONE_TEMP_HAUTEUR_MAX");

_Static_assert(TEMP_ZONE_Y + ZONE_TEMP_HAUTEUR_MAX + 10 == POSITION_Y,
                "la ligne de position ne demarre plus juste sous la zone de temperature");
_Static_assert(POSITION_Y + POSITION_HAUTEUR + 10 == CONTROLES_Y,
                "la zone de controles chevauche la ligne de position");
_Static_assert(CONTROLES_Y + CONTROLES_HAUTEUR + 10 == MACROS_Y,
                "la rangee Macros chevauche la zone de controles");
_Static_assert(MACROS_Y + MACROS_HAUTEUR == HAUTEUR_CONTENU,
                "la rangee Macros deborde de la hauteur du contenu");
_Static_assert(MARGE + OUTIL_ACTIF_LARGEUR <= LARGEUR_CONTENU - MARGE,
                "le libelle d'outil actif deborderait a gauche de la ligne de position");

#define COULEUR_FOND             0x10161D
#define COULEUR_FOND_CELLULE     0x1B2430
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_BORDURE          0x2A3644
/* Bleu dedie a « outil actif », jamais le vert 0x2ECC71 de
 * habillage_couleur_liaison() (LIAISON_EN_LIGNE) : reutiliser cette
 * couleur-la aurait laisse croire a un lien entre l'etat de connexion et
 * l'outil courant, deux informations sans rapport. */
#define COULEUR_ACTIF 0x3B82F6

/* Géométrie dérivée du palier courant : `colonnes` et `police_valeur`
 * viennent de klipper_paliers.h (jamais recopiés en dur, voir son en-tête) ;
 * `largeur`/`hauteur`/`afficher_consigne` sont propres à cet écran, qui est
 * le seul consommateur de dimensions en pixels concrètes. */
typedef struct {
    uint8_t         colonnes;
    lv_coord_t      largeur;
    lv_coord_t      hauteur;
    const lv_font_t *police_valeur;
    bool            afficher_consigne;
} idle_geometrie_t;

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

static idle_geometrie_t geometrie_pour_palier(palier_outils_t palier)
{
    idle_geometrie_t g;
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
        /* Spec tache 3 : "Au palier COMPACT, la valeur en 20 et pas de
         * consigne inline (le tap->detail viendra en tache 6)." */
        g.largeur = CELL_LARGEUR_2COL;
        g.hauteur = CELL_HAUTEUR_COMPACT;
        g.afficher_consigne = false;
        break;
    }
    return g;
}

/* Crée les quatre objets LVGL d'une cellule et les range dans `c` (déjà
 * alloué par le pool du contexte, voir ecran_accueil_idle.h). Position,
 * taille, police de la valeur et visibilité de la consigne sont posées ici
 * de façon triviale (rien n'est encore connu du palier courant, voir
 * ecran_accueil_idle_construire() plus bas qui ne connaît que `parent`) --
 * c'est mettre_a_jour() qui les recalcule systématiquement à chaque appel. */
static void cellule_creer(ecran_accueil_idle_cellule_t *c, lv_obj_t *parent)
{
    c->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(c->racine);
    lv_obj_set_style_bg_color(c->racine, lv_color_hex(COULEUR_FOND_CELLULE), 0);
    lv_obj_set_style_bg_opa(c->racine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c->racine, 10, 0);
    lv_obj_set_style_border_color(c->racine, lv_color_hex(COULEUR_ACTIF), 0);
    /* Bare lv_obj_create() affiche une barre de defilement et un padding de
     * theme par defaut (meme motif que tuile.c/habillage.c) : neutralises
     * ici, jamais laisses a la charge de l'appelant. */
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

static void ecran_accueil_idle_construire(lv_obj_t *parent, void *contexte)
{
    ecran_accueil_idle_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < ECRAN_ACCUEIL_IDLE_CELLULES_MAX; i++) {
        cellule_creer(&ctx->cellules[i], parent);
        lv_obj_add_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_HIDDEN);
    }

    ctx->position = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->position, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->position, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->position, "");
    lv_obj_set_pos(ctx->position, MARGE, POSITION_Y);

    ctx->outil_actif_nom = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->outil_actif_nom, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->outil_actif_nom, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->outil_actif_nom, "");
    lv_obj_set_size(ctx->outil_actif_nom, OUTIL_ACTIF_LARGEUR, POSITION_HAUTEUR);
    lv_obj_set_style_text_align(ctx->outil_actif_nom, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(ctx->outil_actif_nom, LARGEUR_CONTENU - MARGE - OUTIL_ACTIF_LARGEUR, POSITION_Y);

    ctx->zone_controles = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_controles);
    lv_obj_set_style_bg_color(ctx->zone_controles, lv_color_hex(COULEUR_FOND_CELLULE), 0);
    lv_obj_set_style_bg_opa(ctx->zone_controles, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->zone_controles, 10, 0);
    lv_obj_set_style_border_width(ctx->zone_controles, 1, 0);
    lv_obj_set_style_border_color(ctx->zone_controles, lv_color_hex(COULEUR_BORDURE), 0);
    lv_obj_clear_flag(ctx->zone_controles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->zone_controles, LARGEUR_CONTENU - 2 * MARGE, CONTROLES_HAUTEUR);
    lv_obj_set_pos(ctx->zone_controles, MARGE, CONTROLES_Y);

    /* Tache 3 : placeholder visible seulement -- le pad de jog et le
     * homing arrivent aux taches 4/5, DANS ce meme conteneur reserve. */
    ctx->label_controles = lv_label_create(ctx->zone_controles);
    lv_obj_set_style_text_font(ctx->label_controles, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->label_controles, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->label_controles, "Controls");
    lv_obj_center(ctx->label_controles);

    /* Tache 3 : placeholder statique -- aucun rappel de clic pose ici, le
     * cablage reel (visibilite selon nb_macros, navigation vers
     * ECRAN_MACROS) arrive a la tache 7 (voir le brief : "Macros (tache 7)
     * -- en tache 3, un placeholder"). */
    ctx->bouton_macros = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->bouton_macros);
    lv_obj_set_style_bg_color(ctx->bouton_macros, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_bg_opa(ctx->bouton_macros, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->bouton_macros, 10, 0);
    lv_obj_clear_flag(ctx->bouton_macros, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->bouton_macros, LARGEUR_CONTENU - 2 * MARGE, MACROS_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_macros, MARGE, MACROS_Y);

    ctx->label_macros = lv_label_create(ctx->bouton_macros);
    lv_obj_set_style_text_font(ctx->label_macros, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->label_macros, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(ctx->label_macros, "Macros");
    lv_obj_center(ctx->label_macros);
}

/* Écrit "%.1f" si `reference` est vrai, "--" sinon -- même politique que
 * ui_format_temperature() (tuile.h) pour une mesure non plausible : ne
 * jamais présenter comme mesurée une position qu'aucun homing n'a établie
 * (`axes_references`, voir etat_klipper.h). */
static void formater_axe(char *sortie, size_t taille, float valeur, bool reference)
{
    if (!reference) {
        snprintf(sortie, taille, "--");
        return;
    }
    snprintf(sortie, taille, "%.1f", (double)valeur);
}

static void ecran_accueil_idle_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_accueil_idle_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme : nb_extrudeurs promet 0..8
     * (voir etat_klipper.h) mais rien de ce cote-ci ne DOIT lui faire
     * confiance aveuglement -- un nombre plus grand ecrirait hors du pool
     * de cellules (ECRAN_ACCUEIL_IDLE_CELLULES_MAX, dimensionne exactement
     * sur KLIPPER_EXTRUDEURS_MAX + 1). */
    uint8_t nb_extrudeurs = e->nb_extrudeurs;
    if (nb_extrudeurs > KLIPPER_EXTRUDEURS_MAX) {
        nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
    }

    palier_outils_t palier = palier_outils(nb_extrudeurs);
    idle_geometrie_t geo = geometrie_pour_palier(palier);

    /* --- Contenu : extrudeurs presents puis plateau, dans cet ordre ----- */
    size_t total = 0;
    char valeur[16];
    char consigne[16];
    char nom[8];

    for (uint8_t i = 0; i < nb_extrudeurs; i++) {
        if (!e->extrudeurs[i].presente) {
            /* nb_extrudeurs promet que 0..nb_extrudeurs-1 sont presents
             * (voir etat_klipper.h) ; garde defensive quand meme, meme
             * politique que le reste de ce fichier vis-a-vis d'un etat
             * malforme. */
            continue;
        }
        ecran_accueil_idle_cellule_t *c = &ctx->cellules[total];
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
        /* Outil actif marque par une bordure distincte -- reevalue a
         * CHAQUE appel (systematique, jamais incremental, meme discipline
         * que tuile_griser()) : un outil qui change de "actif" a "inactif"
         * (changeur d'outils) doit perdre sa bordure au prochain appel. */
        lv_obj_set_style_border_width(c->racine, (i == e->outil_actif) ? 3 : 0, 0);
        total++;
    }

    if (e->plateau.presente) {
        ecran_accueil_idle_cellule_t *c = &ctx->cellules[total];
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
        /* Le plateau n'est jamais "l'outil actif" (outil_actif indexe
         * extrudeurs[], jamais le plateau, voir etat_klipper.h). */
        lv_obj_set_style_border_width(c->racine, 0, 0);
        total++;
    }

    /* --- Geometrie + visibilite : appliquees a TOUT le pool, y compris les
     * cellules au-dela de `total` (masquees) -- systematique, jamais
     * incremental : un appel qui ferait passer le palier de COMPACT a MONO
     * (changement de machine hypothetique) doit repositionner/redimensionner
     * les cellules restantes, pas laisser trainer une geometrie perimee. */
    for (size_t i = 0; i < ECRAN_ACCUEIL_IDLE_CELLULES_MAX; i++) {
        ecran_accueil_idle_cellule_t *c = &ctx->cellules[i];
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

    /* --- Ligne d'etat machine : position puis outil actif nomme --------- */
    char pos_x[8];
    char pos_y[8];
    char pos_z[8];
    formater_axe(pos_x, sizeof(pos_x), e->position[0], (e->axes_references & 0x1u) != 0);
    formater_axe(pos_y, sizeof(pos_y), e->position[1], (e->axes_references & 0x2u) != 0);
    formater_axe(pos_z, sizeof(pos_z), e->position[2], (e->axes_references & 0x4u) != 0);
    char texte_position[48];
    snprintf(texte_position, sizeof(texte_position), "X:%s Y:%s Z:%s", pos_x, pos_y, pos_z);
    lv_label_set_text(ctx->position, texte_position);

    char texte_outil[24];
    if (nb_extrudeurs > 0) {
        snprintf(texte_outil, sizeof(texte_outil), "Active: T%u", (unsigned)e->outil_actif);
    } else {
        snprintf(texte_outil, sizeof(texte_outil), "Active: --");
    }
    lv_label_set_text(ctx->outil_actif_nom, texte_outil);

    /* --- Grisage integral, style RESOLU (spec tache 3) : cellules,
     * position, controles -- systematique a chaque appel, jamais
     * incremental (meme lecon que tuile_griser()/ecran_accueil.c : un appel
     * avec donnees_perimees=false doit rendre exactement les couleurs
     * normales, y compris apres plusieurs allers-retours). */
    uint32_t couleur_texte = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    uint32_t couleur_valeur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    for (size_t i = 0; i < total; i++) {
        ecran_accueil_idle_cellule_t *c = &ctx->cellules[i];
        lv_obj_set_style_text_color(c->nom, lv_color_hex(couleur_texte), 0);
        lv_obj_set_style_text_color(c->valeur, lv_color_hex(couleur_valeur), 0);
        lv_obj_set_style_text_color(c->consigne, lv_color_hex(couleur_texte), 0);
    }
    lv_obj_set_style_text_color(ctx->position, lv_color_hex(couleur_texte), 0);
    lv_obj_set_style_text_color(ctx->outil_actif_nom, lv_color_hex(couleur_texte), 0);
    lv_obj_set_style_text_color(ctx->label_controles, lv_color_hex(couleur_texte), 0);
    /* Rangee Macros : non listee explicitement par le brief ("cellules,
     * position, controles"), grisee quand meme pour rester visuellement
     * coherente avec le reste de l'ecran perime -- ce placeholder ne porte
     * de toute facon aucun etat LV_STATE_DISABLED avant la tache 7. */
    lv_obj_set_style_text_color(ctx->label_macros, lv_color_hex(couleur_texte), 0);
}

const ecran_desc_t ECRAN_ACCUEIL_IDLE = {
    .id = "accueil_idle",
    .titre = "Home",
    .taille_contexte = sizeof(ecran_accueil_idle_ctx_t),
    .construire = ecran_accueil_idle_construire,
    .mettre_a_jour = ecran_accueil_idle_mettre_a_jour,
    .detruire = NULL,
};
