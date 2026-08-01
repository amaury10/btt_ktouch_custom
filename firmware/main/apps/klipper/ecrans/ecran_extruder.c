/* Implémentation : voir ecran_extruder.h pour le contrat et la décision de
 * périmètre V1 ("PAS de sélecteur d'outil ici").
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : une ligne
 * d'état "Actif : T<n>" en haut, suivie de la tuile de température de la
 * buse active (widget partagé tuile.h, LECTURE SEULE -- le RÉGLAGE de la
 * consigne vit dans ecran_temperatures.c, jamais ici, voir le commentaire de
 * tête du .h), puis deux sélecteurs (Longueur, Vitesse) côte à côte, et
 * enfin deux gros boutons pleine largeur de colonne (Extruder / Rétracter)
 * qui envoient G1 E<+-longueur> F<vitesse> via klipper_gcode_extrude()
 * (tâche 1, jalon 3b). Toutes les constantes de position sont vérifiées les
 * unes par rapport aux autres via _Static_assert (même discipline que
 * ecran_deplacer.c/ecran_macros.c) : un futur ajustement qui ferait déborder
 * ou chevaucher une zone devient une erreur de compilation. */
#include "ecran_extruder.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "etat_klipper.h"
#include "klipper_gcode.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE      14
#define ZONE_ECART 14 /* écart vertical entre la ligne d'état/tuile, les sélecteurs et les boutons */

#define STATUT_Y       10
#define STATUT_HAUTEUR 26

#define TUILE_Y (STATUT_Y + STATUT_HAUTEUR + ZONE_ECART)
/* Budget vertical réservé à la tuile (widget LV_SIZE_CONTENT, voir tuile.c) :
 * libellé (police 20, ~24px) + pad_row(4) + valeur (police 48, ~57px) +
 * pad_row(4) + consigne (police 20, ~24px) ~= 109px -- 120 laisse une marge
 * sans avoir à recopier ici la géométrie interne de tuile.c. */
#define TUILE_HAUTEUR_RESERVEE 120

#define SELECTEUR_CAPTION_HAUTEUR 22
#define SELECTEUR_HAUTEUR         50
#define SELECTEUR_ECART_INTERNE   8 /* entre une légende et son sélecteur */

#define SELECTEURS_LABEL_Y     (TUILE_Y + TUILE_HAUTEUR_RESERVEE + ZONE_ECART)
#define SELECTEURS_SELECTEUR_Y (SELECTEURS_LABEL_Y + SELECTEUR_CAPTION_HAUTEUR + SELECTEUR_ECART_INTERNE)

/* Grille à deux colonnes égales, réutilisée par les sélecteurs ET les
 * boutons Extruder/Rétracter ci-dessous -- même largeur, alignement vertical
 * cohérent des deux rangées. */
#define COLONNE_ECART    14
#define COLONNE_LARGEUR  ((LARGEUR_CONTENU - 2 * MARGE - COLONNE_ECART) / 2)
#define COLONNE_GAUCHE_X MARGE
#define COLONNE_DROITE_X (MARGE + COLONNE_LARGEUR + COLONNE_ECART)

#define BOUTONS_Y      (SELECTEURS_SELECTEUR_Y + SELECTEUR_HAUTEUR + ZONE_ECART)
#define BOUTON_HAUTEUR 90 /* meme hauteur que JOG_BOUTON_HAUTEUR dans ecran_deplacer.c, tres au-dessus de 44px */
#define BOUTON_LARGEUR COLONNE_LARGEUR

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_temperatures.c (voir son commentaire complet) :
 * bande couverte par le bandeau de notification de habillage.c, en
 * coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(2 * COLONNE_LARGEUR + COLONNE_ECART == LARGEUR_CONTENU - 2 * MARGE,
                "les deux colonnes (selecteurs/boutons) ne remplissent plus exactement la largeur du contenu");
_Static_assert(BOUTONS_Y + BOUTON_HAUTEUR <= HAUTEUR_CONTENU,
                "les boutons Extruder/Retracter debordent de la hauteur du contenu");
/* Même garde-fou que PRESETS_Y/MENU_ZONE_Y dans ecran_temperatures.c/
 * ecran_accueil_hub.c ("PLAFOND REEL de CONTROLES_HAUTEUR" dans l'ancien
 * idle) : le bas des boutons, en coordonnées ABSOLUES d'écran, doit rester
 * au-dessus du bandeau de notification -- sans quoi une notification
 * recouvrirait ET bloquerait le tap sur Extruder/Retracter. */
