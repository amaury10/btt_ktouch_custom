/* Implémentation : voir ecran_reglage_fin.h pour le contrat, le pourquoi de
 * la mutualisation du sélecteur % (Speed/Flow) et le chemin RELATIF de Z
 * (Z_ADJUST) contre le chemin ABSOLU de Speed/Flow (M220/M221 S<pct>).
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : trois
 * lignes label + valeur + boutons -/+ (Z-offset avec Reset en plus, puis
 * Speed, puis Flow), et sous les trois, deux sélecteurs de pas côte à côte
 * (% partagé Speed/Flow, mm pour Z). Géométrie vérifiée par _Static_assert,
 * même discipline que ecran_deplacer.c/ecran_temperatures.c/
 * ecran_extruder.c/ecran_ventilateurs.c. */
#include "ecran_reglage_fin.h"

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
#define ZONE_ECART 14 /* écart vertical entre deux lignes, et entre les lignes et les sélecteurs */

/* --- Trois lignes (Z-offset, Speed, Flow), même hauteur, empilées ------- */
#define LIGNE_HAUTEUR 64

#define LIGNE_Z_Y     MARGE
#define LIGNE_SPEED_Y (LIGNE_Z_Y + LIGNE_HAUTEUR + ZONE_ECART)
#define LIGNE_FLOW_Y  (LIGNE_SPEED_Y + LIGNE_HAUTEUR + ZONE_ECART)

/* Géométrie horizontale d'UNE ligne : label | bouton "-" | valeur | bouton
 * "+" | (Z uniquement) bouton "Reset". */
#define LIGNE_LABEL_LARGEUR  140
#define LIGNE_BOUTON_LARGEUR  64
#define LIGNE_VALEUR_LARGEUR 150
#define LIGNE_RESET_LARGEUR  110
#define LIGNE_ECART_INTERNE   10 /* écart horizontal entre deux éléments d'une même ligne */

#define LIGNE_LABEL_X  MARGE
#define LIGNE_MOINS_X  (LIGNE_LABEL_X + LIGNE_LABEL_LARGEUR + LIGNE_ECART_INTERNE)
#define LIGNE_VALEUR_X (LIGNE_MOINS_X + LIGNE_BOUTON_LARGEUR + LIGNE_ECART_INTERNE)
#define LIGNE_PLUS_X   (LIGNE_VALEUR_X + LIGNE_VALEUR_LARGEUR + LIGNE_ECART_INTERNE)
#define LIGNE_RESET_X  (LIGNE_PLUS_X + LIGNE_BOUTON_LARGEUR + LIGNE_ECART_INTERNE)

/* --- Sélecteurs de pas, sous les trois lignes, côte à côte -------------- */
#define SELECTEURS_Y (LIGNE_FLOW_Y + LIGNE_HAUTEUR + ZONE_ECART)

#define SELECTEUR_CAPTION_HAUTEUR 22
#define SELECTEUR_ECART_INTERNE    8 /* entre une légende et son sélecteur */
#define SELECTEUR_HAUTEUR         50
#define SELECTEUR_GROUPE_HAUTEUR (SELECTEUR_CAPTION_HAUTEUR + SELECTEUR_ECART_INTERNE + SELECTEUR_HAUTEUR)

#define SELECTEUR_LABEL_Y SELECTEURS_Y
#define SELECTEUR_CHOIX_Y (SELECTEUR_LABEL_Y + SELECTEUR_CAPTION_HAUTEUR + SELECTEUR_ECART_INTERNE)

#define SELECTEUR_ECART_GROUPE 14 /* écart horizontal entre les deux groupes de sélecteur */
#define SELECTEUR_LARGEUR ((LARGEUR_CONTENU - 2 * MARGE - SELECTEUR_ECART_GROUPE) / 2)
#define SELECTEUR_PCT_X MARGE
#define SELECTEUR_Z_X   (SELECTEUR_PCT_X + SELECTEUR_LARGEUR + SELECTEUR_ECART_GROUPE)

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_ventilateurs.c/ecran_temperatures.c (voir leur
 * commentaire complet) : bande couverte par le bandeau de notification de
 * habillage.c, en coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN    44
