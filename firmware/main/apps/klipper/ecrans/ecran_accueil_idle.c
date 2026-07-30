/* Implémentation : voir ecran_accueil_idle.h pour le contrat.
 *
 * Mise en page (800x436, sous la barre d'état construite par habillage.c) :
 * une grille de cellules de température en haut (extrudeurs puis plateau,
 * disposées selon klipper_paliers.h), une ligne de position + outil actif,
 * une zone de contrôles (pad de jog XY/Z + sélecteur de pas, tâche 4 -- le
 * homing de la tâche 5 s'y ajoutera), une rangée Macros (placeholder tâche
 * 3, câblage tâche 7). Toutes les constantes de position sont dérivées ou
 * vérifiées les unes par rapport aux autres via _Static_assert (voir plus
 * bas), même discipline que ecran_accueil.c : un futur ajustement de l'une
 * d'entre elles qui ferait déborder ou chevaucher une zone devient une
 * erreur de compilation, jamais un pixel qui sort du cadre sans que
 * personne ne le remarque avant une capture.
 *
 * ÉCART tâche 4 par rapport à la mise en page figée par la tâche 3 :
 * CONTROLES_HAUTEUR grandit (70 -> 92 px, un vrai pad de jog à trois lignes
 * -- "X-/X+/Y-/Y+ autour d'un centre, colonne Z à côté" -- ne tient pas dans
 * 70 px avec des boutons tactiles) et l'espace nécessaire est repris SANS
 * TOUCHER la zone de température (ZONE_TEMP_HAUTEUR_MAX=244 reste une
 * égalité EXACTE avec le pire cas du palier COMPACT, voir plus bas -- la
 * réduire tronquerait le texte des cellules à 8 têtes) : les trois écarts
 * entre zones passent de 10 à 4 px (ZONE_ECART), TEMP_ZONE_Y de 16 à 6, et
 * MACROS_HAUTEUR de 50 à 56 (placeholder de la tâche 3, câblage réel tâche
 * 7 -- rien ne dépend encore de sa hauteur exacte). Bilan exact (voir les
 * _Static_assert en bas de ce bloc) : toujours 436 px pile.
 *
 * PLAFOND REEL de CONTROLES_HAUTEUR (constaté à la capture, pas seulement
 * calculé) : le bandeau de notification de habillage.c (60 px, superposé en
 * ABSOLU depuis le bas de l'ÉCRAN ENTIER, opaque, cliquable par défaut comme
 * tout lv_obj_t -- voir BANDEAU_HAUTEUR_ECRAN/BANDEAU_Y_ECRAN plus bas, même
 * convention que ecran_macros.c) couvre les 60 derniers px ABSOLUS de
 * l'écran quel que soit le contenu en dessous. La toute première tentative
 * de cette tâche (CONTROLES_HAUTEUR=98, ZONE_ECART=6, TEMP_ZONE_Y=10)
 * poussait le bas de la zone de contrôles 14 px À L'INTÉRIEUR de cette
 * bande -- invisible sur idle-jog.png tant qu'aucun bandeau n'est affiché,
 * mais "host connected" (déclenché par --cycles>0, capture MANDATORY de
 * cette tâche) l'aurait recouvert, masquant ET bloquant le tap (le bandeau
 * est un lv_obj_t normal, cliquable) sur Y-/Z-/le sélecteur. Découvert en
 * ouvrant la capture, corrigé ICI (92 px, pas 98) avec le même
 * _Static_assert que PAGINATION_Y dans ecran_macros.c pour que cette classe
 * de bug ne puisse plus jamais repasser inaperçue. */
#include "ecran_accueil_idle.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "klipper_gcode.h"
#include "klipper_paliers.h"
#include "source_etat.h"
#include "tuile.h" /* ui_format_temperature() */

#define LARGEUR_CONTENU 800
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE        14
#define GRILLE_ECART  6

