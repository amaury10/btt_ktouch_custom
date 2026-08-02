/* Implémentation : voir ecran_fichiers.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : une ligne
 * d'avertissement de troncature tout en haut (masquée sauf
 * `fichiers_tronques`), une grille 4x4 de boutons (16 fichiers par page), une
 * ligne de pagination en bas ("< Page X/Y >"). Ossature reprise quasi telle
 * quelle de ecran_macros.c (voir son commentaire de tête) -- toutes les
 * constantes de position sont dérivées les unes des autres (voir les
 * _Static_assert plus bas), même discipline. */
#include "ecran_fichiers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "confirmation.h"
#include "klipper_fichiers.h"
#include "klipper_gcode.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c -- PAS les 800px
                              * hérités de ecran_macros.c, voir le commentaire de tête de
                              * ecran_fichiers.h */
#define HAUTEUR_CONTENU 436

#define MARGE 20

#define AVERTISSEMENT_Y       8
#define AVERTISSEMENT_HAUTEUR 22

#define GRILLE_Y            36
#define BOUTON_ECART_X       8
#define BOUTON_ECART_Y       8
/* Largeur de bouton dérivée de LARGEUR_CONTENU (742, pas les 800px de
 * ecran_macros.c) -- division entière, le _Static_assert plus bas vérifie
 * que le reste (arrondi vers le bas) laisse toujours un budget <=
 * LARGEUR_CONTENU, jamais une égalité stricte requise ici (contrairement à
 * la grille de menu de ecran_accueil_hub.c, qui doit remplir EXACTEMENT sa
 * largeur -- cette grille-ci, comme celle de ecran_macros.c, n'a pas cette
 * contrainte). */
#define BOUTON_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - (ECRAN_FICHIERS_PAGE_COLONNES - 1) * BOUTON_ECART_X) / \
                        ECRAN_FICHIERS_PAGE_COLONNES)
#define BOUTON_HAUTEUR      64
#define GRILLE_BAS (GRILLE_Y + ECRAN_FICHIERS_PAGE_LIGNES * BOUTON_HAUTEUR + \
                    (ECRAN_FICHIERS_PAGE_LIGNES - 1) * BOUTON_ECART_Y)

#define PAGINATION_HAUTEUR 44
/* Même piège documenté par ecran_macros.c (voir son commentaire complet sur
 * PAGINATION_Y) : positionnée ICI juste sous la grille, jamais dérivée du
 * bas de la zone de contenu -- le _Static_assert plus bas vérifie la même
 * chose en coordonnées ABSOLUES d'écran. */
#define PAGINATION_Y (GRILLE_BAS + 8)
#define BOUTON_PAGE_LARGEUR 60

/* Même convention que ecran_macros.c/ecran_accueil_hub.c : bande couverte
 * par le bandeau de notification de habillage.c, en coordonnées ABSOLUES
 * d'écran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_AVERTISSEMENT    0xF1C40F /* meme ambre que ecran_macros.c/habillage.c */

/* Même raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_macros.c (voir
 * son commentaire complet). */
#define BOUTON_DESACTIVE_MELANGE 90

_Static_assert(MARGE + ECRAN_FICHIERS_PAGE_COLONNES * BOUTON_LARGEUR +
                       (ECRAN_FICHIERS_PAGE_COLONNES - 1) * BOUTON_ECART_X + MARGE <= LARGEUR_CONTENU,
               "la grille de fichiers deborde de la largeur du contenu");
_Static_assert(AVERTISSEMENT_Y + AVERTISSEMENT_HAUTEUR <= GRILLE_Y,
               "la ligne d'avertissement de troncature chevauche la premiere rangee de la grille");
_Static_assert(GRILLE_BAS <= PAGINATION_Y,
               "la grille de fichiers chevauche la ligne de pagination");
_Static_assert(PAGINATION_Y + PAGINATION_HAUTEUR <= HAUTEUR_CONTENU,
               "la ligne de pagination deborde de la hauteur du contenu");
_Static_assert(BARRE_HAUTEUR_ECRAN + PAGINATION_Y + PAGINATION_HAUTEUR <= BANDEAU_Y_ECRAN,
               "la ligne de pagination chevauche la bande du bandeau de notification de l'habillage");

