/* Implémentation : voir ecran_accueil.h pour le contrat.
 *
 * Mise en page (800x436, sous la barre d'état construite par habillage.c) :
 * deux tuiles de température en haut, le nom de fichier juste dessous, une
 * barre de progression pleine largeur, trois boutons en bas. Toutes les
 * constantes de position sont dérivées les unes des autres (voir les macros
 * ci-dessous) plutôt que recopiées, pour qu'un futur ajustement de l'une
 * d'entre elles ne désaligne pas silencieusement le reste de la colonne. */
#include "ecran_accueil.h"

#include <string.h>

#include "etat_klipper.h"

#define LARGEUR_CONTENU 800

#define MARGE          20
#define TUILE_LARGEUR 380
#define TUILE_HAUTEUR 140
#define TUILE_Y        16

#define FICHIER_Y       (TUILE_Y + TUILE_HAUTEUR + 10)
#define FICHIER_HAUTEUR  30

#define PROGRESSION_Y       (FICHIER_Y + FICHIER_HAUTEUR + 10)
#define PROGRESSION_HAUTEUR  40
#define PROGRESSION_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)
/* Décalage horizontal du libellé de temps restant par rapport au CENTRE de
 * la barre, où vit déjà le pourcentage (posé par progression_creer(), voir
 * progression.h) : assez grand pour ne jamais chevaucher "100.0%", le texte
 * de pourcentage le plus large possible. */
#define TEMPS_DECALAGE_X 120

#define BOUTONS_Y      (PROGRESSION_Y + PROGRESSION_HAUTEUR + 20)
#define BOUTON_LARGEUR 230
#define BOUTON_HAUTEUR  70
#define BOUTON_ECART    35

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* imposee par le brief : gris de peremption */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_BOUTON_URGENCE   0xE74C3C /* meme rouge que la pastille "hors ligne", habillage.c */
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Un bouton créé ici mais INERTE : aucun lv_obj_add_event_cb(). Câbler une
 * action est le travail de la tâche 9, une fois la file de commandes en
 * place (voir le commentaire de tête de ce fichier et celui de
 * ecran_accueil.h). */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, uint32_t couleur_fond, lv_coord_t x)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, BOUTON_LARGEUR, BOUTON_HAUTEUR);
    lv_obj_set_pos(bouton, x, BOUTONS_Y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(couleur_fond), 0);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 10, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

static void ecran_accueil_construire(lv_obj_t *parent, void *contexte)
{
    ecran_accueil_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    tuile_creer(&ctx->buse, parent, "Nozzle");
    lv_obj_set_size(ctx->buse.racine, TUILE_LARGEUR, TUILE_HAUTEUR);
    lv_obj_set_pos(ctx->buse.racine, MARGE, TUILE_Y);

    tuile_creer(&ctx->plateau, parent, "Bed");
    lv_obj_set_size(ctx->plateau.racine, TUILE_LARGEUR, TUILE_HAUTEUR);
    lv_obj_set_pos(ctx->plateau.racine, MARGE + TUILE_LARGEUR + MARGE, TUILE_Y);

    ctx->fichier = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->fichier, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->fichier, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    /* Points de suspension plutot que deborder du cadre sur un nom de
     * fichier long (brief) : LV_LABEL_LONG_DOT exige une largeur explicite,
     * pas LV_SIZE_CONTENT, sinon il n'a rien contre quoi tronquer. */
    lv_label_set_long_mode(ctx->fichier, LV_LABEL_LONG_DOT);
    lv_obj_set_size(ctx->fichier, LARGEUR_CONTENU - 2 * MARGE, FICHIER_HAUTEUR);
    lv_label_set_text(ctx->fichier, "");
    lv_obj_set_pos(ctx->fichier, MARGE, FICHIER_Y);

    progression_creer(&ctx->progression, parent);
    lv_obj_set_size(ctx->progression.racine, PROGRESSION_LARGEUR, PROGRESSION_HAUTEUR);
    lv_obj_set_pos(ctx->progression.racine, MARGE, PROGRESSION_Y);

    ctx->temps = lv_label_create(ctx->progression.racine);
    lv_obj_set_style_text_font(ctx->temps, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->temps, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->temps, "");
    lv_obj_align(ctx->temps, LV_ALIGN_CENTER, TEMPS_DECALAGE_X, 0);

    ctx->bouton_pause = bouton_creer(parent, "Pause", COULEUR_BOUTON, MARGE);
    ctx->bouton_annuler =
        bouton_creer(parent, "Cancel", COULEUR_BOUTON, MARGE + BOUTON_LARGEUR + BOUTON_ECART);
    ctx->bouton_urgence = bouton_creer(parent, "E-STOP", COULEUR_BOUTON_URGENCE,
                                        MARGE + 2 * (BOUTON_LARGEUR + BOUTON_ECART));
}

static void ecran_accueil_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_accueil_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    char valeur[16];
    char consigne[16];
    char temps[16];

    ui_format_temperature(valeur, sizeof(valeur), e->buse_actuelle);
    ui_format_temperature(consigne, sizeof(consigne), e->buse_consigne);
    tuile_definir_valeur(&ctx->buse, valeur);
    tuile_definir_consigne(&ctx->buse, consigne);

    ui_format_temperature(valeur, sizeof(valeur), e->plateau_actuel);
    ui_format_temperature(consigne, sizeof(consigne), e->plateau_consigne);
    tuile_definir_valeur(&ctx->plateau, valeur);
    tuile_definir_consigne(&ctx->plateau, consigne);

    /* Copie bornee EXPLICITEMENT avant de tendre le texte a LVGL : le champ
     * fichier peut occuper la totalite de KLIPPER_FICHIER_MAX sans octet nul
     * (etat_klipper_t est un POD a champs fixes, voir etat_klipper.h) -- le
     * passer directement a lv_label_set_text() (qui appelle strlen()) lirait
     * au-dela du tampon. */
    char nom[KLIPPER_FICHIER_MAX + 1];
    memcpy(nom, e->fichier, KLIPPER_FICHIER_MAX);
    nom[KLIPPER_FICHIER_MAX] = '\0';
    lv_label_set_text(ctx->fichier, nom);

    progression_definir(&ctx->progression, e->progression);
    ui_format_duree(temps, sizeof(temps), e->temps_restant_s);
    lv_label_set_text(ctx->temps, temps);

    /* Grisage systematique a chaque appel, jamais incremental (meme lecon
     * que tuile_griser()/progression_griser(), voir la revue de la tache 4) :
     * un appel avec donnees_perimees=false doit rendre exactement les
     * couleurs normales, y compris apres plusieurs allers-retours. */
    tuile_griser(&ctx->buse, donnees_perimees);
    tuile_griser(&ctx->plateau, donnees_perimees);
    progression_griser(&ctx->progression, donnees_perimees);
    uint32_t couleur_texte = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    lv_obj_set_style_text_color(ctx->fichier, lv_color_hex(couleur_texte), 0);
    lv_obj_set_style_text_color(ctx->temps, lv_color_hex(couleur_texte), 0);
}

const ecran_desc_t ECRAN_ACCUEIL = {
    .id = "accueil",
    .titre = "Home",
    .taille_contexte = sizeof(ecran_accueil_ctx_t),
    .construire = ecran_accueil_construire,
    .mettre_a_jour = ecran_accueil_mettre_a_jour,
    .detruire = NULL,
};