/* Écart vertical entre les quatre zones empilées (température, position,
 * contrôles, macros) -- nommé (tâche 4) plutôt que répété en littéral, pour
 * que le bilan de hauteur du commentaire de tête ci-dessus reste vérifiable
 * d'un coup d'œil. Valait 10 avant la tâche 4 (voir son commentaire). */
#define ZONE_ECART 4

#define TEMP_ZONE_Y 6

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
 * palier courant. INCHANGÉ par la tâche 4 (voir le commentaire de tête) :
 * CELL_HAUTEUR_COMPACT=44 est déjà le minimum exact pour deux lignes de
 * texte (nom 14pt + valeur 20pt) dans une cellule, sans marge à céder. */
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

#define POSITION_Y        254
#define POSITION_HAUTEUR    26
#define OUTIL_ACTIF_LARGEUR 220

#define CONTROLES_Y        284
#define CONTROLES_HAUTEUR   92

#define MACROS_HAUTEUR       56
#define MACROS_Y            380

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_macros.c (voir son commentaire complet) : bande
 * couverte par le bandeau de notification de habillage.c, en coordonnées
 * ABSOLUES d'écran -- DOIVENT rester synchronisées avec lui (aucune des deux
 * définitions n'incluant l'autre). */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

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

_Static_assert(TEMP_ZONE_Y + ZONE_TEMP_HAUTEUR_MAX + ZONE_ECART == POSITION_Y,
                "la ligne de position ne demarre plus juste sous la zone de temperature");
_Static_assert(POSITION_Y + POSITION_HAUTEUR + ZONE_ECART == CONTROLES_Y,
                "la zone de controles chevauche la ligne de position");
_Static_assert(CONTROLES_Y + CONTROLES_HAUTEUR + ZONE_ECART == MACROS_Y,
                "la rangee Macros chevauche la zone de controles");
_Static_assert(MACROS_Y + MACROS_HAUTEUR == HAUTEUR_CONTENU,
                "la rangee Macros deborde de la hauteur du contenu");
_Static_assert(MARGE + OUTIL_ACTIF_LARGEUR <= LARGEUR_CONTENU - MARGE,
                "le libelle d'outil actif deborderait a gauche de la ligne de position");
/* Le garde-fou qui manquait a la toute premiere version de cette tache (bug
 * reel constate a la capture, voir le commentaire de tete de ce fichier) :
 * le bas de la zone de CONTROLES (en coordonnees ABSOLUES d'ecran) doit
 * rester strictement au-dessus du haut du bandeau de notification -- sans
 * quoi "host connected" (ou n'importe quelle notification) recouvre ET
 * bloque le tap sur les boutons de jog/le selecteur de pas, exactement comme
 * idle-jog.png l'a montre avant ce fix. Meme garde que PAGINATION_Y dans
 * ecran_macros.c ; contrairement a lui, PAS applique a MACROS_Y ici -- la
 * rangee Macros est de toute facon la derniere du contenu (MACROS_Y +
 * MACROS_HAUTEUR == HAUTEUR_CONTENU, assert ci-dessus) et ne PEUT
 * structurellement pas eviter les 60 derniers px absolus de l'ecran, meme
 * probleme deja accepte tel quel par le placeholder de la tache 3. */