_Static_assert(BARRE_HAUTEUR_ECRAN + BOUTONS_Y + BOUTON_HAUTEUR <= BANDEAU_Y_ECRAN,
                "les boutons Extruder/Retracter chevauchent la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Longueurs d'extrusion (mm) par index du sélecteur Longueur -- brief : "5 /
 * 10 / 25 / 50, défaut index 1 (10)". */
static const float LONGUEUR_MM[4] = { 5.0f, 10.0f, 25.0f, 50.0f };

/* Vitesses d'extrusion (mm/min) par index du sélecteur Vitesse -- brief :
 * "Lent / Moyen / Rapide -> 120 / 300 / 600". */
static const uint16_t VITESSE_MM_MIN[3] = { 120, 300, 600 };

/* Tampon suffisant pour {"script":"<gcode>"} -- même raisonnement que
 * GCODE_ARGS_MAX dans ecran_deplacer.c/ecran_temperatures.c : KLIPPER_GCODE_MAX
 * plus la marge du wrapper JSON. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_deplacer.c/ecran_temperatures.c (voir
 * leur commentaire complet sur pourquoi cJSON plutôt qu'un snprintf à la
 * main). Copie plutôt que partage, même choix que le reste de ce dépôt. */
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

/* Bouton Extruder/Rétracter, EN GRAND -- copie de bouton_creer() dans
 * ecran_deplacer.c (même raison : lv_obj_remove_style_all() ôte le thème par
 * défaut ET sa transition de couleur animée sur bg_color, pas de style
 * LV_STATE_DISABLED dédié -- cet écran ne désactive jamais ces boutons). */
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
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

static lv_obj_t *legende_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y, lv_coord_t largeur)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(label, texte);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, largeur);
    return label;
}

/* Lit la longueur ET la vitesse courantes (au moment du clic, jamais mises
 * en cache ailleurs -- même discipline que jog_bouton_cb() de
 * ecran_deplacer.c), construit le gcode via klipper_gcode_extrude() et
 * l'envoie. Index bornés défensivement (selecteur_choix_index() rend déjà
 * 0..nb-1, mais ce fichier ne fait jamais confiance aveuglément à un état
 * externe, même politique que le reste de ce dépôt). */
static void extrude_bouton_cb(lv_event_t *e)
{
    ecran_extruder_bouton_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    uint8_t indice_longueur = selecteur_choix_index(&info->ctx->selecteur_longueur);
    if (indice_longueur >= 4) {
        indice_longueur = 3;
    }
    float longueur = LONGUEUR_MM[indice_longueur];

    uint8_t indice_vitesse = selecteur_choix_index(&info->ctx->selecteur_vitesse);
    if (indice_vitesse >= 3) {
        indice_vitesse = 2;
    }
    uint16_t vitesse = VITESSE_MM_MIN[indice_vitesse];

    char script[KLIPPER_GCODE_MAX];
    if (!klipper_gcode_extrude(script, sizeof(script), info->signe * longueur, vitesse)) {
        return; /* ne devrait jamais arriver : longueur/vitesse toujours valides depuis ces selecteurs */
    }
    envoyer_gcode(script);
}

