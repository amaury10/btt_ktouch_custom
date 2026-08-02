/* Implémentation : voir ecran_niveau_lit.h pour le contrat et pourquoi la
 * visualisation "coins" du brief est délibérément absente.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : une grille
 * 2x2 de quatre boutons pleine taille (~350x120, très au-dessus du minimum
 * tactile de 44px -- brief), ORDRE FIXE "Screws Adjust, Z-Tilt, QGL, Disable
 * Motors" -- géométrie vérifiée par _Static_assert, même discipline que
 * ecran_reglage_fin.c/ecran_zcalibrate.c/ecran_accueil_hub.c. */
#include "ecran_niveau_lit.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "etat_klipper.h"
#include "klipper_gcode.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE 14

/* --- Grille 2x2, deux colonnes x deux lignes -- géométrie propre à ce
 * panneau (pas de zone au-dessus, contrairement à ecran_accueil_hub.c). --- */
#define GRILLE_COLONNES 2
#define GRILLE_LIGNES   2

#define GRILLE_CELL_LARGEUR 350 /* brief : "~350x120" */
#define GRILLE_CELL_HAUTEUR 120

#define GRILLE_ECART_COLONNE (LARGEUR_CONTENU - 2 * MARGE - GRILLE_COLONNES * GRILLE_CELL_LARGEUR)
#define GRILLE_ECART_LIGNE   14

#define GRILLE_X MARGE
#define GRILLE_Y MARGE
#define GRILLE_HAUTEUR (GRILLE_LIGNES * GRILLE_CELL_HAUTEUR + (GRILLE_LIGNES - 1) * GRILLE_ECART_LIGNE)

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_reglage_fin.c/ecran_zcalibrate.c (voir leur
 * commentaire complet) : bande couverte par le bandeau de notification de
 * habillage.c, en coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN    44
#define HAUTEUR_ECRAN_TOTALE  480
#define BANDEAU_HAUTEUR_ECRAN  60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

/* Les deux colonnes remplissent EXACTEMENT la largeur du contenu, même
 * discipline que MENU_COLONNES dans ecran_accueil_hub.c. */
_Static_assert(2 * MARGE + GRILLE_COLONNES * GRILLE_CELL_LARGEUR + (GRILLE_COLONNES - 1) * GRILLE_ECART_COLONNE
                   == LARGEUR_CONTENU,
                "les deux colonnes de la grille ne remplissent plus exactement la largeur du contenu");
_Static_assert(GRILLE_Y + GRILLE_HAUTEUR <= HAUTEUR_CONTENU,
                "la grille 2x2 deborde de la hauteur du contenu");
/* Même garde-fou que PRESETS_Y/ACCEPT_Y dans les autres panneaux ("PLAFOND
 * REEL de CONTROLES_HAUTEUR" dans l'ancien idle) : le bas de la grille, en
 * coordonnées ABSOLUES d'écran, doit rester au-dessus du bandeau de
 * notification. */
_Static_assert(BARRE_HAUTEUR_ECRAN + GRILLE_Y + GRILLE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la grille 2x2 chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND         0x10161D
#define COULEUR_BOUTON       0x2A3644
#define COULEUR_TEXTE_BOUTON 0xFFFFFF

/* Tampon suffisant pour {"script":"<gcode>"} -- même raisonnement que
 * GCODE_ARGS_MAX dans ecran_deplacer.c/ecran_reglage_fin.c/
 * ecran_zcalibrate.c/ecran_temperatures.c/ecran_extruder.c/
 * ecran_ventilateurs.c. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_deplacer.c/ecran_reglage_fin.c/
 * ecran_zcalibrate.c/ecran_temperatures.c/ecran_extruder.c/
 * ecran_ventilateurs.c (voir leur commentaire complet sur pourquoi cJSON
 * plutot qu'un snprintf a la main). Copie plutot que partage, meme choix que
 * le reste de ce depot. */
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

/* Bouton, EN GRAND -- copie de bouton_creer() de ecran_deplacer.c/
 * ecran_reglage_fin.c/ecran_zcalibrate.c (lv_obj_remove_style_all() ôte le
 * thème par défaut ET sa transition de couleur animée, même raison). */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y, lv_coord_t largeur,
                               lv_coord_t hauteur)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_remove_style_all(bouton);
    lv_obj_set_size(bouton, largeur, hauteur);
    lv_obj_set_pos(bouton, x, y);
    lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 10, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

