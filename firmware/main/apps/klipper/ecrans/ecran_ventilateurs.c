/* Implémentation : voir ecran_ventilateurs.h pour le contrat, les trois
 * moyens de régler la même valeur et le piège d'interaction slider/
 * mettre_a_jour().
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : le bouton
 * "NN %" en haut (retour visuel + saisie numérique), le slider juste en
 * dessous, puis la rangée de 5 préréglages -- rien d'autre, cet écran ne
 * pilote qu'un seul ventilateur (voir le commentaire de tête du .h).
 * Géométrie vérifiée par _Static_assert, même discipline que
 * ecran_deplacer.c/ecran_temperatures.c/ecran_extruder.c. */
#include "ecran_ventilateurs.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "clavier.h"
#include "etat_klipper.h"
#include "habillage.h" /* habillage_notifier() */
#include "klipper_gcode.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE      14
#define ZONE_ECART 14 /* écart vertical entre le bouton valeur, le slider et les préréglages */

#define VALEUR_Y       20
#define VALEUR_HAUTEUR 90
#define VALEUR_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)

#define SLIDER_Y       (VALEUR_Y + VALEUR_HAUTEUR + ZONE_ECART)
#define SLIDER_HAUTEUR 60
#define SLIDER_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)

#define PRESETS_Y       (SLIDER_Y + SLIDER_HAUTEUR + ZONE_ECART)
#define PRESETS_HAUTEUR 60 /* >= 44px cible tactile minimale, largement */
#define PRESETS_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_temperatures.c/ecran_extruder.c (voir leur
 * commentaire complet) : bande couverte par le bandeau de notification de
 * habillage.c, en coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(PRESETS_Y + PRESETS_HAUTEUR <= HAUTEUR_CONTENU,
                "la rangee de preregalges deborde de la hauteur du contenu");
/* Même garde-fou que PRESETS_Y/MENU_ZONE_Y dans ecran_temperatures.c/
 * ecran_accueil_hub.c ("PLAFOND REEL de CONTROLES_HAUTEUR" dans l'ancien
 * idle) : le bas de la rangée de préréglages, en coordonnées ABSOLUES
 * d'écran, doit rester au-dessus du bandeau de notification -- sans quoi
 * une notification recouvrirait ET bloquerait le tap sur les préréglages. */