_Static_assert(BARRE_HAUTEUR_ECRAN + CONTROLES_Y + CONTROLES_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la zone de controles chevauche la bande du bandeau de notification de l'habillage");

/* --- Mise en page interne de la zone de controles (tache 4) -------------
 *
 * Pad XY (trois lignes, "autour d'un centre" -- Y+ en haut, X-/(centre
 * vide)/X+ au milieu, Y- en bas) + colonne Z a cote (Z+/Z-, meme hauteur
 * totale que le pad) + selecteur de pas a droite (occupe TOUT l'espace
 * restant, voir SELECTEUR_LARGEUR ci-dessous) -- les trois groupes tiennent
 * sur UNE seule rangee horizontale : "en dessous" du brief (SDD
 * task-4-brief.md) n'est pas atteignable en restant dans CONTROLES_HAUTEUR
 * sans re-tailler la zone de temperature (voir le commentaire de tete de ce
 * fichier) -- cote a cote reste "un pad de jog + un selecteur de pas dans
 * la zone de controles" au sens du brief, juste reparti sur la largeur
 * plutot que la hauteur. */
#define CONTROLES_PAD_INTERNE 6 /* marge interne de zone_controles, tous cotes */

#define JOG_BOUTON_LARGEUR 44
#define JOG_BOUTON_HAUTEUR 24
#define JOG_ECART           4 /* entre les 3 lignes du pad XY et entre les 2 boutons Z */

#define JOG_PAD_LARGEUR (3 * JOG_BOUTON_LARGEUR + 2 * JOG_ECART)
#define JOG_PAD_HAUTEUR (3 * JOG_BOUTON_HAUTEUR + 2 * JOG_ECART)

#define JOG_Z_LARGEUR JOG_BOUTON_LARGEUR
#define JOG_Z_HAUTEUR ((JOG_PAD_HAUTEUR - JOG_ECART) / 2)
#define JOG_Z_ECART_COLONNE 10 /* entre le pad XY et la colonne Z */

#define SELECTEUR_ECART_PAD 16 /* entre la colonne Z et le selecteur de pas */
#define SELECTEUR_LARGEUR (LARGEUR_CONTENU - 2 * MARGE - 2 * CONTROLES_PAD_INTERNE - JOG_PAD_LARGEUR - \
                            JOG_Z_ECART_COLONNE - JOG_Z_LARGEUR - SELECTEUR_ECART_PAD)
#define SELECTEUR_HAUTEUR JOG_PAD_HAUTEUR

_Static_assert(2 * CONTROLES_PAD_INTERNE + JOG_PAD_HAUTEUR == CONTROLES_HAUTEUR,
                "le pad XY ne remplit plus exactement la hauteur de la zone de controles");
_Static_assert(2 * JOG_Z_HAUTEUR + JOG_ECART == JOG_PAD_HAUTEUR,
                "la colonne Z (2 boutons) ne fait plus exactement la hauteur du pad XY");
_Static_assert(CONTROLES_PAD_INTERNE + JOG_PAD_LARGEUR + JOG_Z_ECART_COLONNE + JOG_Z_LARGEUR +
                    SELECTEUR_ECART_PAD + SELECTEUR_LARGEUR + CONTROLES_PAD_INTERNE ==
                    LARGEUR_CONTENU - 2 * MARGE,
                "pad + colonne Z + selecteur ne remplissent plus exactement la largeur de la zone de controles");

#define JOG_COL0_X CONTROLES_PAD_INTERNE
#define JOG_COL1_X (JOG_COL0_X + JOG_BOUTON_LARGEUR + JOG_ECART)
#define JOG_COL2_X (JOG_COL1_X + JOG_BOUTON_LARGEUR + JOG_ECART)

#define JOG_ROW0_Y CONTROLES_PAD_INTERNE
#define JOG_ROW1_Y (JOG_ROW0_Y + JOG_BOUTON_HAUTEUR + JOG_ECART)
#define JOG_ROW2_Y (JOG_ROW1_Y + JOG_BOUTON_HAUTEUR + JOG_ECART)

#define JOG_Z_X (CONTROLES_PAD_INTERNE + JOG_PAD_LARGEUR + JOG_Z_ECART_COLONNE)
#define JOG_Z_Y0 CONTROLES_PAD_INTERNE
#define JOG_Z_Y1 (JOG_Z_Y0 + JOG_Z_HAUTEUR + JOG_ECART)

#define SELECTEUR_X (JOG_Z_X + JOG_Z_LARGEUR + SELECTEUR_ECART_PAD)
#define SELECTEUR_Y CONTROLES_PAD_INTERNE

/* Vitesses de jog (brief tache 4, jalon 3b) : 3000 mm/min pour XY, 600 pour
 * Z -- Z est mecaniquement plus lent sur la quasi-totalite des imprimantes
 * FDM (vis/ecrou, pas courroie), memes valeurs que la convention Klipper
 * usuelle. */