#define HAUTEUR_ECRAN_TOTALE  480
#define BANDEAU_HAUTEUR_ECRAN  60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

/* Les deux sélecteurs remplissent EXACTEMENT la largeur du contenu, même
 * discipline que SELECTEURS_LARGEUR dans ecran_deplacer.c. */
_Static_assert(MARGE + SELECTEUR_LARGEUR + SELECTEUR_ECART_GROUPE + SELECTEUR_LARGEUR + MARGE == LARGEUR_CONTENU,
                "les deux selecteurs de pas ne remplissent plus exactement la largeur du contenu");
/* Aucune ligne ne déborde de la largeur du contenu -- la ligne Z (la plus
 * large, avec son bouton Reset) suffit à couvrir les deux autres. */
_Static_assert(LIGNE_RESET_X + LIGNE_RESET_LARGEUR + MARGE <= LARGEUR_CONTENU,
                "la ligne Z-offset (avec Reset) deborde de la largeur du contenu");
_Static_assert(LIGNE_PLUS_X + LIGNE_BOUTON_LARGEUR + MARGE <= LARGEUR_CONTENU,
                "une ligne Speed/Flow deborde de la largeur du contenu");
_Static_assert(SELECTEUR_CHOIX_Y + SELECTEUR_HAUTEUR <= HAUTEUR_CONTENU,
                "les selecteurs de pas debordent de la hauteur du contenu");
/* Même garde-fou que PRESETS_Y dans ecran_ventilateurs.c/ecran_temperatures.c
 * ("PLAFOND REEL de CONTROLES_HAUTEUR" dans l'ancien idle) : le bas des
 * sélecteurs, en coordonnées ABSOLUES d'écran, doit rester au-dessus du
 * bandeau de notification. */
