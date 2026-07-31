/* Implémentation : voir ecran_deplacer.h pour le contrat et les écarts
 * délibérés par rapport à ecran_accueil_idle.c (pas de grisage par axe, pas
 * de confirmation avant Home).
 *
 * Mise en page (800x436, sous la barre d'état construite par habillage.c,
 * même convention que ecran_accueil_idle.c) : une ligne de position + outil
 * actif en haut, puis une seule rangée horizontale -- pad de jog XY (croix,
 * Y+ en haut / X-/X+ au milieu / Y- en bas) + colonne Z séparée + panneau des
 * deux sélecteurs (Pas, Vitesse) -- et enfin la rangée Home (All/X/Y/Z) en
 * pleine largeur. Toutes les constantes de position sont vérifiées les unes
 * par rapport aux autres via _Static_assert (même discipline que
 * ecran_accueil_idle.c/ecran_macros.c) : un futur ajustement qui ferait
 * déborder ou chevaucher une zone devient une erreur de compilation. */
#include "ecran_deplacer.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "etat_klipper.h"
#include "klipper_gcode.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 800
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE       14
#define ZONE_ECART  14 /* écart vertical entre la ligne de position, la rangée jog/sélecteurs et Home */

#define POSITION_Y         10
#define POSITION_HAUTEUR   26
#define OUTIL_ACTIF_LARGEUR 220

#define CONTROLES_Y (POSITION_Y + POSITION_HAUTEUR + ZONE_ECART)

/* Pad de jog EN GRAND (brief : "gros", cible tactile >= 44px très largement
 * dépassée) -- même structure à trois lignes/trois colonnes que
 * ecran_accueil_idle.c (JOG_ECART_LIGNE vertical, JOG_ECART_COLONNE
 * horizontal, jamais confondus, voir son commentaire de tête pour la leçon
 * "fix round 1" qui a motivé cette séparation). */
#define JOG_BOUTON_LARGEUR 110
#define JOG_BOUTON_HAUTEUR  90
#define JOG_ECART_LIGNE     10
#define JOG_ECART_COLONNE   16

#define JOG_PAD_LARGEUR (3 * JOG_BOUTON_LARGEUR + 2 * JOG_ECART_COLONNE)
#define JOG_PAD_HAUTEUR (3 * JOG_BOUTON_HAUTEUR + 2 * JOG_ECART_LIGNE)

#define JOG_Z_LARGEUR JOG_BOUTON_LARGEUR
#define JOG_Z_HAUTEUR ((JOG_PAD_HAUTEUR - JOG_ECART_LIGNE) / 2)
/* Double de JOG_ECART_COLONNE, même raison que ecran_accueil_idle.c : la
 * colonne Z se lit comme un groupe séparé du pad XY, pas une 3e colonne. */
#define JOG_Z_ECART_COLONNE (2 * JOG_ECART_COLONNE)

/* Panneau des deux sélecteurs (Pas, Vitesse), à droite de la colonne Z --
 * occupe tout l'espace restant de la rangée jog/sélecteurs. */
#define SELECTEURS_ECART_PAD JOG_Z_ECART_COLONNE
#define SELECTEURS_X (MARGE + JOG_PAD_LARGEUR + JOG_Z_ECART_COLONNE + JOG_Z_LARGEUR + SELECTEURS_ECART_PAD)
#define SELECTEURS_LARGEUR (LARGEUR_CONTENU - MARGE - SELECTEURS_X)
#define SELECTEURS_HAUTEUR JOG_PAD_HAUTEUR

#define SELECTEUR_CAPTION_HAUTEUR 22
#define SELECTEUR_HAUTEUR         50
#define SELECTEUR_ECART_INTERNE   8  /* entre une légende et son sélecteur */
#define SELECTEUR_ECART_GROUPE    14 /* entre le groupe Pas et le groupe Vitesse */

#define PAS_LABEL_Y      CONTROLES_Y
#define PAS_SELECTEUR_Y  (PAS_LABEL_Y + SELECTEUR_CAPTION_HAUTEUR + SELECTEUR_ECART_INTERNE)
#define VITESSE_LABEL_Y  (PAS_SELECTEUR_Y + SELECTEUR_HAUTEUR + SELECTEUR_ECART_GROUPE)
#define VITESSE_SELECTEUR_Y (VITESSE_LABEL_Y + SELECTEUR_CAPTION_HAUTEUR + SELECTEUR_ECART_INTERNE)