_Static_assert(BARRE_HAUTEUR_ECRAN + PRESETS_Y + PRESETS_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la rangee de preregalges chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
/* Bleu "actif", meme constante que COULEUR_ACTIF dans selecteur_choix.c/
 * ecran_accueil_hub.c -- couleur normale (non perimee) de l'indicateur du
 * slider. Le slider N'EST PAS enveloppe (lv_obj_remove_style_all(), voir le
 * commentaire de tete du .h) : sans cette reaffirmation explicite de
 * bg_color sur ses parties INDICATOR/KNOB, le theme par defaut de LVGL
 * n'offre AUCUN style "disabled" pour lv_slider (contrairement a
 * lv_switch/lv_checkbox/lv_textarea, verifie dans
 * simulateur/lvgl/src/themes/default/lv_theme_default.c) : le grisage sur
 * donnees_perimees doit donc etre pose ET leve explicitement a chaque
 * mettre_a_jour(), jamais via LV_STATE_DISABLED. */
#define COULEUR_SLIDER_INDICATEUR 0x3B82F6

/* Tampon suffisant pour {"script":"<gcode>"} -- meme raisonnement que
 * GCODE_ARGS_MAX dans ecran_deplacer.c/ecran_temperatures.c/
 * ecran_extruder.c : KLIPPER_GCODE_MAX plus la marge du wrapper JSON. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_deplacer.c/ecran_temperatures.c/
 * ecran_extruder.c (voir leur commentaire complet sur pourquoi cJSON plutot
 * qu'un snprintf a la main). Copie plutot que partage, meme choix que le
 * reste de ce depot. */
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

/* Envoie klipper_gcode_ventilateur(pct) -- SEUL chemin par lequel un gcode
 * quitte cet ecran, partage par le relachement du slider, un prereglage, et
 * la saisie clavier validee (voir le commentaire de tete du .h, "TROIS
 * moyens"). `pct` deja borne [0, 100] par l'appelant. */
static void envoyer_vitesse(uint8_t pct)
{
    char script[KLIPPER_GCODE_MAX];
    if (!klipper_gcode_ventilateur(script, sizeof(script), pct)) {
        return; /* ne devrait jamais arriver : pct deja borne 0..100 par tout appelant */
    }
    envoyer_gcode(script);
}

static void formater_pct(char *sortie, size_t taille, uint8_t pct)
{
    snprintf(sortie, taille, "%u %%", (unsigned)pct);
}

/* LV_EVENT_VALUE_CHANGED : retour visuel UNIQUEMENT, jamais de gcode ici
 * (anti-flood Klipper -- voir le commentaire de tete du .h). Se declenche a
 * chaque pas du glissement ET au premier positionnement programmatique
 * (lv_slider_set_value() dans mettre_a_jour() ci-dessous), ce qui est
 * exactement ce qu'on veut : le label doit toujours refleter la position
 * actuelle du slider, qu'elle vienne du doigt ou du backend. */
static void slider_value_changed_cb(lv_event_t *e)
{
    ecran_ventilateurs_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    if (ctx == NULL || slider == NULL) {
        return;
    }
    int32_t valeur = lv_slider_get_value(slider);
    char texte[8];
    formater_pct(texte, sizeof(texte), (uint8_t)valeur);
    lv_label_set_text(ctx->label_valeur, texte);
}

/* LV_EVENT_PRESSED : arme `glissement_en_cours` -- voir le commentaire de
 * tete du .h pour pourquoi mettre_a_jour() en a besoin. */
static void slider_pressed_cb(lv_event_t *e)
{
    ecran_ventilateurs_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    ctx->glissement_en_cours = true;
}

/* LV_EVENT_RELEASED : SEUL evenement du slider qui envoie du gcode (voir le
 * commentaire de tete du .h). Desarme aussi `glissement_en_cours`, que le
 * relachement vienne d'un vrai glissement ou (comme dans
 * host-test/tests/test_ecran_ventilateurs.c) d'un evenement synthetique
 * envoye directement sans LV_EVENT_PRESSED prealable -- desarmer sans
 * condition est sans danger, le drapeau etant deja faux dans ce second cas. */
static void slider_released_cb(lv_event_t *e)
{
    ecran_ventilateurs_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    if (ctx == NULL || slider == NULL) {
        return;
    }
    ctx->glissement_en_cours = false;

    int32_t valeur = lv_slider_get_value(slider);
    if (valeur < 0) {
        valeur = 0;
    } else if (valeur > 100) {
        valeur = 100;
    }
    envoyer_vitesse((uint8_t)valeur);
}

/* Rappel du clavier numerique ouvert par valeur_bouton_cb() ci-dessous --
 * meme politique de parsing que cellule_clavier_rappel() dans
 * ecran_temperatures.c (strtol, borne EXACTE, aucun gcode hors bornes),
 * bornes [ECRAN_VENTILATEURS_PCT_MIN, ECRAN_VENTILATEURS_PCT_MAX] au lieu de
 * [0, 350]. `contexte` EST directement `ctx` (pas de sous-structure par
 * cellule -- il n'y a qu'UN seul reglage sur cet ecran). */
static void clavier_rappel(const char *valeur, void *contexte)
{
    ecran_ventilateurs_ctx_t *ctx = contexte;
    if (valeur == NULL || ctx == NULL) {
        return;
    }

    char *fin = NULL;
    errno = 0;
    long cible = strtol(valeur, &fin, 10);
    bool numerique = (fin != valeur) && (fin != NULL) && (*fin == '\0') && (errno == 0);
    bool dans_bornes =
        numerique && cible >= ECRAN_VENTILATEURS_PCT_MIN && cible <= ECRAN_VENTILATEURS_PCT_MAX;

    if (!dans_bornes) {
        habillage_notifier("Invalid fan speed (0-100)", true);
        return;
    }

    envoyer_vitesse((uint8_t)cible);
}

/* Tap sur le bouton "NN %" : ouvre le clavier numerique, prerempli avec la
 * position ACTUELLE du slider -- qui reflete toujours la derniere valeur
 * connue du backend (mettre_a_jour() la recale, sauf pendant un glissement,
 * et un glissement/un tap sur un AUTRE bouton ne peuvent pas se produire
 * simultanement sur un ecran tactile mono-doigt). */
static void valeur_bouton_cb(lv_event_t *e)
{
    ecran_ventilateurs_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (ctx == NULL || cible == NULL) {
        return;
    }

    int32_t valeur = lv_slider_get_value(ctx->slider);
    char valeur_initiale[8];
    snprintf(valeur_initiale, sizeof(valeur_initiale), "%d", (int)valeur);
    clavier_ouvrir("Fan speed", valeur_initiale, CLAVIER_NUMERIQUE, clavier_rappel, ctx);
}

/* Clic sur un prereglage : envoie directement sa cible fixe -- aucun etat a
 * relire (contrairement a preset_bouton_cb() de ecran_temperatures.c, qui
 * doit relire l'outil actif). */
static void preset_bouton_cb(lv_event_t *e)
{
    ecran_ventilateurs_preset_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || cible == NULL) {
        return;
    }
    envoyer_vitesse(info->pct);
}