/* Tampon suffisant pour {"script":"<gcode>"} -- copie exacte de
 * construire_arguments_gcode()/envoyer_gcode() dans rail_actions.c (voir son
 * commentaire complet : un vrai octet 0x0A dans un script doit etre echappe
 * par un JSON conforme, jamais un snprintf a la main). Copie plutot que
 * partage, meme choix que le reste de ce depot. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

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

/* Même paire de styles locaux (DEFAULT + DISABLED, bouton ET label) que
 * bouton_creer() dans ecran_macros.c -- voir son commentaire complet. */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y,
                               lv_coord_t largeur, lv_coord_t hauteur, const lv_font_t *police,
                               lv_text_align_t alignement_texte)
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
    lv_obj_set_style_radius(bouton, 8, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, police, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_GRISE), LV_STATE_DISABLED);
    /* Points de suspension plutot que deborder sur un chemin de fichier long
     * (jusqu'a KLIPPER_FICHIER_MAX-1 caracteres, eventuellement avec un
     * sous-dossier) -- meme technique que le nom de macro de ecran_macros.c. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, alignement_texte, 0);
    lv_obj_set_width(label, largeur - 12);
    /* Sans cet appel, le PREMIER lv_label_set_text() ci-dessous verrait une
     * largeur de contenu pas encore resolue -- meme piege, meme correctif
     * que ecran_macros.c/ecran_configuration.c, voir leur commentaire. */
    lv_obj_update_layout(label);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

static void bouton_definir_desactive(lv_obj_t *bouton, bool desactive)
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

/* min(1, ceil(nb_fichiers / ECRAN_FICHIERS_PAGE_TAILLE)) : au moins une
 * page, meme quand la liste est vide (une page vide, qui affiche "No
 * files") -- meme raisonnement que nb_pages() dans ecran_macros.c. */
static uint8_t nb_pages(uint8_t nb_fichiers)
{
    if (nb_fichiers == 0) {
        return 1;
    }
    return (uint8_t)((nb_fichiers + ECRAN_FICHIERS_PAGE_TAILLE - 1) / ECRAN_FICHIERS_PAGE_TAILLE);
}

/* Recalcule entierement ce que la page courante doit montrer -- meme
 * discipline (systematique, jamais incrementale) que afficher_page() dans
 * ecran_macros.c, voir son commentaire complet. */
static void afficher_page(ecran_fichiers_ctx_t *ctx)
{
    uint8_t pages = nb_pages(ctx->nb_fichiers);
    if (ctx->page >= pages) {
        ctx->page = (uint8_t)(pages - 1);
    }

    bool vide = (ctx->nb_fichiers == 0);
    if (vide) {
        lv_obj_clear_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE), 0);

    for (uint8_t emplacement = 0; emplacement < ECRAN_FICHIERS_PAGE_TAILLE; emplacement++) {
        uint16_t indice = (uint16_t)ctx->page * ECRAN_FICHIERS_PAGE_TAILLE + emplacement;
        if (!vide && indice < ctx->nb_fichiers) {
            lv_label_set_text(ctx->labels[emplacement], ctx->fichiers_copie[indice]);
            lv_obj_clear_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        }
        bouton_definir_desactive(ctx->boutons[emplacement], ctx->donnees_perimees);
    }

    if (ctx->fichiers_tronques) {
        lv_label_set_text(ctx->avertissement,
                           "Some files are not shown (list truncated)");
        lv_obj_clear_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(ctx->avertissement,
                                 lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_AVERTISSEMENT), 0);

    bool page_unique = (pages <= 1) || vide;
    if (page_unique) {
        lv_obj_add_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);
        char texte[16];
        snprintf(texte, sizeof(texte), "Page %u/%u", (unsigned)(ctx->page + 1), (unsigned)pages);
        lv_label_set_text(ctx->page_label, texte);
    }
    lv_obj_set_style_text_color(ctx->page_label,
                                 lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE), 0);
    bouton_definir_desactive(ctx->bouton_precedent, ctx->donnees_perimees || ctx->page == 0);
    bouton_definir_desactive(ctx->bouton_suivant,
                              ctx->donnees_perimees || (uint8_t)(ctx->page + 1) >= pages);
}

/* Rappel de la confirmation d'impression : ne demarre l'impression que si
 * l'utilisateur a REELLEMENT confirme (voir confirmation.h : le rappel est
 * appele exactement une fois, dialogue deja referme). Un declin (confirme ==
 * false) ne fait rien -- une impression ne doit jamais partir d'un
 * effleurement accidentel. `ctx->nom_attente` a ete rempli par
 * bouton_fichier_cb() juste avant l'ouverture du dialogue (voir son
 * commentaire et celui du champ dans ecran_fichiers.h) -- toujours le nom
 * REELLEMENT affiche au moment du tap, jamais une saisie manuelle. */
static void rappel_confirmer_impression(bool confirme, void *contexte)
{
    ecran_fichiers_ctx_t *ctx = contexte;
    if (!confirme || ctx == NULL) {
        return;
    }
    char script[KLIPPER_GCODE_MAX];
    if (klipper_gcode_imprimer_fichier(script, sizeof(script), ctx->nom_attente)) {
        envoyer_gcode(script);
    }
}