/* Rangée Home, pleine largeur du contenu, sous la rangée jog/sélecteurs. */
#define HOME_Y (CONTROLES_Y + JOG_PAD_HAUTEUR + ZONE_ECART)
#define HOME_HAUTEUR 60
#define HOME_ECART_BOUTON 16
#define HOME_LARGEUR_TOTALE (LARGEUR_CONTENU - 2 * MARGE)
#define HOME_BOUTON_LARGEUR ((HOME_LARGEUR_TOTALE - (ECRAN_DEPLACER_HOME_NB - 1) * HOME_ECART_BOUTON) / \
                              ECRAN_DEPLACER_HOME_NB)

_Static_assert(MARGE + JOG_PAD_LARGEUR + JOG_Z_ECART_COLONNE + JOG_Z_LARGEUR + SELECTEURS_ECART_PAD +
                    SELECTEURS_LARGEUR + MARGE ==
                    LARGEUR_CONTENU,
                "pad + colonne Z + panneau des selecteurs ne remplissent plus exactement la largeur du contenu");
_Static_assert(2 * JOG_Z_HAUTEUR + JOG_ECART_LIGNE == JOG_PAD_HAUTEUR,
                "la colonne Z (2 boutons) ne fait plus exactement la hauteur du pad XY");
_Static_assert(VITESSE_SELECTEUR_Y + SELECTEUR_HAUTEUR <= CONTROLES_Y + SELECTEURS_HAUTEUR,
                "le panneau des selecteurs deborde de la hauteur reservee (hauteur du pad XY)");
_Static_assert(ECRAN_DEPLACER_HOME_NB * HOME_BOUTON_LARGEUR + (ECRAN_DEPLACER_HOME_NB - 1) * HOME_ECART_BOUTON ==
                    HOME_LARGEUR_TOTALE,
                "les boutons Home ne remplissent plus exactement la largeur du contenu");
_Static_assert(HOME_Y + HOME_HAUTEUR <= HAUTEUR_CONTENU,
                "la rangee Home deborde de la hauteur du contenu");

/* Vitesses de jog (brief, valeurs figées) : index 0=Lent, 1=Moyen, 2=Rapide
 * du sélecteur Vitesse -- XY et Z ont chacun leur propre table (Z est
 * mécaniquement plus lent, même raisonnement que JOG_VITESSE_Z_MM_MIN dans
 * ecran_accueil_idle.c). */
static const uint16_t VITESSE_XY[3] = { 600, 3000, 6000 };
static const uint16_t VITESSE_Z[3]  = { 300, 600, 1200 };

/* Pas de jog (mm) par index du sélecteur Pas -- même valeurs que
 * selecteur_pas.h (0.1/1/10/100), reprises ici en tableau local puisque cet
 * écran utilise le sélecteur générique (selecteur_choix.h, tâche 1) plutôt
 * que le widget dédié selecteur_pas.h. */
static const float PAS_MM[4] = { 0.1f, 1.0f, 10.0f, 100.0f };

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Tampon suffisant pour {"script":"<gcode>"} -- même raisonnement que
 * ECRAN_ACCUEIL_IDLE_GCODE_ARGS_MAX (voir son commentaire complet dans
 * ecran_accueil_idle.c) : KLIPPER_GCODE_MAX plus la marge du wrapper JSON et
 * de l'échappement des `\n` réels que klipper_gcode_jog() produit. */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Construit {"script":"<script>"} via cJSON -- copie exacte de
 * construire_arguments_gcode()/envoyer_gcode() dans ecran_accueil_idle.c
 * (voir son commentaire complet pour la justification : de vrais octets
 * 0x0A dans un script de jog doivent être échappés par un JSON conforme
 * RFC 8259, jamais un snprintf à la main). Copie plutôt que partage, même
 * choix que le reste de ce dépôt (voir le commentaire de tête de
 * ecran_deplacer.h). */
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

/* Bouton de jog/homing, EN GRAND (110x90 pour le pad/colonne Z, redimensionné
 * par les boucles de construction plus bas pour Home) -- lv_obj_remove_style_all()
 * ôte le thème par défaut ET sa transition de couleur animée sur bg_color,
 * même choix que home_bouton_creer() dans ecran_accueil_idle.c (voir son
 * commentaire complet : évite d'alourdir style_trans_ll côté host-test).
 * Aucun état DISABLED n'est jamais posé sur ces boutons par ce fichier (voir
 * le commentaire de tête de ecran_deplacer.h, "ECART délibéré") : ce
 * constructeur n'a donc pas besoin du style LV_STATE_DISABLED dédié que
 * jog_bouton_creer()/home_bouton_creer() de ecran_accueil_idle.c posent. */
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

/* Lit le pas ET la vitesse courants (au moment du clic, jamais mis en cache
 * ailleurs -- même discipline que jog_bouton_cb() dans ecran_accueil_idle.c),
 * construit le gcode via klipper_gcode_jog() et l'envoie. Index bornés
 * défensivement (selecteur_choix_index() rend déjà 0..nb-1, mais ce fichier
 * ne fait jamais confiance aveuglément à un état externe, même politique que
 * le reste de ce dépôt). */
