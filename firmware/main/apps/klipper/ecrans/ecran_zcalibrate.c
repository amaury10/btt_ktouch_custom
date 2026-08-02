/* Implémentation : voir ecran_zcalibrate.h pour le contrat, le pourquoi du
 * placeholder "Probe Offset: -" et de la politique de grisage minimale.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : une ligne
 * d'info (valeur Z relue à gauche, "Probe Offset: -" à droite), puis les
 * boutons "Start Probe"/"Start Endstop" pleine largeur, un sélecteur de pas
 * (.01/.05/.1/.5/1/5 mm), les boutons "Raise Nozzle"/"Lower Nozzle", et enfin
 * "Accept"/"Abort" -- géométrie vérifiée par _Static_assert, même discipline
 * que ecran_reglage_fin.c/ecran_deplacer.c. */
#include "ecran_zcalibrate.h"

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
#define ZONE_ECART 14 /* écart vertical entre deux zones (info/rangée de boutons/sélecteur) */

/* --- ligne d'info : valeur Z (gauche) + placeholder Probe Offset (droite) - */
#define INFO_Y        MARGE
#define INFO_HAUTEUR  26
#define VALEUR_Z_LARGEUR      200
#define PROBE_OFFSET_LARGEUR  300
#define VALEUR_Z_X     MARGE
#define PROBE_OFFSET_X (LARGEUR_CONTENU - MARGE - PROBE_OFFSET_LARGEUR)

/* --- deux boutons pleine largeur, réutilisés par TROIS rangées (Start,
 * Raise/Lower, Accept/Abort) -- même largeur partout, seule la hauteur varie
 * par rangée (touche tactile plus grande pour Raise/Lower/Accept/Abort, les
 * boutons manipulés en boucle pendant la calibration). */
#define BOUTON_LARGEUR_2 ((LARGEUR_CONTENU - 2 * MARGE - ZONE_ECART) / 2)
#define BOUTON_GAUCHE_X  MARGE
#define BOUTON_DROIT_X   (MARGE + BOUTON_LARGEUR_2 + ZONE_ECART)

#define START_Y        (INFO_Y + INFO_HAUTEUR + ZONE_ECART)
#define START_HAUTEUR  56

#define SELECTEUR_CAPTION_HAUTEUR 22
#define SELECTEUR_ECART_INTERNE    8 /* entre la légende et le sélecteur */
#define SELECTEUR_HAUTEUR         50
#define PAS_LABEL_Y     (START_Y + START_HAUTEUR + ZONE_ECART)
#define PAS_SELECTEUR_Y (PAS_LABEL_Y + SELECTEUR_CAPTION_HAUTEUR + SELECTEUR_ECART_INTERNE)
#define SELECTEUR_LARGEUR (LARGEUR_CONTENU - 2 * MARGE) /* un seul sélecteur, pleine largeur */

#define RAISE_Y       (PAS_SELECTEUR_Y + SELECTEUR_HAUTEUR + ZONE_ECART)
#define RAISE_HAUTEUR 72

#define ACCEPT_Y       (RAISE_Y + RAISE_HAUTEUR + ZONE_ECART)
#define ACCEPT_HAUTEUR 72

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_reglage_fin.c/ecran_ventilateurs.c/
 * ecran_temperatures.c (voir leur commentaire complet) : bande couverte par
 * le bandeau de notification de habillage.c, en coordonnées ABSOLUES
 * d'écran. */
#define BARRE_HAUTEUR_ECRAN    44
#define HAUTEUR_ECRAN_TOTALE  480
#define BANDEAU_HAUTEUR_ECRAN  60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(2 * BOUTON_LARGEUR_2 + ZONE_ECART + 2 * MARGE == LARGEUR_CONTENU,
                "les deux boutons d'une rangee ne remplissent plus exactement la largeur du contenu");
_Static_assert(VALEUR_Z_X + VALEUR_Z_LARGEUR <= PROBE_OFFSET_X,
                "la valeur Z chevauche le placeholder Probe Offset");
_Static_assert(PROBE_OFFSET_X + PROBE_OFFSET_LARGEUR + MARGE <= LARGEUR_CONTENU,
                "le placeholder Probe Offset deborde de la largeur du contenu");
_Static_assert(ACCEPT_Y + ACCEPT_HAUTEUR <= HAUTEUR_CONTENU,
                "la rangee Accept/Abort deborde de la hauteur du contenu");
/* Même garde-fou que PRESETS_Y dans ecran_ventilateurs.c/ecran_temperatures.c/
 * les selecteurs de ecran_reglage_fin.c ("PLAFOND REEL de CONTROLES_HAUTEUR"
 * dans l'ancien idle) : le bas de la derniere rangee, en coordonnees
 * ABSOLUES d'ecran, doit rester au-dessus du bandeau de notification. */
