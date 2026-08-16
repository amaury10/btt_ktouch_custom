/* Implémentation : voir ecran_limites.h pour le contrat, le pourquoi de
 * l'absence de bouton Reset, et la convention de grisage (donnees_perimees ET
 * "pas encore reçu" partagent la même couleur grise).
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : QUATRE
 * lignes label + valeur + boutons -/+, empilées, sans sélecteur de pas (le
 * pas est FIXE par ligne, voir PAS[] plus bas) -- géométrie vérifiée par
 * _Static_assert, même discipline que ecran_reglage_fin.c/ecran_deplacer.c/
 * ecran_temperatures.c/ecran_extruder.c/ecran_ventilateurs.c. */
#include "ecran_limites.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "etat_klipper.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE      14
#define ZONE_ECART 14 /* écart vertical entre deux lignes */

/* --- Quatre lignes, même hauteur, empilées ------------------------------- */
#define LIGNE_HAUTEUR 64

#define LIGNE_VELOCITY_Y       MARGE
#define LIGNE_ACCEL_Y          (LIGNE_VELOCITY_Y + LIGNE_HAUTEUR + ZONE_ECART)
#define LIGNE_SQV_Y             (LIGNE_ACCEL_Y + LIGNE_HAUTEUR + ZONE_ECART)
#define LIGNE_ACCEL_TO_DECEL_Y (LIGNE_SQV_Y + LIGNE_HAUTEUR + ZONE_ECART)

/* Géométrie horizontale d'UNE ligne : label | bouton "-" | valeur | bouton
 * "+". Label plus large que ecran_reglage_fin.c (260, pas 140) : "Square
 * Corner Velocity" est le plus long des quatre libellés et doit tenir sur une
 * seule ligne, à la police 14 utilisée par legende_creer(). */
#define LIGNE_LABEL_LARGEUR  260
#define LIGNE_BOUTON_LARGEUR  64
#define LIGNE_VALEUR_LARGEUR 160
#define LIGNE_ECART_INTERNE   10 /* écart horizontal entre deux éléments d'une même ligne */

#define LIGNE_LABEL_X  MARGE
#define LIGNE_MOINS_X  (LIGNE_LABEL_X + LIGNE_LABEL_LARGEUR + LIGNE_ECART_INTERNE)
#define LIGNE_VALEUR_X (LIGNE_MOINS_X + LIGNE_BOUTON_LARGEUR + LIGNE_ECART_INTERNE)
#define LIGNE_PLUS_X   (LIGNE_VALEUR_X + LIGNE_VALEUR_LARGEUR + LIGNE_ECART_INTERNE)

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_reglage_fin.c/ecran_ventilateurs.c (voir leur
 * commentaire complet) : bande couverte par le bandeau de notification de
 * habillage.c, en coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN    44
#define HAUTEUR_ECRAN_TOTALE  480
#define BANDEAU_HAUTEUR_ECRAN  60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

/* Aucune ligne ne déborde de la largeur du contenu. */
_Static_assert(LIGNE_PLUS_X + LIGNE_BOUTON_LARGEUR + MARGE <= LARGEUR_CONTENU,
                "une ligne du panneau Limits deborde de la largeur du contenu");
/* Les quatre lignes tiennent dans la hauteur du contenu, sans scroll. */
_Static_assert(LIGNE_ACCEL_TO_DECEL_Y + LIGNE_HAUTEUR <= HAUTEUR_CONTENU,
                "les quatre lignes du panneau Limits debordent de la hauteur du contenu");
/* Même garde-fou que PRESETS_Y dans ecran_reglage_fin.c/ecran_ventilateurs.c :
 * le bas de la dernière ligne, en coordonnées ABSOLUES d'écran, doit rester
 * au-dessus du bandeau de notification. */