_Static_assert(BARRE_HAUTEUR_ECRAN + SELECTEURS_Y + SELECTEUR_GROUPE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "les selecteurs de pas chevauchent la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Tampon suffisant pour {"script":"<gcode>"} -- même raisonnement que
 * GCODE_ARGS_MAX dans ecran_deplacer.c/ecran_temperatures.c/
 * ecran_extruder.c/ecran_ventilateurs.c. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Pas du sélecteur %, même index que ECRAN_REGLAGE_FIN_PAS_PCT_* (le .h). */
static const uint16_t PAS_PCT[ECRAN_REGLAGE_FIN_PAS_PCT_NB] = { 1, 5, 10, 25 };
/* Pas du sélecteur Z, en µm -- {0.01, 0.05} mm, même index que
 * ECRAN_REGLAGE_FIN_PAS_Z_*. */
static const int32_t PAS_Z_UM[ECRAN_REGLAGE_FIN_PAS_Z_NB] = { 10, 50 };

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_deplacer.c/ecran_temperatures.c/
 * ecran_extruder.c/ecran_ventilateurs.c (voir leur commentaire complet sur
 * pourquoi cJSON plutot qu'un snprintf a la main). Copie plutot que
 * partage, meme choix que le reste de ce depot. */
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

static void formater_pct(char *sortie, size_t taille, uint16_t pct)
{
    snprintf(sortie, taille, "%u %%", (unsigned)pct);
}

/* "-0.02 mm" -- toujours deux décimales fixes (contrairement au formatage
 * gcode de klipper_gcode_offset_z(), qui trime les zéros de fin : un
 * affichage utilisateur veut une largeur stable, pas la forme la plus
 * courte). */
static void formater_z_mm(char *sortie, size_t taille, int32_t delta_um)
{
    snprintf(sortie, taille, "%.2f mm", (double)delta_um / 1000.0);
}

/* Borne un pourcentage cible dans [1, 300] -- même borne que
 * klipper_gcode_vitesse_impression()/klipper_gcode_flux() (voir
 * klipper_gcode.h). `v` est un int32_t (calcul intermédiaire pct ± pas, qui
 * peut déborder uint16_t vers le bas) plutôt qu'un uint16_t : comparer
 * `v < 1` sur un type non signé serait toujours faux. */
static uint16_t borner_pct(int32_t v)
{
    if (v < 1) {
        return 1;
    }
    if (v > 300) {
        return 300;
    }
    return (uint16_t)v;
}

/* Bouton +/- d'une des trois lignes -- voir le commentaire de tête du .h
 * ("Différence structurelle") pour pourquoi Z suit un chemin RELATIF
 * (Z_ADJUST=±pas, aucune lecture de babystep_z_um necessaire) alors que
 * Speed/Flow suivent un chemin ABSOLU (pct_connu ± pas, borne [1,300]). */
static void bouton_cb(lv_event_t *e)
{
    ecran_reglage_fin_bouton_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    char script[KLIPPER_GCODE_MAX];
    bool ok = false;

    switch (info->cible) {
    case ECRAN_REGLAGE_FIN_CIBLE_Z: {
        uint8_t indice = selecteur_choix_index(&info->ctx->selecteur_pas_z);
        if (indice >= ECRAN_REGLAGE_FIN_PAS_Z_NB) {
            indice = ECRAN_REGLAGE_FIN_PAS_Z_NB - 1;
        }
        int32_t delta_um = (int32_t)info->signe * PAS_Z_UM[indice];
        ok = klipper_gcode_offset_z(script, sizeof(script), delta_um, false);
        break;
    }
    case ECRAN_REGLAGE_FIN_CIBLE_SPEED: {
        uint8_t indice = selecteur_choix_index(&info->ctx->selecteur_pct);
        if (indice >= ECRAN_REGLAGE_FIN_PAS_PCT_NB) {
            indice = ECRAN_REGLAGE_FIN_PAS_PCT_NB - 1;
        }
        int32_t cible_pct = (int32_t)info->ctx->vitesse_pct_connue + (int32_t)info->signe * (int32_t)PAS_PCT[indice];
        ok = klipper_gcode_vitesse_impression(script, sizeof(script), borner_pct(cible_pct));
        break;
    }
    case ECRAN_REGLAGE_FIN_CIBLE_FLOW: {
        uint8_t indice = selecteur_choix_index(&info->ctx->selecteur_pct);
        if (indice >= ECRAN_REGLAGE_FIN_PAS_PCT_NB) {
            indice = ECRAN_REGLAGE_FIN_PAS_PCT_NB - 1;
        }
        int32_t cible_pct = (int32_t)info->ctx->flux_pct_connue + (int32_t)info->signe * (int32_t)PAS_PCT[indice];
        ok = klipper_gcode_flux(script, sizeof(script), borner_pct(cible_pct));
        break;
    }
    default:
        return;
    }

    if (ok) {
        envoyer_gcode(script);
    }
}

/* Bouton "Reset" de la ligne Z-offset : toujours offset_z(0, true), quel
 * que soit le pas sélectionné -- pas de sous-structure info, `ctx` posé
 * directement en user_data (voir le commentaire de ecran_reglage_fin.h). */
static void reset_bouton_cb(lv_event_t *e)
{
    lv_obj_t *cible = lv_event_get_target(e);
    if (lv_event_get_user_data(e) == NULL || cible == NULL) {
        return;
    }
    char script[KLIPPER_GCODE_MAX];
    if (klipper_gcode_offset_z(script, sizeof(script), 0, true)) {
        envoyer_gcode(script);
    }
}

/* Bouton -/+/Reset, EN GRAND -- copie de bouton_creer() de ecran_deplacer.c
 * (lv_obj_remove_style_all() ôte le thème par défaut ET sa transition de
 * couleur animée, même raison). */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, const lv_font_t *police, lv_coord_t x,
                               lv_coord_t y, lv_coord_t largeur, lv_coord_t hauteur)
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
    lv_obj_set_style_text_font(label, police, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

/* Légende statique au-dessus d'un sélecteur, ou en tête d'une ligne -- copie
 * de legende_creer() de ecran_deplacer.c. */
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

/* Valeur relue d'une ligne (ex. "100 %", "-0.02 mm") -- texte centré,
 * grisée sur donnees_perimees par mettre_a_jour() (voir plus bas). */
static lv_obj_t *valeur_creer(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t largeur, lv_coord_t hauteur)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, "");
    lv_obj_set_size(label, largeur, hauteur);
    lv_obj_set_pos(label, x, y);
    return label;
}