#define JOG_VITESSE_XY_MM_MIN 3000u
#define JOG_VITESSE_Z_MM_MIN   600u

/* Tampon suffisant pour {"script":"<gcode>"} : KLIPPER_GCODE_MAX (160,
 * klipper_gcode.h) plus la marge du wrapper JSON (guillemets, cle "script",
 * accolades) et de l'echappement des `\n` reels que klipper_gcode_jog()
 * produit (chacun devient 2 octets, "\n", dans le JSON -- au plus 3 dans un
 * script de jog, largement couvert par cette marge). */
#define ECRAN_ACCUEIL_IDLE_GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

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

/* Meme raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_accueil.c/
 * ecran_macros.c (voir leur commentaire complet la-bas, revue tache 6, fix
 * round 1, jalon 2b) : un style local DEDIE a LV_STATE_DISABLED, jamais un
 * simple lv_obj_add_state() seul, est ce qui rend le grisage des boutons de
 * jog honnete. */
#define BOUTON_DESACTIVE_MELANGE 90

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

/* Meme paire de styles locaux (DEFAULT + DISABLED, bouton ET label) que
 * bouton_creer()/bouton_definir_desactive() dans ecran_macros.c -- voir son
 * commentaire de tete pour le recit complet de la lecon (revue finale jalon
 * 2b). Version minimale ici (pas de LV_LABEL_LONG_DOT, pas d'alignement
 * parametrable) : les libelles de jog ("X-", "Z+", ...) sont fixes et
 * courts, jamais tronques. */
static lv_obj_t *jog_bouton_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y,
                                   lv_coord_t largeur, lv_coord_t hauteur)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, largeur, hauteur);
    lv_obj_set_pos(bouton, x, y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_color_t couleur_desactivee =
        lv_color_mix(lv_color_hex(COULEUR_BOUTON), lv_color_hex(COULEUR_FOND), BOUTON_DESACTIVE_MELANGE);
    lv_obj_set_style_bg_color(bouton, couleur_desactivee, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 6, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_GRISE), LV_STATE_DISABLED);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

static void jog_bouton_definir_desactive(lv_obj_t *bouton, bool desactive)
{
    lv_obj_t *label = lv_obj_get_child(bouton, 0);
    if (desactive) {
        lv_obj_add_state(bouton, LV_STATE_DISABLED);
        if (label != NULL) {
            lv_obj_add_state(label, LV_STATE_DISABLED);
        }
    } else {
        lv_obj_remove_state(bouton, LV_STATE_DISABLED);
        if (label != NULL) {
            lv_obj_remove_state(label, LV_STATE_DISABLED);
        }
    }
}

/* Construit {"script":"<script>"} via cJSON (jamais un snprintf a la main) :
 * un script de jog (klipper_gcode_jog()) contient de VRAIS retours a la
 * ligne (SAVE_GCODE_STATE ... \n G91 \n ...) -- un octet 0x0A brut dans une
 * chaine JSON violerait RFC 8259, cJSON echappe correctement au lieu de le
 * laisser passer tel quel. Meme choix, meme raison que le chemin gcode de
 * backend_moonraker.c (voir son commentaire "ECART DELIBERE par rapport au
 * chemin macro", pres de est_gcode), reproduit ici a la couche ecran plutot
 * qu'au backend : le contrat BACKEND_ACTION_GCODE (core/backend.h) exige
 * deja du JSON valide au moment d'entrer dans la file de ui_commander(), pas
 * seulement au moment ou backend_moonraker.c le retransmet a Moonraker.
 * Rend false SANS TOUCHER `sortie` si un argument est invalide, si cJSON
 * echoue (memoire), ou si le resultat ne tient pas dans `taille`. */
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