_Static_assert(BARRE_HAUTEUR_ECRAN + LIGNE_ACCEL_TO_DECEL_Y + LIGNE_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la derniere ligne du panneau Limits chevauche la bande du bandeau de notification");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Tampon suffisant pour {"script":"<gcode>"} -- même raisonnement que
 * GCODE_ARGS_MAX dans ecran_reglage_fin.c/ecran_deplacer.c/
 * ecran_temperatures.c/ecran_extruder.c/ecran_ventilateurs.c. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Pas fixe par ligne, INDEXÉ par klipper_lim_champ_t -- valeurs imposées par
 * le brief (task-5-brief.md) : velocity ±10 mm/s, accel ±100 mm/s^2, sqv ±1
 * mm/s, accel_to_decel ±100 mm/s^2. */
static const uint32_t PAS[ECRAN_LIMITES_LIGNE_NB] = {
    [KLIPPER_LIM_VELOCITY]       = 10,
    [KLIPPER_LIM_ACCEL]          = 100,
    [KLIPPER_LIM_SQV]            = 1,
    [KLIPPER_LIM_ACCEL_TO_DECEL] = 100,
};

/* Libellés + unités, même index. Unité ASCII pure ("mm/s^2", pas "mm/s²") --
 * brief : "On-screen ASCII/English". */
static const char *const LIBELLES[ECRAN_LIMITES_LIGNE_NB] = {
    [KLIPPER_LIM_VELOCITY]       = "Max Velocity",
    [KLIPPER_LIM_ACCEL]          = "Max Acceleration",
    [KLIPPER_LIM_SQV]            = "Square Corner Velocity",
    [KLIPPER_LIM_ACCEL_TO_DECEL] = "Accel to Decel",
};
static const char *const UNITES[ECRAN_LIMITES_LIGNE_NB] = {
    [KLIPPER_LIM_VELOCITY]       = "mm/s",
    [KLIPPER_LIM_ACCEL]          = "mm/s^2",
    [KLIPPER_LIM_SQV]            = "mm/s",
    [KLIPPER_LIM_ACCEL_TO_DECEL] = "mm/s^2",
};

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode() de ecran_reglage_fin.c (voir son commentaire
 * complet sur pourquoi cJSON plutot qu'un snprintf a la main). Copie plutot
 * que partage, meme choix que le reste de ce depot. */
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

/* "250 mm/s", ou "-" si `valeur` est encore a 0 (pas encore recu, voir
 * etat_klipper.h) -- jamais "0 mm/s", qui laisserait croire a une limite
 * reellement nulle plutot qu'a une absence de lecture. Toutes ces limites
 * sont des grandeurs physiques strictement positives une fois recues -- une
 * valeur negative ne peut venir que d'un etat corrompu, traitee ici comme
 * "pas encore recue" par la meme garde (`<= 0.0f`). */
static void formater_limite(char *sortie, size_t taille, float valeur, const char *unite)
{
    if (!isfinite(valeur) || valeur <= 0.0f) {
        snprintf(sortie, taille, "-");
        return;
    }
    snprintf(sortie, taille, "%.0f %s", (double)valeur, unite);
}

/* Borne un entier cible dans [1, 100000] -- meme borne que
 * klipper_gcode_limite_vitesse() (voir klipper_gcode.h). Comparaison en
 * DOUBLE (comme les helpers double_vers_*_borne de moonraker_rpc.c) : `d`
 * peut deborder un uint32_t vers le haut (valeur connue enorme + pas) ou vers
 * le bas (valeur connue proche de 0 - pas), jamais d'UB sur le cast final. */
static uint32_t borner_limite(double d)
{
    if (d < 1.0) {
        return 1;
    }
    if (d > 100000.0) {
        return 100000;
    }
    return (uint32_t)d;
}

/* Bouton +/- d'une des quatre lignes : relit la derniere valeur connue de CE
 * champ (mise en cache par mettre_a_jour(), AU MOMENT DU CLIC -- jamais
 * depuis l'etat backend directement, meme discipline que
 * ecran_reglage_fin.c pour Speed/Flow), arrondit au plus proche, applique
 * +/- le pas fixe de la ligne, borne [1, 100000], puis envoie
 * SET_VELOCITY_LIMIT via klipper_gcode_limite_vitesse(). */
static void bouton_cb(lv_event_t *e)
{
    ecran_limites_bouton_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }
    if (info->champ < 0 || info->champ >= ECRAN_LIMITES_LIGNE_NB) {
        return;
    }

    double base = round((double)info->ctx->valeurs_connues[info->champ]);
    double cible_valeur = base + (double)info->signe * (double)PAS[info->champ];
    uint32_t valeur = borner_limite(cible_valeur);

    char script[KLIPPER_GCODE_MAX];
    if (klipper_gcode_limite_vitesse(script, sizeof(script), info->champ, valeur)) {
        envoyer_gcode(script);
    }
}

/* Bouton -/+ -- copie de bouton_creer() de ecran_reglage_fin.c
 * (lv_obj_remove_style_all() ôte le thème par défaut ET sa transition de
 * couleur animée, même raison). */
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

/* Légende statique en tête d'une ligne -- copie de legende_creer() de
 * ecran_reglage_fin.c. */
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

/* Valeur relue d'une ligne (ex. "250 mm/s", "-") -- texte centré, grisée sur
 * donnees_perimees OU valeur pas encore reçue par mettre_a_jour() (voir plus
 * bas). */
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

/* Ligne titre + valeur + bouton "-" + bouton "+", à `y` -- factorise les
 * quatre lignes identiques de construire() (aucun bouton Reset ici,
 * contrairement a ecran_reglage_fin.c : voir le commentaire de tete du .h). */
static void ligne_creer(lv_obj_t *parent, const char *titre, lv_coord_t y, lv_obj_t **valeur,
                         lv_obj_t **bouton_moins, lv_obj_t **bouton_plus)
{
    legende_creer(parent, titre, LIGNE_LABEL_X, y + (LIGNE_HAUTEUR - 20) / 2, LIGNE_LABEL_LARGEUR);
    *bouton_moins = bouton_creer(parent, "-", LIGNE_MOINS_X, y, LIGNE_BOUTON_LARGEUR, LIGNE_HAUTEUR);
    *valeur = valeur_creer(parent, LIGNE_VALEUR_X, y, LIGNE_VALEUR_LARGEUR, LIGNE_HAUTEUR);
    *bouton_plus = bouton_creer(parent, "+", LIGNE_PLUS_X, y, LIGNE_BOUTON_LARGEUR, LIGNE_HAUTEUR);
}

static void ecran_limites_construire(lv_obj_t *parent, void *contexte)
{
    ecran_limites_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- quatre lignes, ORDRE FIXE = ordre de klipper_lim_champ_t --------- */
    static const lv_coord_t Y[ECRAN_LIMITES_LIGNE_NB] = {
        [KLIPPER_LIM_VELOCITY]       = LIGNE_VELOCITY_Y,
        [KLIPPER_LIM_ACCEL]          = LIGNE_ACCEL_Y,
        [KLIPPER_LIM_SQV]            = LIGNE_SQV_Y,
        [KLIPPER_LIM_ACCEL_TO_DECEL] = LIGNE_ACCEL_TO_DECEL_Y,
    };
    for (int champ = 0; champ < ECRAN_LIMITES_LIGNE_NB; champ++) {
        ligne_creer(parent, LIBELLES[champ], Y[champ], &ctx->valeurs[champ],
                    &ctx->boutons[champ * 2], &ctx->boutons[champ * 2 + 1]);
    }

    static const struct {
        klipper_lim_champ_t champ;
        int8_t               signe;
    } BOUTON_DEFS[ECRAN_LIMITES_BOUTON_NB] = {
        [ECRAN_LIMITES_BOUTON_VELOCITY_NEG]       = { KLIPPER_LIM_VELOCITY, -1 },
        [ECRAN_LIMITES_BOUTON_VELOCITY_POS]       = { KLIPPER_LIM_VELOCITY, 1 },
        [ECRAN_LIMITES_BOUTON_ACCEL_NEG]          = { KLIPPER_LIM_ACCEL, -1 },
        [ECRAN_LIMITES_BOUTON_ACCEL_POS]          = { KLIPPER_LIM_ACCEL, 1 },
        [ECRAN_LIMITES_BOUTON_SQV_NEG]            = { KLIPPER_LIM_SQV, -1 },
        [ECRAN_LIMITES_BOUTON_SQV_POS]            = { KLIPPER_LIM_SQV, 1 },
        [ECRAN_LIMITES_BOUTON_ACCEL_TO_DECEL_NEG] = { KLIPPER_LIM_ACCEL_TO_DECEL, -1 },
        [ECRAN_LIMITES_BOUTON_ACCEL_TO_DECEL_POS] = { KLIPPER_LIM_ACCEL_TO_DECEL, 1 },
    };
    for (uint8_t i = 0; i < ECRAN_LIMITES_BOUTON_NB; i++) {
        ctx->bouton_infos[i].ctx = ctx;
        ctx->bouton_infos[i].champ = BOUTON_DEFS[i].champ;
        ctx->bouton_infos[i].signe = BOUTON_DEFS[i].signe;
        lv_obj_add_event_cb(ctx->boutons[i], bouton_cb, LV_EVENT_CLICKED, &ctx->bouton_infos[i]);
    }
}

/* Relit les quatre valeurs, formate, grise (C3) sur donnees_perimees OU
 * valeur pas encore reçue (0.0f, ou non finie sur un état corrompu -- même
 * garde défensive que consigne_u16() dans ecran_temperatures.c vis-à-vis
 * d'un état corrompu). Voir le commentaire de tête du .h : les deux cas
 * partagent la même couleur grise, un choix délibéré. */
static void ecran_limites_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_limites_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    const float brutes[ECRAN_LIMITES_LIGNE_NB] = {
        [KLIPPER_LIM_VELOCITY]       = e->limite_velocity,
        [KLIPPER_LIM_ACCEL]          = e->limite_accel,
        [KLIPPER_LIM_SQV]            = e->limite_square_corner,
        [KLIPPER_LIM_ACCEL_TO_DECEL] = e->limite_accel_to_decel,
    };

    for (int champ = 0; champ < ECRAN_LIMITES_LIGNE_NB; champ++) {
        float valeur = isfinite(brutes[champ]) ? brutes[champ] : 0.0f;

        /* Mise en cache AVANT tout affichage -- relue par bouton_cb() AU
         * MOMENT DU CLIC (voir le commentaire de tête du .h). */
        ctx->valeurs_connues[champ] = valeur;

        char texte[24];
        formater_limite(texte, sizeof(texte), valeur, UNITES[champ]);
        lv_label_set_text(ctx->valeurs[champ], texte);

        bool grise = donnees_perimees || valeur <= 0.0f;
        uint32_t couleur = grise ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
        lv_obj_set_style_text_color(ctx->valeurs[champ], lv_color_hex(couleur), 0);
    }
}

const ecran_desc_t ECRAN_LIMITES = {
    .id = "limites",
    .titre = "Limits",
    .taille_contexte = sizeof(ecran_limites_ctx_t),
    .construire = ecran_limites_construire,
    .mettre_a_jour = ecran_limites_mettre_a_jour,
    .detruire = NULL,
};