/* Relit uniquement l'action associée au bouton cliqué (posée en user_data à
 * la construction, voir plus bas) -- aucun `ctx` à relire, contrairement à
 * bouton_cb() de ecran_reglage_fin.c/ecran_zcalibrate.c (voir le commentaire
 * de tête du .h, "aucun bouton n'a besoin du contexte"). */
static void bouton_cb(lv_event_t *e)
{
    klipper_lit_action_t *action = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (action == NULL || cible == NULL) {
        return;
    }

    char script[KLIPPER_GCODE_MAX];
    if (klipper_gcode_niveau_lit(script, sizeof(script), *action)) {
        envoyer_gcode(script);
    }
}

/* Action de chaque bouton, ORDRE FIXE (voir ECRAN_NIVEAU_LIT_BOUTON_* dans le
 * .h) -- statique plutôt que dans le contexte : jamais modifiée après
 * construire(), aucune raison de la dupliquer par instance. */
static const klipper_lit_action_t ACTIONS[ECRAN_NIVEAU_LIT_BOUTON_NB] = {
    [ECRAN_NIVEAU_LIT_BOUTON_SCREWS]  = KLIPPER_LIT_SCREWS,
    [ECRAN_NIVEAU_LIT_BOUTON_ZTILT]   = KLIPPER_LIT_ZTILT,
    [ECRAN_NIVEAU_LIT_BOUTON_QGL]     = KLIPPER_LIT_QGL,
    [ECRAN_NIVEAU_LIT_BOUTON_DISABLE] = KLIPPER_LIT_DISABLE,
};

static const char *const LIBELLES[ECRAN_NIVEAU_LIT_BOUTON_NB] = {
    [ECRAN_NIVEAU_LIT_BOUTON_SCREWS]  = "Screws Adjust",
    [ECRAN_NIVEAU_LIT_BOUTON_ZTILT]   = "Z-Tilt",
    [ECRAN_NIVEAU_LIT_BOUTON_QGL]     = "QGL",
    [ECRAN_NIVEAU_LIT_BOUTON_DISABLE] = "Disable Motors",
};

static void ecran_niveau_lit_construire(lv_obj_t *parent, void *contexte)
{
    ecran_niveau_lit_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- grille 2x2 : x/y calcules par cellule, meme idiome que la grille
     * de menu de ecran_accueil_hub.c. --------------------------------------*/
    for (uint8_t i = 0; i < ECRAN_NIVEAU_LIT_BOUTON_NB; i++) {
        uint8_t ligne = i / GRILLE_COLONNES;
        uint8_t colonne = i % GRILLE_COLONNES;
        lv_coord_t x = (lv_coord_t)(GRILLE_X + colonne * (GRILLE_CELL_LARGEUR + GRILLE_ECART_COLONNE));
        lv_coord_t y = (lv_coord_t)(GRILLE_Y + ligne * (GRILLE_CELL_HAUTEUR + GRILLE_ECART_LIGNE));

        ctx->boutons[i] = bouton_creer(parent, LIBELLES[i], x, y, GRILLE_CELL_LARGEUR, GRILLE_CELL_HAUTEUR);
        lv_obj_add_event_cb(ctx->boutons[i], bouton_cb, LV_EVENT_CLICKED, (void *)&ACTIONS[i]);
    }
}

const ecran_desc_t ECRAN_NIVEAU_LIT = {
    .id = "niveau_lit",
    .titre = "Bed Level",
    .taille_contexte = sizeof(ecran_niveau_lit_ctx_t),
    .construire = ecran_niveau_lit_construire,
    .mettre_a_jour = NULL, /* aucune valeur relue, aucun grisage -- voir le .h */
    .detruire = NULL,
};