static void ecran_ventilateurs_construire(lv_obj_t *parent, void *contexte)
{
    ecran_ventilateurs_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- bouton "NN %" : retour visuel du slider + saisie numerique -------
     * lv_button (comme bouton_creer() de ecran_deplacer.c/ecran_extruder.c) :
     * deja LV_OBJ_FLAG_CLICKABLE par construction, pas besoin de le poser a
     * la main (contrairement aux tuiles de temperature -- lv_obj_create() nu
     * -- de ecran_temperatures.c). ------------------------------------- */
    ctx->bouton_valeur = lv_button_create(parent);
    lv_obj_remove_style_all(ctx->bouton_valeur);
    lv_obj_set_size(ctx->bouton_valeur, VALEUR_LARGEUR, VALEUR_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_valeur, MARGE, VALEUR_Y);
    lv_obj_set_style_bg_opa(ctx->bouton_valeur, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->bouton_valeur, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(ctx->bouton_valeur, 0, 0);
    lv_obj_set_style_shadow_width(ctx->bouton_valeur, 0, 0);
    lv_obj_set_style_radius(ctx->bouton_valeur, 10, 0);

    ctx->label_valeur = lv_label_create(ctx->bouton_valeur);
    lv_obj_set_style_text_font(ctx->label_valeur, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ctx->label_valeur, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(ctx->label_valeur, "0 %");
    lv_obj_center(ctx->label_valeur);

    lv_obj_add_event_cb(ctx->bouton_valeur, valeur_bouton_cb, LV_EVENT_CLICKED, ctx);

    /* --- slider, natif, AUCUN wrapper (voir le commentaire de tete du .h) */
    ctx->slider = lv_slider_create(parent);
    lv_obj_set_size(ctx->slider, SLIDER_LARGEUR, SLIDER_HAUTEUR);
    lv_obj_set_pos(ctx->slider, MARGE, SLIDER_Y);
    lv_slider_set_range(ctx->slider, 0, 100);
    lv_slider_set_value(ctx->slider, 0, LV_ANIM_OFF);
    ctx->glissement_en_cours = false;

    lv_obj_add_event_cb(ctx->slider, slider_pressed_cb, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(ctx->slider, slider_released_cb, LV_EVENT_RELEASED, ctx);
    lv_obj_add_event_cb(ctx->slider, slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);

    /* --- rangee de 5 prereglages, largeur egale -- meme idiome flex que
     * zone_preregalges dans ecran_temperatures.c. ------------------------ */
    static const struct {
        const char *libelle;
        uint8_t     pct;
    } PRESET_DEFS[ECRAN_VENTILATEURS_PRESET_NB] = {
        [ECRAN_VENTILATEURS_PRESET_OFF] = { "Off", 0 },
        [ECRAN_VENTILATEURS_PRESET_25]  = { "25", 25 },
        [ECRAN_VENTILATEURS_PRESET_50]  = { "50", 50 },
        [ECRAN_VENTILATEURS_PRESET_75]  = { "75", 75 },
        [ECRAN_VENTILATEURS_PRESET_100] = { "100", 100 },
    };
    const char *libelles[ECRAN_VENTILATEURS_PRESET_NB];
    for (uint8_t i = 0; i < ECRAN_VENTILATEURS_PRESET_NB; i++) {
        libelles[i] = PRESET_DEFS[i].libelle;
    }
    /* Index par defaut arbitraire (Off) : `preregalges.index_actif` n'a
     * aucun sens persistant sur cet ecran (voir le commentaire de tete du
     * .h) -- n'importe quelle valeur dans [0, NB-1] conviendrait tout
     * autant. */
    selecteur_choix_creer(&ctx->preregalges, parent, libelles, ECRAN_VENTILATEURS_PRESET_NB,
                           ECRAN_VENTILATEURS_PRESET_OFF);
    lv_obj_set_size(ctx->preregalges.racine, PRESETS_LARGEUR, PRESETS_HAUTEUR);
    lv_obj_set_pos(ctx->preregalges.racine, MARGE, PRESETS_Y);

    for (uint8_t i = 0; i < ECRAN_VENTILATEURS_PRESET_NB; i++) {
        ctx->preset_infos[i].pct = PRESET_DEFS[i].pct;
        lv_obj_add_event_cb(ctx->preregalges.boutons[i], preset_bouton_cb, LV_EVENT_CLICKED, &ctx->preset_infos[i]);
    }
}

/* Brief : recale slider+label sur `ventilateurs[0].vitesse` (0.0-1.0,
 * arrondi a l'entier de pourcentage le plus proche) SAUF pendant un
 * glissement actif (voir le commentaire de tete du .h, "Piege
 * d'interaction"), et grise slider+label sur `donnees_perimees` -- style
 * RESOLU, systematique a chaque appel, jamais incremental (meme lecon que
 * tuile_griser()/le reste de ui/). */
static void ecran_ventilateurs_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_ventilateurs_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme (vitesse hors [0.0, 1.0],
     * NaN...) : meme politique que consigne_u16() dans ecran_temperatures.c
     * vis-a-vis d'une consigne malformee -- isnan() teste EXPLICITEMENT en
     * premier, comparer un NaN a une borne rend faux dans les DEUX sens. */
    float vitesse = e->ventilateurs[0].vitesse;
    if (isnan(vitesse) || vitesse < 0.0f) {
        vitesse = 0.0f;
    } else if (vitesse > 1.0f) {
        vitesse = 1.0f;
    }
    uint8_t pct = (uint8_t)(vitesse * 100.0f + 0.5f); /* round-to-nearest, vitesse*100 tient toujours dans [0,100] */

    char texte[8];
    formater_pct(texte, sizeof(texte), pct);
    lv_label_set_text(ctx->label_valeur, texte);

    if (!ctx->glissement_en_cours) {
        lv_slider_set_value(ctx->slider, pct, LV_ANIM_OFF);
    }

    uint32_t couleur_texte = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    uint32_t couleur_indicateur = donnees_perimees ? COULEUR_GRISE : COULEUR_SLIDER_INDICATEUR;
    uint32_t couleur_knob = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    lv_obj_set_style_text_color(ctx->label_valeur, lv_color_hex(couleur_texte), 0);
    lv_obj_set_style_bg_color(ctx->slider, lv_color_hex(couleur_indicateur), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ctx->slider, lv_color_hex(couleur_knob), LV_PART_KNOB);
}

const ecran_desc_t ECRAN_VENTILATEURS = {
    .id = "ventilateurs",
    .titre = "Fan",
    .taille_contexte = sizeof(ecran_ventilateurs_ctx_t),
    .construire = ecran_ventilateurs_construire,
    .mettre_a_jour = ecran_ventilateurs_mettre_a_jour,
    .detruire = NULL,
};