/* Ligne titre + valeur + bouton "-" + bouton "+" (Reset optionnel, NULL
 * sinon), à `y`. `valeur` reçoit le label de valeur créé (jamais NULL) ;
 * `bouton_moins`/`bouton_plus`/`bouton_reset` reçoivent leurs boutons
 * respectifs (`bouton_reset` ignoré si NULL en entrée -- pas de ligne
 * Reset). Factorise les trois lignes quasi identiques de construire(). */
static void ligne_creer(lv_obj_t *parent, const char *titre, lv_coord_t y, lv_obj_t **valeur,
                         lv_obj_t **bouton_moins, lv_obj_t **bouton_plus, lv_obj_t **bouton_reset)
{
    legende_creer(parent, titre, LIGNE_LABEL_X, y + (LIGNE_HAUTEUR - 20) / 2, LIGNE_LABEL_LARGEUR);
    *bouton_moins = bouton_creer(parent, "-", &lv_font_montserrat_28, LIGNE_MOINS_X, y, LIGNE_BOUTON_LARGEUR,
                                  LIGNE_HAUTEUR);
    *valeur = valeur_creer(parent, LIGNE_VALEUR_X, y, LIGNE_VALEUR_LARGEUR, LIGNE_HAUTEUR);
    *bouton_plus = bouton_creer(parent, "+", &lv_font_montserrat_28, LIGNE_PLUS_X, y, LIGNE_BOUTON_LARGEUR,
                                 LIGNE_HAUTEUR);
    if (bouton_reset != NULL) {
        *bouton_reset = bouton_creer(parent, "Reset", &lv_font_montserrat_20, LIGNE_RESET_X, y, LIGNE_RESET_LARGEUR,
                                      LIGNE_HAUTEUR);
    }
}

static void ecran_reglage_fin_construire(lv_obj_t *parent, void *contexte)
{
    ecran_reglage_fin_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- trois lignes, ORDRE FIXE (brief : "Z-offset, Speed, Flow") ----- */
    ligne_creer(parent, "Z Offset", LIGNE_Z_Y, &ctx->valeur_z,
                &ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_Z_NEG], &ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_Z_POS],
                &ctx->bouton_reset_z);
    ligne_creer(parent, "Speed", LIGNE_SPEED_Y, &ctx->valeur_speed,
                &ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_SPEED_NEG], &ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_SPEED_POS],
                NULL);
    ligne_creer(parent, "Flow", LIGNE_FLOW_Y, &ctx->valeur_flow,
                &ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_FLOW_NEG], &ctx->boutons[ECRAN_REGLAGE_FIN_BOUTON_FLOW_POS],
                NULL);

    static const struct {
        ecran_reglage_fin_cible_t cible;
        int8_t                    signe;
    } BOUTON_DEFS[ECRAN_REGLAGE_FIN_BOUTON_NB] = {
        [ECRAN_REGLAGE_FIN_BOUTON_Z_NEG]     = { ECRAN_REGLAGE_FIN_CIBLE_Z, -1 },
        [ECRAN_REGLAGE_FIN_BOUTON_Z_POS]     = { ECRAN_REGLAGE_FIN_CIBLE_Z, 1 },
        [ECRAN_REGLAGE_FIN_BOUTON_SPEED_NEG] = { ECRAN_REGLAGE_FIN_CIBLE_SPEED, -1 },
        [ECRAN_REGLAGE_FIN_BOUTON_SPEED_POS] = { ECRAN_REGLAGE_FIN_CIBLE_SPEED, 1 },
        [ECRAN_REGLAGE_FIN_BOUTON_FLOW_NEG]  = { ECRAN_REGLAGE_FIN_CIBLE_FLOW, -1 },
        [ECRAN_REGLAGE_FIN_BOUTON_FLOW_POS]  = { ECRAN_REGLAGE_FIN_CIBLE_FLOW, 1 },
    };
    for (uint8_t i = 0; i < ECRAN_REGLAGE_FIN_BOUTON_NB; i++) {
        ctx->bouton_infos[i].ctx = ctx;
        ctx->bouton_infos[i].cible = BOUTON_DEFS[i].cible;
        ctx->bouton_infos[i].signe = BOUTON_DEFS[i].signe;
        lv_obj_add_event_cb(ctx->boutons[i], bouton_cb, LV_EVENT_CLICKED, &ctx->bouton_infos[i]);
    }
    lv_obj_add_event_cb(ctx->bouton_reset_z, reset_bouton_cb, LV_EVENT_CLICKED, ctx);

    /* --- deux sélecteurs de pas, côte à côte ---------------------------- */
    ctx->label_pct = legende_creer(parent, "Step %", SELECTEUR_PCT_X, SELECTEUR_LABEL_Y, SELECTEUR_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_pct, parent, (const char *const[]){ "1", "5", "10", "25" },
                           ECRAN_REGLAGE_FIN_PAS_PCT_NB, ECRAN_REGLAGE_FIN_PAS_PCT_DEFAUT);
    lv_obj_set_size(ctx->selecteur_pct.racine, SELECTEUR_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_pct.racine, SELECTEUR_PCT_X, SELECTEUR_CHOIX_Y);

    ctx->label_pas_z = legende_creer(parent, "Step Z (mm)", SELECTEUR_Z_X, SELECTEUR_LABEL_Y, SELECTEUR_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_pas_z, parent, (const char *const[]){ "0.01", "0.05" },
                           ECRAN_REGLAGE_FIN_PAS_Z_NB, ECRAN_REGLAGE_FIN_PAS_Z_DEFAUT);
    lv_obj_set_size(ctx->selecteur_pas_z.racine, SELECTEUR_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_pas_z.racine, SELECTEUR_Z_X, SELECTEUR_CHOIX_Y);
}