/* Tap sur un fichier : ouvre une confirmation ("Print file?", le nom du
 * fichier en message) -- JAMAIS d'action directe, contrairement a
 * bouton_macro_cb() dans ecran_macros.c (voir le commentaire de tete de
 * ecran_fichiers.h pour pourquoi ce chemin-ci en a besoin). Le nom est relu
 * ICI, au moment du clic, depuis `ctx->fichiers_copie` -- jamais un nom mis
 * en cache ailleurs -- puis copie dans `ctx->nom_attente` pour survivre
 * jusqu'au rappel de la confirmation (voir son commentaire). */
static void bouton_fichier_cb(lv_event_t *e)
{
    ecran_fichiers_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_fichiers_ctx_t *ctx = info->ctx;
    if (ctx->donnees_perimees) {
        /* Garde defensive : LV_STATE_DISABLED bloque deja un appui tactile
         * reel, mais host-test envoie l'evenement directement via
         * lv_obj_send_event() -- meme raisonnement que bouton_macro_cb()
         * dans ecran_macros.c. */
        return;
    }
    uint16_t indice = (uint16_t)ctx->page * ECRAN_FICHIERS_PAGE_TAILLE + info->emplacement;
    if (indice >= ctx->nb_fichiers) {
        return; /* bouton cache/vide sur cette page ; ne devrait jamais arriver via un vrai doigt */
    }

    /* Copie bornee manuelle, PAS snprintf(dst, N, "%s", src) -- `src` (une
     * entree de ctx->fichiers_copie) fait jusqu'a KLIPPER_FICHIER_MAX-1
     * octets de long, ce que gcc ne peut pas prouver borne a la compilation ;
     * meme piege, meme correctif que ecran_macros.c (voir son commentaire
     * sur bouton_macro_cb()/-Werror=format-truncation). */
    const char *nom = ctx->fichiers_copie[indice];
    size_t longueur = strlen(nom);
    if (longueur >= sizeof(ctx->nom_attente)) {
        longueur = sizeof(ctx->nom_attente) - 1;
    }
    memcpy(ctx->nom_attente, nom, longueur);
    ctx->nom_attente[longueur] = '\0';

    confirmation_ouvrir("Print file?", ctx->nom_attente, "Print", /*destructif=*/false,
                         rappel_confirmer_impression, ctx);
}

static void bouton_precedent_cb(lv_event_t *e)
{
    ecran_fichiers_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees || ctx->page == 0) {
        return;
    }
    ctx->page--;
    afficher_page(ctx);
}

static void bouton_suivant_cb(lv_event_t *e)
{
    ecran_fichiers_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees) {
        return;
    }
    if ((uint8_t)(ctx->page + 1) >= nb_pages(ctx->nb_fichiers)) {
        return;
    }
    ctx->page++;
    afficher_page(ctx);
}

static void ecran_fichiers_construire(lv_obj_t *parent, void *contexte)
{
    ecran_fichiers_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->avertissement = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->avertissement, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->avertissement, lv_color_hex(COULEUR_AVERTISSEMENT), 0);
    lv_label_set_text(ctx->avertissement, "");
    lv_obj_set_pos(ctx->avertissement, MARGE, AVERTISSEMENT_Y);
    lv_obj_set_size(ctx->avertissement, LARGEUR_CONTENU - 2 * MARGE, AVERTISSEMENT_HAUTEUR);
    lv_obj_add_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN);

    /* "No files" (jamais un ecran muet, meme regle que "No macros" dans
     * ecran_macros.c) -- l'instantane du dernier connect peut ne contenir
     * aucun fichier, ce n'est pas une erreur. */
    ctx->vide = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->vide, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->vide, "No files");
    lv_obj_set_pos(ctx->vide, MARGE, GRILLE_Y);
    lv_obj_set_size(ctx->vide, LARGEUR_CONTENU - 2 * MARGE, PAGINATION_Y - GRILLE_Y);
    lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t emplacement = 0; emplacement < ECRAN_FICHIERS_PAGE_TAILLE; emplacement++) {
        uint8_t colonne = emplacement % ECRAN_FICHIERS_PAGE_COLONNES;
        uint8_t ligne = emplacement / ECRAN_FICHIERS_PAGE_COLONNES;
        lv_coord_t x = MARGE + colonne * (BOUTON_LARGEUR + BOUTON_ECART_X);
        lv_coord_t y = GRILLE_Y + ligne * (BOUTON_HAUTEUR + BOUTON_ECART_Y);

        ctx->boutons[emplacement] =
            bouton_creer(parent, "", x, y, BOUTON_LARGEUR, BOUTON_HAUTEUR, &lv_font_montserrat_14,
                         LV_TEXT_ALIGN_LEFT);
        ctx->labels[emplacement] = lv_obj_get_child(ctx->boutons[emplacement], 0);
        lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);

        ctx->emplacements[emplacement].ctx = ctx;
        ctx->emplacements[emplacement].emplacement = emplacement;
        lv_obj_add_event_cb(ctx->boutons[emplacement], bouton_fichier_cb, LV_EVENT_CLICKED,
                             &ctx->emplacements[emplacement]);
    }

    lv_coord_t pagination_centre_x = LARGEUR_CONTENU / 2;
    ctx->bouton_precedent = bouton_creer(parent, "<", pagination_centre_x - BOUTON_PAGE_LARGEUR - 90,
                                          PAGINATION_Y, BOUTON_PAGE_LARGEUR, PAGINATION_HAUTEUR,
                                          &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_precedent, bouton_precedent_cb, LV_EVENT_CLICKED, ctx);

    ctx->bouton_suivant = bouton_creer(parent, ">", pagination_centre_x + 90, PAGINATION_Y,
                                        BOUTON_PAGE_LARGEUR, PAGINATION_HAUTEUR, &lv_font_montserrat_20,
                                        LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_suivant, bouton_suivant_cb, LV_EVENT_CLICKED, ctx);

    ctx->page_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->page_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->page_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->page_label, "");
    lv_obj_set_size(ctx->page_label, 160, PAGINATION_HAUTEUR);
    lv_obj_set_pos(ctx->page_label, pagination_centre_x - 80, PAGINATION_Y);
    lv_obj_set_style_text_align(ctx->page_label, LV_TEXT_ALIGN_CENTER, 0);

    ctx->nb_fichiers = 0;
    ctx->fichiers_tronques = false;
    ctx->donnees_perimees = false;
    ctx->page = 0;
    ctx->nom_attente[0] = '\0';
}