static void jog_bouton_cb(lv_event_t *e)
{
    ecran_deplacer_jog_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    uint8_t indice_pas = selecteur_choix_index(&info->ctx->selecteur_pas);
    if (indice_pas >= 4) {
        indice_pas = 3;
    }
    float pas = PAS_MM[indice_pas];

    uint8_t indice_vitesse = selecteur_choix_index(&info->ctx->selecteur_vitesse);
    if (indice_vitesse >= 3) {
        indice_vitesse = 2;
    }
    uint16_t vitesse = (info->axe == 'Z') ? VITESSE_Z[indice_vitesse] : VITESSE_XY[indice_vitesse];

    char script[KLIPPER_GCODE_MAX];
    if (!klipper_gcode_jog(script, sizeof(script), info->axe, info->signe * pas, vitesse)) {
        return; /* ne devrait jamais arriver : axe/pas/vitesse toujours valides depuis ce pad */
    }
    envoyer_gcode(script);
}

/* Home : clic direct, AUCUNE confirmation (voir le commentaire de tête de
 * ecran_deplacer.h, "ECART délibéré n2"). */
static void home_bouton_cb(lv_event_t *e)
{
    ecran_deplacer_home_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    char script[KLIPPER_GCODE_MAX];
    if (klipper_gcode_home(script, sizeof(script), info->masque)) {
        envoyer_gcode(script);
    }
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

static void ecran_deplacer_construire(lv_obj_t *parent, void *contexte)
{
    ecran_deplacer_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- ligne de position + outil actif -------------------------------- */
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

    /* --- pad de jog XY (croix, Y+ haut / X-/X+ milieu / Y- bas) + colonne Z,
     * ORDRE FIXE (ECRAN_DEPLACER_JOG_*, brief : "X-/X+/Y-/Y+/Z+/Z-"). Même
     * idiome de tableau que JOG_DEFS dans ecran_accueil_idle.c. --------- */
    lv_coord_t col0_x = MARGE;
    lv_coord_t col1_x = col0_x + JOG_BOUTON_LARGEUR + JOG_ECART_COLONNE;
    lv_coord_t col2_x = col1_x + JOG_BOUTON_LARGEUR + JOG_ECART_COLONNE;
    lv_coord_t row0_y = CONTROLES_Y;
    lv_coord_t row1_y = row0_y + JOG_BOUTON_HAUTEUR + JOG_ECART_LIGNE;
    lv_coord_t row2_y = row1_y + JOG_BOUTON_HAUTEUR + JOG_ECART_LIGNE;
    lv_coord_t z_x = MARGE + JOG_PAD_LARGEUR + JOG_Z_ECART_COLONNE;
    lv_coord_t z_y0 = CONTROLES_Y;
    lv_coord_t z_y1 = z_y0 + JOG_Z_HAUTEUR + JOG_ECART_LIGNE;

    const struct {
        char        axe;
        float       signe;
        const char *libelle;
        lv_coord_t  x, y, largeur, hauteur;
    } JOG_DEFS[ECRAN_DEPLACER_JOG_NB] = {
        [ECRAN_DEPLACER_JOG_X_NEG] = { 'X', -1.0f, "X-", col0_x, row1_y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        [ECRAN_DEPLACER_JOG_X_POS] = { 'X',  1.0f, "X+", col2_x, row1_y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        [ECRAN_DEPLACER_JOG_Y_NEG] = { 'Y', -1.0f, "Y-", col1_x, row2_y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        [ECRAN_DEPLACER_JOG_Y_POS] = { 'Y',  1.0f, "Y+", col1_x, row0_y, JOG_BOUTON_LARGEUR, JOG_BOUTON_HAUTEUR },
        [ECRAN_DEPLACER_JOG_Z_POS] = { 'Z',  1.0f, "Z+", z_x,    z_y0,   JOG_Z_LARGEUR,       JOG_Z_HAUTEUR },
        [ECRAN_DEPLACER_JOG_Z_NEG] = { 'Z', -1.0f, "Z-", z_x,    z_y1,   JOG_Z_LARGEUR,       JOG_Z_HAUTEUR },
    };
    for (uint8_t i = 0; i < ECRAN_DEPLACER_JOG_NB; i++) {
        ctx->jog_boutons[i] = bouton_creer(parent, JOG_DEFS[i].libelle, &lv_font_montserrat_28, JOG_DEFS[i].x,
                                            JOG_DEFS[i].y, JOG_DEFS[i].largeur, JOG_DEFS[i].hauteur);
        ctx->jog_infos[i].ctx = ctx;
        ctx->jog_infos[i].axe = JOG_DEFS[i].axe;
        ctx->jog_infos[i].signe = JOG_DEFS[i].signe;
        lv_obj_add_event_cb(ctx->jog_boutons[i], jog_bouton_cb, LV_EVENT_CLICKED, &ctx->jog_infos[i]);
    }

    /* --- panneau des sélecteurs (Pas, Vitesse), à droite de la colonne Z -- */
    ctx->label_pas = legende_creer(parent, "Pas", SELECTEURS_X, PAS_LABEL_Y, SELECTEURS_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_pas, parent,
                           (const char *const[]){ "0.1", "1", "10", "100" }, 4, ECRAN_DEPLACER_PAS_DEFAUT);
    lv_obj_set_size(ctx->selecteur_pas.racine, SELECTEURS_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_pas.racine, SELECTEURS_X, PAS_SELECTEUR_Y);

    ctx->label_vitesse = legende_creer(parent, "Vitesse", SELECTEURS_X, VITESSE_LABEL_Y, SELECTEURS_LARGEUR);
    selecteur_choix_creer(&ctx->selecteur_vitesse, parent,
                           (const char *const[]){ "Lent", "Moyen", "Rapide" }, 3, ECRAN_DEPLACER_VITESSE_DEFAUT);
    lv_obj_set_size(ctx->selecteur_vitesse.racine, SELECTEURS_LARGEUR, SELECTEUR_HAUTEUR);
    lv_obj_set_pos(ctx->selecteur_vitesse.racine, SELECTEURS_X, VITESSE_SELECTEUR_Y);

    /* --- rangée Home (All/X/Y/Z), pleine largeur -- même convention
     * ECRAN_DEPLACER_HOME_* que le pad de jog ci-dessus. ------------------ */
    const struct {
        uint8_t     masque;
        const char *libelle;
    } HOME_DEFS[ECRAN_DEPLACER_HOME_NB] = {
        [ECRAN_DEPLACER_HOME_ALL] = { 0x7u, "All" },
        [ECRAN_DEPLACER_HOME_X]   = { 0x1u, "X" },
        [ECRAN_DEPLACER_HOME_Y]   = { 0x2u, "Y" },
        [ECRAN_DEPLACER_HOME_Z]   = { 0x4u, "Z" },
    };
    for (uint8_t i = 0; i < ECRAN_DEPLACER_HOME_NB; i++) {
        lv_coord_t x = MARGE + (lv_coord_t)(i * (HOME_BOUTON_LARGEUR + HOME_ECART_BOUTON));
        ctx->home_boutons[i] = bouton_creer(parent, HOME_DEFS[i].libelle, &lv_font_montserrat_20, x, HOME_Y,
                                             HOME_BOUTON_LARGEUR, HOME_HAUTEUR);
        ctx->home_infos[i].ctx = ctx;
        ctx->home_infos[i].masque = HOME_DEFS[i].masque;
        lv_obj_add_event_cb(ctx->home_boutons[i], home_bouton_cb, LV_EVENT_CLICKED, &ctx->home_infos[i]);
    }
}

/* Écrit "%.1f" si `reference` est vrai, "--" sinon -- copie exacte de
 * formater_axe() dans ecran_accueil_idle.c (voir son commentaire : ne jamais
 * présenter comme mesurée une position qu'aucun homing n'a établie). */
static void formater_axe(char *sortie, size_t taille, float valeur, bool reference)
{
    if (!reference) {
        snprintf(sortie, taille, "--");
        return;
    }
    snprintf(sortie, taille, "%.1f", (double)valeur);
}

/* Brief (step 3) : "mettre_a_jour : ligne position + outil actif (grise si
 * donnees_perimees)" -- rien de plus. Voir le commentaire de tête de
 * ecran_deplacer.h ("ECART délibéré") pour pourquoi le pad de jog/homing
 * n'est PAS désactivé ici, contrairement à ecran_accueil_idle.c. */
static void ecran_deplacer_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_deplacer_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

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
    if (e->nb_extrudeurs > 0) {
        snprintf(texte_outil, sizeof(texte_outil), "Active: T%u", (unsigned)e->outil_actif);
    } else {
        snprintf(texte_outil, sizeof(texte_outil), "Active: --");
    }
    lv_label_set_text(ctx->outil_actif_nom, texte_outil);

    uint32_t couleur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(ctx->position, lv_color_hex(couleur), 0);
    lv_obj_set_style_text_color(ctx->outil_actif_nom, lv_color_hex(couleur), 0);
}

const ecran_desc_t ECRAN_DEPLACER = {
    .id = "deplacer",
    .titre = "Move",
    .taille_contexte = sizeof(ecran_deplacer_ctx_t),
    .construire = ecran_deplacer_construire,
    .mettre_a_jour = ecran_deplacer_mettre_a_jour,
    .detruire = NULL,
};