/* Helper local (brief tache 4) : enveloppe `script` puis l'envoie via le
 * meme seam que le reste de l'interface (ui_commander(), voir source_etat.h)
 * -- ESP_OK ne dit toujours que "acceptee en file", jamais "executee avec
 * succes" (meme limite que bouton_macro_cb() dans ecran_macros.c). Pas de
 * habillage_notifier() ici, DELIBEREMENT, contrairement a bouton_macro_cb() :
 * un jog est un geste REPETE et rapide (plusieurs taps par seconde pendant
 * un positionnement fin), un bandeau qui se rearme a chaque tap serait du
 * bruit plutot qu'une aide -- la position affichee (ctx->position, mise a
 * jour au cycle suivant par mettre_a_jour()) est deja le retour visuel
 * attendu pour cette action-la. */
static void envoyer_gcode(const char *script)
{
    if (script == NULL) {
        return;
    }
    char arguments[ECRAN_ACCUEIL_IDLE_GCODE_ARGS_MAX];
    if (!construire_arguments_gcode(script, arguments, sizeof(arguments))) {
        return; /* ne devrait jamais arriver : ECRAN_ACCUEIL_IDLE_GCODE_ARGS_MAX suffit toujours */
    }
    ui_commander(BACKEND_ACTION_GCODE, arguments);
}

/* Tache 4 : lit le pas courant du selecteur AU MOMENT DU CLIC (jamais mis en
 * cache ailleurs -- meme discipline que bouton_macro_cb() qui relit
 * ctx->macros_filtrees/ctx->page au moment du clic, pas avant), construit le
 * gcode via klipper_gcode_jog() et l'envoie. */