static void ecran_fichiers_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_fichiers_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* La liste de fichiers NE vit plus dans etat_klipper_t (sortie vers un
     * store dedie, voir klipper_fichiers.h : les ~2 Ko dupliques dans chaque
     * copie de l'etat epuisaient la RAM interne). On la lit ICI sous verrou
     * dans une copie locale -- `e` (l'etat) reste passe/verifie par symetrie
     * avec les autres ecrans, mais n'en porte plus les fichiers. */
    klipper_fichiers_t fics;
    klipper_fichiers_lire(&fics);

    /* Copie DEFENSIVEMENT bornee AVANT de lire le premier caractere -- meme
     * raisonnement que ecran_macros.c/ecran_accueil.c :
     * `fics.noms[i]` peut occuper la totalite de KLIPPER_FICHIER_MAX sans
     * octet nul (klipper_fichiers_t est un POD a champs fixes). Aucun filtrage
     * par prefixe ici (contrairement aux macros `_prefixees`) -- un chemin
     * de fichier n'a pas cette convention Klipper, voir le commentaire de
     * tete de ecran_fichiers.h -- seuls les emplacements vides (chaine
     * vide, ne devrait normalement pas arriver dans les indices < nb)
     * sont sautes, meme garde defensive que ecran_macros.c pour un
     * emplacement vide. */
    uint8_t nb_fichiers_source = fics.nb;
    if (nb_fichiers_source > KLIPPER_FICHIERS_MAX) {
        nb_fichiers_source = KLIPPER_FICHIERS_MAX; /* meme borne defensive que ecran_macros.c */
    }
    ctx->nb_fichiers = 0;
    for (uint8_t i = 0; i < nb_fichiers_source; i++) {
        char nom_borne[KLIPPER_FICHIER_MAX + 1];
        memcpy(nom_borne, fics.noms[i], KLIPPER_FICHIER_MAX);
        nom_borne[KLIPPER_FICHIER_MAX] = '\0';
        if (nom_borne[0] == '\0') {
            continue; /* emplacement vide */
        }
        /* Copie bornee manuelle, PAS snprintf -- meme raison que
         * ecran_macros.c (voir son commentaire sur -Werror=format-truncation). */
        size_t longueur = strlen(nom_borne);
        if (longueur >= KLIPPER_FICHIER_MAX) {
            longueur = KLIPPER_FICHIER_MAX - 1;
        }
        memcpy(ctx->fichiers_copie[ctx->nb_fichiers], nom_borne, longueur);
        ctx->fichiers_copie[ctx->nb_fichiers][longueur] = '\0';
        ctx->nb_fichiers++;
    }
    ctx->fichiers_tronques = fics.tronques;
    ctx->donnees_perimees = donnees_perimees;

    afficher_page(ctx);
}

const ecran_desc_t ECRAN_FICHIERS = {
    .id = "fichiers",
    .titre = "Print",
    .taille_contexte = sizeof(ecran_fichiers_ctx_t),
    .construire = ecran_fichiers_construire,
    .mettre_a_jour = ecran_fichiers_mettre_a_jour,
    .detruire = NULL,
};