/* Brief (step 3) : "mettre_a_jour : relit les 3 valeurs, formate, grise C3
 * sur donnees_perimees. Défense NaN/bornes comme ecran_ventilateurs.c." --
 * `vitesse_pct`/`flux_pct` sont des uint16_t (pas de NaN possible, voir
 * etat_klipper.h), la défense ici est une borne haute [0, 300] contre un
 * état corrompu (même politique que consigne_u16() dans
 * ecran_temperatures.c vis-à-vis d'une consigne malformée), 0 restant une
 * valeur légitime ("pas encore reçu", voir etat_klipper.h). */
static void ecran_reglage_fin_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_reglage_fin_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    uint16_t vitesse = e->vitesse_pct > 300 ? 300 : e->vitesse_pct;
    uint16_t flux = e->flux_pct > 300 ? 300 : e->flux_pct;

    /* Mises en cache AVANT tout affichage -- relues par bouton_cb() AU
     * MOMENT DU CLIC (voir le commentaire de tête du .h). */
    ctx->vitesse_pct_connue = vitesse;
    ctx->flux_pct_connue = flux;

    char texte_z[16];
    char texte_speed[8];
    char texte_flow[8];
    formater_z_mm(texte_z, sizeof(texte_z), e->babystep_z_um);
    formater_pct(texte_speed, sizeof(texte_speed), vitesse);
    formater_pct(texte_flow, sizeof(texte_flow), flux);

    lv_label_set_text(ctx->valeur_z, texte_z);
    lv_label_set_text(ctx->valeur_speed, texte_speed);
    lv_label_set_text(ctx->valeur_flow, texte_flow);

    /* Grisage integral, style RESOLU : systematique a chaque appel, jamais
     * incremental (meme lecon que tuile_griser()/le reste de ui/). Seules
     * les TROIS valeurs relues sont grisees (pas les boutons/selecteurs de
     * pas) -- meme politique minimale que ecran_deplacer.c (position/outil
     * actif seuls grises, pas le pad de jog). */
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    lv_obj_set_style_text_color(ctx->valeur_z, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->valeur_speed, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->valeur_flow, lv_color_hex(couleur), 0);
}

const ecran_desc_t ECRAN_REGLAGE_FIN = {
    .id = "reglage_fin",
    .titre = "Fine Tune",
    .taille_contexte = sizeof(ecran_reglage_fin_ctx_t),
    .construire = ecran_reglage_fin_construire,
    .mettre_a_jour = ecran_reglage_fin_mettre_a_jour,
    .detruire = NULL,
};