static void jog_bouton_cb(lv_event_t *e)
{
    ecran_accueil_idle_jog_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }
    if (lv_obj_has_state(cible, LV_STATE_DISABLED)) {
        /* Garde defensive : LV_STATE_DISABLED bloque deja un appui tactile
         * reel (lv_indev.c), mais host-test envoie l'evenement directement
         * via lv_obj_send_event(), qui ne passe jamais par cette
         * verification -- meme raisonnement que bouton_macro_cb() dans
         * ecran_macros.c. Verifiee ICI sur LE BOUTON CLIQUE lui-meme, pas un
         * drapeau d'ecran a l'echelle globale : le grisage du pad de jog est
         * PAR AXE (spec tache 4 §7), un clic sur X+ doit rester refuse
         * independamment de l'etat de Z, et inversement. */
        return;
    }

    float pas = selecteur_pas_valeur(&info->ctx->selecteur_pas);
    uint16_t vitesse = (info->axe == 'Z') ? JOG_VITESSE_Z_MM_MIN : JOG_VITESSE_XY_MM_MIN;
    char script[KLIPPER_GCODE_MAX];
    if (!klipper_gcode_jog(script, sizeof(script), info->axe, info->signe * pas, vitesse)) {
        return; /* ne devrait jamais arriver : axe/pas/vitesse toujours valides depuis ce pad */
    }
    envoyer_gcode(script);
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

    /* Tache 4 : pad de jog XY/Z, six boutons a position fixe -- voir le bloc
     * de constantes JOG_* en tete de fichier pour la mise en page ("autour
     * d'un centre" + colonne Z a cote). `libelle`/`axe`/`signe`/position
     * sont ranges cote a cote pour que la boucle de construction reste
     * courte, l'ordre des lignes DOIT rester synchronise avec
     * ECRAN_ACCUEIL_IDLE_JOG_* (ecran_accueil_idle.h). */
    static const struct {
        char        axe;
        float       signe;
        const char *libelle;
        lv_coord_t  x, y, largeur, hauteur;
    } JOG_DEFS[ECRAN_ACCUEIL_IDLE_JOG_NB] = {
        { 'X', -1.0f, "X-", JOG_COL0_X, JOG_ROW1_Y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        { 'X',  1.0f, "X+", JOG_COL2_X, JOG_ROW1_Y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        { 'Y', -1.0f, "Y-", JOG_COL1_X, JOG_ROW2_Y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        { 'Y',  1.0f, "Y+", JOG_COL1_X, JOG_ROW0_Y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        { 'Z',  1.0f, "Z+", JOG_Z_X,    JOG_Z_Y0,   JOG_Z_LARGEUR,      JOG_Z_HAUTEUR },
        { 'Z', -1.0f, "Z-", JOG_Z_X,    JOG_Z_Y1,   JOG_Z_LARGEUR,      JOG_Z_HAUTEUR },
    };
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_IDLE_JOG_NB; i++) {
        ctx->jog_boutons[i] = jog_bouton_creer(ctx->zone_controles, JOG_DEFS[i].libelle, JOG_DEFS[i].x,
                                                JOG_DEFS[i].y, JOG_DEFS[i].largeur, JOG_DEFS[i].hauteur);
        ctx->jog_infos[i].ctx = ctx;
        ctx->jog_infos[i].axe = JOG_DEFS[i].axe;
        ctx->jog_infos[i].signe = JOG_DEFS[i].signe;
        lv_obj_add_event_cb(ctx->jog_boutons[i], jog_bouton_cb, LV_EVENT_CLICKED, &ctx->jog_infos[i]);
    }

    /* Selecteur de pas (tache 4) : cree via son propre widget partage
     * (selecteur_pas.h), puis repositionne/redimensionne explicitement ICI
     * -- meme droit que l'ecran appelant de progression_t (voir son
     * commentaire de tete), le widget lui-meme ne connait rien de cette
     * mise en page. */
    selecteur_pas_creer(&ctx->selecteur_pas, ctx->zone_controles);
    lv_obj_set_size(ctx->selecteur_pas.racine, SELECTEUR_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_pas.racine, SELECTEUR_X, SELECTEUR_Y);

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
    snprintf(texte_position, sizeof(texte_position), "X:%s Y:%s Z:%s", pos_x, pos_y, pos_z);
    lv_label_set_text(ctx->position, texte_position);

    char texte_outil[24];
    if (nb_extrudeurs > 0) {
        snprintf(texte_outil, sizeof(texte_outil), "Active: T%u", (unsigned)e->outil_actif);
    } else {
        snprintf(texte_outil, sizeof(texte_outil), "Active: --");
    }
    lv_label_set_text(ctx->outil_actif_nom, texte_outil);

    /* --- Grisage du pad de jog, PAR AXE (spec tache 4 §7) : un bouton est
     * desactive si les donnees sont perimees OU si SON axe n'est pas
     * reference -- sans exception pour Z (un jog sur un axe non homee est
     * refuse par Klipper de toute facon, l'interface ne doit pas mentir).
     * Reevalue a CHAQUE appel, meme discipline que le grisage des cellules
     * ci-dessus : un appel a donnees_perimees=false doit toujours rendre
     * exactement l'etat qui correspond aux axes REELLEMENT references,
     * jamais un etat herite d'un appel precedent. */
    for (uint8_t i = 0; i < ECRAN_ACCUEIL_IDLE_JOG_NB; i++) {
        bool axe_reference;
        switch (ctx->jog_infos[i].axe) {
        case 'X':
            axe_reference = axe_x_reference;
            break;
        case 'Y':
            axe_reference = axe_y_reference;
            break;
        case 'Z':
        default:
            axe_reference = axe_z_reference;
            break;
        }
        jog_bouton_definir_desactive(ctx->jog_boutons[i], donnees_perimees || !axe_reference);
    }

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
    /* Rangee Macros : non listee explicitement par le brief de la tache 3
     * ("cellules, position, controles"), grisee quand meme pour rester
     * visuellement coherente avec le reste de l'ecran perime -- ce
     * placeholder ne porte de toute facon aucun etat LV_STATE_DISABLED
     * avant la tache 7. Le selecteur de pas, lui, N'EST PAS grise sur
     * donnees_perimees (ni desactive) : choisir un pas reste un reglage
     * purement local a l'ecran, sans effet tant qu'aucun jog n'est envoye
     * -- le grisage des boutons de jog ci-dessus est deja la garde reelle. */
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