_Static_assert(BARRE_HAUTEUR_ECRAN + ACCEPT_Y + ACCEPT_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la rangee Accept/Abort chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Tampon suffisant pour {"script":"<gcode>"} -- même raisonnement que
 * GCODE_ARGS_MAX dans ecran_deplacer.c/ecran_reglage_fin.c/
 * ecran_temperatures.c/ecran_extruder.c/ecran_ventilateurs.c. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Pas de TESTZ en µm, ORDRE FIXE {.01, .05, .1, .5, 1, 5} mm -- même index
 * que ECRAN_ZCALIBRATE_PAS_* (le .h). */
static const int32_t PAS_UM[ECRAN_ZCALIBRATE_PAS_NB] = { 10, 50, 100, 500, 1000, 5000 };

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_deplacer.c/ecran_reglage_fin.c/
 * ecran_temperatures.c/ecran_extruder.c/ecran_ventilateurs.c (voir leur
 * commentaire complet sur pourquoi cJSON plutot qu'un snprintf a la main).
 * Copie plutot que partage, meme choix que le reste de ce depot. */
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

/* "Z: 1.84" si `reference` est vrai, "Z: -" sinon -- ne jamais présenter
 * comme mesurée une position qu'aucun homing n'a établie, même politique que
 * formater_axe() dans ecran_deplacer.c. Deux décimales fixes (contrairement
 * au formatage gcode de klipper_gcode_offset_z(), qui trime les zéros de
 * fin) : un affichage utilisateur veut une largeur stable. */
static void formater_z(char *sortie, size_t taille, float valeur, bool reference)
{
    if (!reference) {
        snprintf(sortie, taille, "Z: -");
        return;
    }
    snprintf(sortie, taille, "Z: %.2f", (double)valeur);
}

/* Bouton -/+ , EN GRAND -- copie de bouton_creer() de ecran_deplacer.c/
 * ecran_reglage_fin.c (lv_obj_remove_style_all() ôte le thème par défaut ET
 * sa transition de couleur animée, même raison). */
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

/* Relit le pas courant ET le rôle du bouton cliqué (jamais mis en cache
 * ailleurs -- même discipline que bouton_cb() de ecran_reglage_fin.c),
 * construit le gcode via klipper_gcode_calibration_z()/klipper_gcode_testz()
 * et l'envoie. Index de pas borné défensivement (selecteur_choix_index()
 * rend déjà 0..nb-1, mais ce fichier ne fait jamais confiance aveuglément à
 * un état externe, même politique que le reste de ce dépôt). */
static void bouton_cb(lv_event_t *e)
{
    ecran_zcalibrate_bouton_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    char script[KLIPPER_GCODE_MAX];
    bool ok = false;

    switch (info->role) {
    case ECRAN_ZCALIBRATE_ROLE_START_PROBE:
        ok = klipper_gcode_calibration_z(script, sizeof(script), KLIPPER_ZCAL_PROBE);
        break;
    case ECRAN_ZCALIBRATE_ROLE_START_ENDSTOP:
        ok = klipper_gcode_calibration_z(script, sizeof(script), KLIPPER_ZCAL_ENDSTOP);
        break;
    case ECRAN_ZCALIBRATE_ROLE_ACCEPT:
        ok = klipper_gcode_calibration_z(script, sizeof(script), KLIPPER_ZCAL_ACCEPT);
        break;
    case ECRAN_ZCALIBRATE_ROLE_ABORT:
        ok = klipper_gcode_calibration_z(script, sizeof(script), KLIPPER_ZCAL_ABORT);
        break;
    case ECRAN_ZCALIBRATE_ROLE_RAISE:
    case ECRAN_ZCALIBRATE_ROLE_LOWER: {
        uint8_t indice = selecteur_choix_index(&info->ctx->selecteur_pas);
        if (indice >= ECRAN_ZCALIBRATE_PAS_NB) {
            indice = ECRAN_ZCALIBRATE_PAS_NB - 1;
        }
        int32_t delta_um = PAS_UM[indice];
        if (info->role == ECRAN_ZCALIBRATE_ROLE_LOWER) {
            delta_um = -delta_um;
        }
        ok = klipper_gcode_testz(script, sizeof(script), delta_um);
        break;
    }
    default:
        return;
    }

    if (ok) {
        envoyer_gcode(script);
    }
}

static void ecran_zcalibrate_construire(lv_obj_t *parent, void *contexte)
{
    ecran_zcalibrate_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- ligne d'info : valeur Z (gauche) + placeholder Probe Offset
     * (droite, texte FIXE -- voir le commentaire de tête du .h, jamais
     * modifié après cette construction). ---------------------------------- */
    ctx->valeur_z = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->valeur_z, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->valeur_z, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->valeur_z, "");
    lv_obj_set_size(ctx->valeur_z, VALEUR_Z_LARGEUR, INFO_HAUTEUR);
    lv_obj_set_pos(ctx->valeur_z, VALEUR_Z_X, INFO_Y);

    ctx->valeur_probe_offset = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->valeur_probe_offset, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->valeur_probe_offset, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->valeur_probe_offset, "Probe Offset: -");
    lv_obj_set_size(ctx->valeur_probe_offset, PROBE_OFFSET_LARGEUR, INFO_HAUTEUR);
    lv_obj_set_style_text_align(ctx->valeur_probe_offset, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(ctx->valeur_probe_offset, PROBE_OFFSET_X, INFO_Y);

    /* --- sélecteur de pas générique, pleine largeur, ENTRE les deux
     * rangées Start et Raise/Lower (brief : layout du panneau). ----------- */
    ctx->label_pas = legende_creer(parent, "Step (mm)", MARGE, PAS_LABEL_Y, SELECTEUR_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_pas, parent,
                           (const char *const[]){ "0.01", "0.05", "0.1", "0.5", "1", "5" }, ECRAN_ZCALIBRATE_PAS_NB,
                           ECRAN_ZCALIBRATE_PAS_DEFAUT);
    lv_obj_set_size(ctx->selecteur_pas.racine, SELECTEUR_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_pas.racine, MARGE, PAS_SELECTEUR_Y);

    /* --- six boutons, ORDRE FIXE (brief : "Start Probe, Start Endstop,
     * Raise Nozzle, Lower Nozzle, Accept, Abort") -- trois rangées de deux,
     * même largeur (BOUTON_LARGEUR_2), hauteur propre à chaque rangée. ----- */
    static const struct {
        ecran_zcalibrate_role_t role;
        const char             *libelle;
        lv_coord_t              x, y, hauteur;
    } BOUTON_DEFS[ECRAN_ZCALIBRATE_BOUTON_NB] = {
        [ECRAN_ZCALIBRATE_BOUTON_START_PROBE] = { ECRAN_ZCALIBRATE_ROLE_START_PROBE, "Start Probe",
                                                    BOUTON_GAUCHE_X, START_Y, START_HAUTEUR },
        [ECRAN_ZCALIBRATE_BOUTON_START_ENDSTOP] = { ECRAN_ZCALIBRATE_ROLE_START_ENDSTOP, "Start Endstop",
                                                      BOUTON_DROIT_X, START_Y, START_HAUTEUR },
        [ECRAN_ZCALIBRATE_BOUTON_RAISE] = { ECRAN_ZCALIBRATE_ROLE_RAISE, "Raise Nozzle",
                                             BOUTON_GAUCHE_X, RAISE_Y, RAISE_HAUTEUR },
        [ECRAN_ZCALIBRATE_BOUTON_LOWER] = { ECRAN_ZCALIBRATE_ROLE_LOWER, "Lower Nozzle",
                                             BOUTON_DROIT_X, RAISE_Y, RAISE_HAUTEUR },
        [ECRAN_ZCALIBRATE_BOUTON_ACCEPT] = { ECRAN_ZCALIBRATE_ROLE_ACCEPT, "Accept",
                                              BOUTON_GAUCHE_X, ACCEPT_Y, ACCEPT_HAUTEUR },
        [ECRAN_ZCALIBRATE_BOUTON_ABORT] = { ECRAN_ZCALIBRATE_ROLE_ABORT, "Abort",
                                             BOUTON_DROIT_X, ACCEPT_Y, ACCEPT_HAUTEUR },
    };
    for (uint8_t i = 0; i < ECRAN_ZCALIBRATE_BOUTON_NB; i++) {
        ctx->boutons[i] = bouton_creer(parent, BOUTON_DEFS[i].libelle, &lv_font_montserrat_20, BOUTON_DEFS[i].x,
                                        BOUTON_DEFS[i].y, BOUTON_LARGEUR_2, BOUTON_DEFS[i].hauteur);
        ctx->bouton_infos[i].ctx = ctx;
        ctx->bouton_infos[i].role = BOUTON_DEFS[i].role;
        lv_obj_add_event_cb(ctx->boutons[i], bouton_cb, LV_EVENT_CLICKED, &ctx->bouton_infos[i]);
    }
}

/* Brief (step 3) : "mettre_a_jour : relit position[2], formate, grise la
 * SEULE valeur Z sur donnees_perimees -- les boutons d'action ne sont
 * JAMAIS grisés (voir le commentaire de tête du .h)". Le placeholder Probe
 * Offset n'est jamais retouché ici (texte fixe posé une fois par
 * construire(), voir son commentaire). */
static void ecran_zcalibrate_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_zcalibrate_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    bool axe_z_reference = (e->axes_references & (1u << 2)) != 0;
    char texte_z[16];
    formater_z(texte_z, sizeof(texte_z), e->position[2], axe_z_reference);
    lv_label_set_text(ctx->valeur_z, texte_z);

    /* Grisage integral, style RESOLU : systematique a chaque appel, jamais
     * incremental (meme lecon que tuile_griser()/le reste de ui/). Seule la
     * valeur Z relue est grisee -- meme politique minimale que
     * ecran_deplacer.c (position/outil actif seuls grises, pas le pad de
     * jog). */
    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(ctx->valeur_z, lv_color_hex(couleur), 0);
}

const ecran_desc_t ECRAN_ZCALIBRATE = {
    .id = "zcalibrate",
    .titre = "Z Calibrate",
    .taille_contexte = sizeof(ecran_zcalibrate_ctx_t),
    .construire = ecran_zcalibrate_construire,
    .mettre_a_jour = ecran_zcalibrate_mettre_a_jour,
    .detruire = NULL,
};