static void ecran_extruder_construire(lv_obj_t *parent, void *contexte)
{
    ecran_extruder_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- ligne d'état + tuile de la buse active (lecture seule) ---------- */
    ctx->actif_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->actif_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->actif_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->actif_label, "");
    lv_obj_set_pos(ctx->actif_label, MARGE, STATUT_Y);

    tuile_creer(&ctx->tuile, parent, "Buse");
    lv_obj_set_pos(ctx->tuile.racine, MARGE, TUILE_Y);

    /* --- sélecteurs Longueur / Vitesse, côte à côte ---------------------- */
    ctx->label_longueur = legende_creer(parent, "Longueur", COLONNE_GAUCHE_X, SELECTEURS_LABEL_Y, COLONNE_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_longueur, parent,
                           (const char *const[]){ "5", "10", "25", "50" }, 4, ECRAN_EXTRUDER_LONGUEUR_DEFAUT);
    lv_obj_set_size(ctx->selecteur_longueur.racine, COLONNE_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_longueur.racine, COLONNE_GAUCHE_X, SELECTEURS_SELECTEUR_Y);

    ctx->label_vitesse = legende_creer(parent, "Vitesse", COLONNE_DROITE_X, SELECTEURS_LABEL_Y, COLONNE_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_vitesse, parent,
                           (const char *const[]){ "Lent", "Moyen", "Rapide" }, 3, ECRAN_EXTRUDER_VITESSE_DEFAUT);
    lv_obj_set_size(ctx->selecteur_vitesse.racine, COLONNE_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_vitesse.racine, COLONNE_DROITE_X, SELECTEURS_SELECTEUR_Y);

    /* --- deux gros boutons Extruder / Rétracter --------------------------
     * G1 E positif = extrusion, negatif = retraction (klipper_gcode.h) --
     * independants de l'outil (G1 E agit sur l'extrudeur ACTIF de Klipper),
     * aucun nom de chauffeur requis ici (voir le commentaire de tete du
     * .h). */
    ctx->bouton_extruder = bouton_creer(parent, "Extruder", COLONNE_GAUCHE_X, BOUTONS_Y, BOUTON_LARGEUR,
                                         BOUTON_HAUTEUR);
    ctx->info_extruder.ctx = ctx;
    ctx->info_extruder.signe = 1.0f;
    lv_obj_add_event_cb(ctx->bouton_extruder, extrude_bouton_cb, LV_EVENT_CLICKED, &ctx->info_extruder);

    ctx->bouton_retracter = bouton_creer(parent, "Retracter", COLONNE_DROITE_X, BOUTONS_Y, BOUTON_LARGEUR,
                                          BOUTON_HAUTEUR);
    ctx->info_retracter.ctx = ctx;
    ctx->info_retracter.signe = -1.0f;
    lv_obj_add_event_cb(ctx->bouton_retracter, extrude_bouton_cb, LV_EVENT_CLICKED, &ctx->info_retracter);
}

/* Brief (step 3) : "mettre_a_jour : ligne d'etat + tuile de la buse active,
 * grisees si donnees_perimees" -- rien de plus, meme portee que
 * ecran_deplacer_mettre_a_jour(). */
static void ecran_extruder_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_extruder_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme : nb_extrudeurs promet 0..8
     * (voir etat_klipper.h) mais rien de ce cote-ci ne DOIT lui faire
     * confiance aveuglement, meme politique que le reste de ce depot
     * vis-a-vis d'un etat malforme -- outil_actif est borne par
     * `outil_actif < nb_extrudeurs` avant tout indexage de extrudeurs[]. */
    uint8_t nb_extrudeurs = e->nb_extrudeurs;
    if (nb_extrudeurs > KLIPPER_EXTRUDEURS_MAX) {
        nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
    }
    uint8_t outil_actif = e->outil_actif;
    bool buse_valide = (nb_extrudeurs > 0) && (outil_actif < nb_extrudeurs) && e->extrudeurs[outil_actif].presente;

    char texte_actif[24];
    char valeur[16];
    char consigne[16];
    if (buse_valide) {
        snprintf(texte_actif, sizeof(texte_actif), "Actif : T%u", (unsigned)outil_actif);
        ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[outil_actif].actuelle);
        ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[outil_actif].consigne);
    } else {
        snprintf(texte_actif, sizeof(texte_actif), "Actif : --");
        snprintf(valeur, sizeof(valeur), "--");
        snprintf(consigne, sizeof(consigne), "--");
    }
    lv_label_set_text(ctx->actif_label, texte_actif);
    tuile_definir_valeur(&ctx->tuile, valeur);
    tuile_definir_consigne(&ctx->tuile, consigne);

    /* --- Grisage integral, style RESOLU -- systematique a chaque appel,
     * jamais incremental (meme lecon que tuile_griser()/le reste de ui/). */
    tuile_griser(&ctx->tuile, donnees_perimees);
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(ctx->actif_label, lv_color_hex(couleur), 0);
}

const ecran_desc_t ECRAN_EXTRUDER = {
    .id = "extruder",
    .titre = "Extrude",
    .taille_contexte = sizeof(ecran_extruder_ctx_t),
    .construire = ecran_extruder_construire,
    .mettre_a_jour = ecran_extruder_mettre_a_jour,
    .detruire = NULL,
};
